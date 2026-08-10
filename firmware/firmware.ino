/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Feature: Real-Time Sub-Sample Semitone Transposition & Core 1 Audio Task (No ISR Panic)
 *
 * Hardware Pin Map:
 *   Switch 1: GPIO32 (INPUT_PULLUP) -> Decrease semitone offset by 1 (-1 semitone) & Re-trigger sample
 *   Switch 2: GPIO33 (INPUT_PULLUP) -> Increase semitone offset by 1 (+1 semitone) & Re-trigger sample
 *   LED 1:    GPIO18 (OUTPUT) -> HIGH when Switch 1 (Decrease) is pressed
 *   LED 2:    GPIO19 (OUTPUT) -> HIGH when Switch 2 (Increase) is pressed
 *   DAC:      GPIO25 (Internal DAC1 Output for Headphone Jack)
 * ============================================================================= */

#include <Arduino.h>
#include <math.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"
#include "vhl_loader.h"
#include "vhl_harmonium_package.h"
#include "poly_synth.h"
#include "vhr_format.h"
#include "vhr_player.h"
#include "event_bus.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#endif

/* =============================================================================
 * Shared Runtime Data
 * ============================================================================= */

static vhl_loader_t g_vhl_loader;

/* Sub-Sample Fractional Phase Accumulator & Pitch Transposition */
static volatile float g_sample_phase = 0.0f;       /* Sub-sample position */
static volatile int   g_semitone_offset = 0;        /* Semitone transpose (-12..+12) */
static volatile float g_pitch_ratio = 1.0f;         /* pitchRatio = 2^(semitones/12) */

/* Fast 16-to-8 Bit Triangular Noise-Shaping Dither */
static inline uint8_t pcm16_to_dac8_dither(int16_t pcm_sample)
{
    int32_t val = (int32_t)pcm_sample + 32768;

    static uint32_t s_dither_state = 123456789U;
    s_dither_state = s_dither_state * 1664525U + 1013904223U;
    int32_t dither = (int32_t)(s_dither_state >> 25) - 64;

    val += dither;
    if (val < 0)     val = 0;
    if (val > 65535) val = 65535;

    return (uint8_t)(val >> 8);
}

/* Helper to fetch frame sample (Mono or Stereo) */
static inline float get_frame_sample(const int16_t* pPcm, uint32_t frameIdx, uint8_t channels)
{
    if (channels == 2) {
        int32_t l = (int32_t)pPcm[2 * frameIdx];
        int32_t r = (int32_t)pPcm[2 * frameIdx + 1];
        return (float)((l + r) / 2);
    }
    return (float)pPcm[frameIdx];
}

/* Decompile VHL File & Print All Parameters */
static void decompile_and_print_vhl(const vhl_loader_t* pLoader)
{
    if (!pLoader || !pLoader->isLoaded) {
        Serial.println("[VHL DECOMPILER] ERROR: Invalid or unmapped VHL file!");
        return;
    }

    const VHLFileHeader* pHdr = pLoader->pHeader;

    Serial.println("=============================================================");
    Serial.println("         ESP32 VHL FILE DECOMPILER & PARSER");
    Serial.println("=============================================================");
    Serial.printf("[VHL FILE] Size: %u bytes (%0.2f KB)\n", 
                  (unsigned int)g_vhl_harmonium_package_size, 
                  (float)g_vhl_harmonium_package_size / 1024.0f);
    
    Serial.printf("[VHL HEADER] Magic: %c%c%c%c | Version: %u.%u | Flags: 0x%04X\n",
                  pHdr->magic[0], pHdr->magic[1], pHdr->magic[2], pHdr->magic[3],
                  pHdr->version_major, pHdr->version_minor, pHdr->flags);
    Serial.printf("[VHL HEADER] Target Sample Rate: %u Hz | DMA Block Size: %u B\n",
                  (unsigned int)pHdr->target_sample_rate, pHdr->dma_block_size);
    Serial.printf("[VHL HEADER] Instruments: %u | Total Regions: %u | Total Samples: %u\n",
                  pHdr->num_instruments, pHdr->total_region_count, pHdr->total_sample_count);
    Serial.printf("[VHL OFFSETS] Inst: %u | Region: %u | SampleDir: %u | Lookup: %u | PCM: %u\n\n",
                  (unsigned int)pHdr->offset_instrument_table,
                  (unsigned int)pHdr->offset_region_table,
                  (unsigned int)pHdr->offset_sample_directory,
                  (unsigned int)pHdr->offset_lookup_table,
                  (unsigned int)pHdr->offset_pcm_data);

    /* Decompile Instruments */
    for (uint16_t i = 0; i < pHdr->num_instruments; ++i) {
        const VHLInstrumentEntry* pInst = NULL;
        if (vhl_loader_get_instrument(pLoader, (uint8_t)i, &pInst) == VHL_OK) {
            Serial.printf("[VHL INSTRUMENT %u] ID: %u | Name: \"%s\" | Bank: %u | Program: %u | Regions: %u\n",
                          i, pInst->instrument_id, pInst->name, pInst->bank, pInst->program, pInst->region_count);
        }
    }

    /* Decompile Regions */
    for (uint16_t r = 0; r < pHdr->total_region_count; ++r) {
        const VHLRegion* pReg = &pLoader->pRegions[r];
        Serial.printf("\n[VHL REGION %u]\n", r);
        Serial.printf("  - Key Range: %u to %u | Velocity Range: %u to %u\n",
                      pReg->key_lo, pReg->key_hi, pReg->vel_lo, pReg->vel_hi);
        Serial.printf("  - Sample ID: %u | Loop Mode: %u | Exclusive Class: %u | Flags: 0x%02X\n",
                      pReg->sample_id, pReg->loop_mode, pReg->exclusive_class, pReg->flags);
        Serial.printf("  - Coarse Tune: %d semitones | Fine Tune: %d cents | Scale Tuning: %u%%\n",
                      pReg->coarse_tune, pReg->fine_tune, pReg->scale_tuning);
        Serial.printf("  - Attenuation: %u cB | Pan: %d (Centibels)\n",
                      pReg->attenuation_cb, pReg->pan);
        
        Serial.printf("  - Vol Envelope: Delay=%ums, Attack=%ums, Hold=%ums, Decay=%ums, Sustain=%ucB, Release=%ums\n",
                      pReg->vol_env.delay_ms, pReg->vol_env.attack_ms, pReg->vol_env.hold_ms,
                      pReg->vol_env.decay_ms, pReg->vol_env.sustain_level, pReg->vol_env.release_ms);
        Serial.printf("  - Mod Envelope: Delay=%ums, Attack=%ums, Hold=%ums, Decay=%ums, Sustain=%u, Release=%ums\n",
                      pReg->mod_env.delay_ms, pReg->mod_env.attack_ms, pReg->mod_env.hold_ms,
                      pReg->mod_env.decay_ms, pReg->mod_env.sustain_level, pReg->mod_env.release_ms);
        Serial.printf("  - Filter: Cutoff=%u Hz | Resonance=%u cB\n",
                      pReg->filter.cutoff_hz, pReg->filter.resonance_cb);
    }

    /* Decompile Samples */
    for (uint16_t s = 0; s < pHdr->total_sample_count; ++s) {
        const VHLSampleEntry* pSmpl = NULL;
        if (vhl_loader_get_sample(pLoader, s, &pSmpl) == VHL_OK) {
            float durationSec = (float)pSmpl->total_frames / (float)pSmpl->sample_rate;
            uint32_t pcmBytes = pSmpl->total_frames * pSmpl->channels * sizeof(int16_t);

            Serial.printf("\n[VHL SAMPLE %u]\n", s);
            Serial.printf("  - Data Offset: %u bytes (Absolute Flash Offset: %u)\n",
                          (unsigned int)pSmpl->data_offset,
                          (unsigned int)(pHdr->offset_pcm_data + pSmpl->data_offset));
            Serial.printf("  - Total Frames: %u frames (%0.2f seconds @ %u Hz)\n",
                          (unsigned int)pSmpl->total_frames, durationSec, (unsigned int)pSmpl->sample_rate);
            Serial.printf("  - Loop Start Frame: %u | Loop End Frame: %u\n",
                          (unsigned int)pSmpl->loop_start_frame, (unsigned int)pSmpl->loop_end_frame);
            Serial.printf("  - Root Key: %u (MIDI) | Fine Tune: %d cents | Bit Depth: %u-bit | Channels: %u (%s)\n",
                          pSmpl->root_key, pSmpl->fine_tune_cents, pSmpl->bit_depth, pSmpl->channels,
                          (pSmpl->channels == 2) ? "Stereo" : "Mono");
            Serial.printf("  - Total PCM Data Size: %u bytes\n", (unsigned int)pcmBytes);
        }
    }

    Serial.println("=============================================================");
}

/* Core 1 Real-Time Audio Task (Safe FreeRTOS Task Execution) */
static void vhl_audio_task(void* pvParameters)
{
    (void)pvParameters;

    while (1) {
        if (g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
            const VHLSampleEntry* pSmpl = &g_vhl_loader.pSamples[0];
            const int16_t* pPcmData = (const int16_t*)(g_vhl_loader.pPcmBase + pSmpl->data_offset);
            uint32_t totalFrames = pSmpl->total_frames;
            uint8_t channels = (pSmpl->channels > 0) ? pSmpl->channels : 1;

            if (pPcmData && totalFrames > 0) {
                uint32_t idx0 = (uint32_t)g_sample_phase;
                uint32_t idx1 = idx0 + 1;
                float frac = g_sample_phase - (float)idx0;

                if (idx0 >= totalFrames) {
                    g_sample_phase = 0.0f;
                    idx0 = 0;
                    idx1 = 1;
                }
                if (idx1 >= totalFrames) {
                    idx1 = 0;
                }

                /* Sub-sample linear interpolation for smooth pitch transposition */
                float s0 = get_frame_sample(pPcmData, idx0, channels);
                float s1 = get_frame_sample(pPcmData, idx1, channels);
                float interpolated = (1.0f - frac) * s0 + frac * s1;

                int16_t rawSample = (int16_t)interpolated;

                /* Apply Triangular Dither to smooth 8-bit DAC quantization */
                uint8_t dacByte = pcm16_to_dac8_dither(rawSample);

                /* Write directly to GPIO25 DAC */
                dacWrite(VHL_PIN_TEST_DAC, dacByte);

                /* Advance sub-sample phase accumulator by pitch ratio */
                g_sample_phase += g_pitch_ratio;

                delayMicroseconds(10);
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else {
            dacWrite(VHL_PIN_TEST_DAC, 128);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* Switch Scanner:
 * Switch 1 (GPIO32): Decrease semitones by 1 (-1 semitone) & Re-trigger from frame 0
 * Switch 2 (GPIO33): Increase semitones by 1 (+1 semitone) & Re-trigger from frame 0
 */
static bool s_sw1_last = false;
static bool s_sw2_last = false;

static void scan_switches_and_update(void)
{
    int raw1 = digitalRead(VHL_PIN_TEST_SWITCH_1);
    int raw2 = digitalRead(VHL_PIN_TEST_SWITCH_2);

    bool sw1_pressed = (raw1 == LOW);
    bool sw2_pressed = (raw2 == LOW);

    /* Switch 1: Decrease Semitone Offset by 1 & Re-trigger sample */
    if (sw1_pressed && !s_sw1_last) {
        if (g_semitone_offset > -12) {
            g_semitone_offset--;
        }
        g_pitch_ratio = powf(2.0f, (float)g_semitone_offset / 12.0f);
        g_sample_phase = 0.0f; /* Re-trigger sample from frame 0 */
        digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
        Serial.printf("\n[SWITCH 1 - SEMITONE DOWN] Offset: %d semitones | Pitch Ratio: %0.6fx | LED 1 ON | LED 2 OFF\n",
                      g_semitone_offset, g_pitch_ratio);
    } else if (!sw1_pressed && s_sw1_last && !sw2_pressed) {
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    }
    s_sw1_last = sw1_pressed;

    /* Switch 2: Increase Semitone Offset by 1 & Re-trigger sample */
    if (sw2_pressed && !s_sw2_last) {
        if (g_semitone_offset < 12) {
            g_semitone_offset++;
        }
        g_pitch_ratio = powf(2.0f, (float)g_semitone_offset / 12.0f);
        g_sample_phase = 0.0f; /* Re-trigger sample from frame 0 */
        digitalWrite(VHL_PIN_TEST_LED_2, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
        Serial.printf("\n[SWITCH 2 - SEMITONE UP] Offset: %d semitones | Pitch Ratio: %0.6fx | LED 2 ON | LED 1 OFF\n",
                      g_semitone_offset, g_pitch_ratio);
    } else if (!sw2_pressed && s_sw2_last && !sw1_pressed) {
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
    }
    s_sw2_last = sw2_pressed;
}

/* Hardware Interface Setup */
static void vhl_hw_init(void)
{
    pinMode(VHL_PIN_TEST_SWITCH_1, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_SWITCH_2, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_LED_1, OUTPUT);
    pinMode(VHL_PIN_TEST_LED_2, OUTPUT);

    digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    digitalWrite(VHL_PIN_TEST_LED_2, LOW);

    /* Power up DAC hardware on GPIO25 */
    dacWrite(VHL_PIN_TEST_DAC, 128);
}

/* ESP32 Setup Entry Point */
void setup()
{
    Serial.begin(115200);
    delay(300);

    /* 1. Decompile and parse VHL file */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader, g_vhl_harmonium_package, g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader Initialization Failed!");
        return;
    }

    decompile_and_print_vhl(&g_vhl_loader);

    /* 2. Initialize Hardware Pins */
    vhl_hw_init();
    Serial.println("[HARDWARE] Pins Configured: GPIO32 (SW1: Semitone -1), GPIO33 (SW2: Semitone +1), GPIO18 (LED1), GPIO19 (LED2), GPIO25 (DAC)");
    Serial.println("[MODE] REAL-TIME SEMITONE TRANSPOSITION ENGINE INITIALIZED (Core 1 Task).");
    Serial.println("[USAGE] Press SW1 to decrease pitch (-1 semitone) & restart. Press SW2 to increase pitch (+1 semitone) & restart.\n");

    /* 3. Launch Core 1 Audio Task with Priority 1 */
    xTaskCreatePinnedToCore(vhl_audio_task, "vhl_audio_task", 4096, NULL, 1, NULL, 1);
}

/* Core 0 Main Input Scan & Real-Time Pin Monitor */
static uint32_t s_last_monitor_ms = 0;

void loop()
{
    scan_switches_and_update();

    /* Real-time Diagnostic Monitor Output every 2 seconds */
    uint32_t now = millis();
    if (now - s_last_monitor_ms >= 2000) {
        s_last_monitor_ms = now;
        int sw1_val = digitalRead(VHL_PIN_TEST_SWITCH_1);
        int sw2_val = digitalRead(VHL_PIN_TEST_SWITCH_2);
        Serial.printf("[MONITOR] GPIO32 (SW1)=%d | GPIO33 (SW2)=%d | Semitones: %d | Pitch Ratio: %0.6fx | Frame: %u / %u\n",
                      sw1_val, sw2_val, g_semitone_offset, g_pitch_ratio, (unsigned int)g_sample_phase,
                      g_vhl_loader.isLoaded ? (unsigned int)g_vhl_loader.pSamples[0].total_frames : 0);
    }

    delay(5);
}
