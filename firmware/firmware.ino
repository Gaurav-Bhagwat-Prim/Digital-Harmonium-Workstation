/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Mode: Direct C#4 (276 Hz) & +2 Semitone D#4 (311 Hz) Note Switch Engine
 * Note Re-trigger: Resets sample cursor (g_sample_phase = 0.0f) on every key press!
 *
 * Hardware Pin Map:
 *   Switch 1: GPIO32 (INPUT_PULLUP) -> Re-trigger & play C#4 (~276 Hz / 277.18 Hz, MIDI 61)
 *   Switch 2: GPIO33 (INPUT_PULLUP) -> Re-trigger & play D#4 (311.13 Hz, +2 semitones above C#4, MIDI 63)
 *   LED 1:    GPIO18 (OUTPUT) -> HIGH when Switch 1 / C#4 is selected
 *   LED 2:    GPIO19 (OUTPUT) -> HIGH when Switch 2 / D#4 is selected
 *   DAC:      GPIO25 (Internal DAC1 Output for Headphone Jack)
 * ============================================================================= */

#include <Arduino.h>
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
#include <math.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#if __has_include("driver/dac_oneshot.h")
#  include "driver/dac_oneshot.h"
#  define VHL_HAS_ONESHOT_DAC 1
#elif __has_include("driver/dac.h")
#  include "driver/dac.h"
#  define VHL_HAS_LEGACY_DAC 1
#endif
#endif

/* =============================================================================
 * Shared Runtime & Note Pitch Data
 * ============================================================================= */

static vhl_loader_t g_vhl_loader;

/* Pitch constants:
 * Root Key in 000_first try.vhl: C4 (261.63 Hz, MIDI 60)
 * C#4 (277.18 Hz / ~276 Hz): MIDI 61 -> Semitone +1 -> Ratio 2^(1/12) = 1.059463
 * D#4 (311.13 Hz, +2 semitones above C#4): MIDI 63 -> Semitone +3 -> Ratio 2^(3/12) = 1.189207
 */
static const float PITCH_RATIO_CSHARP4 = 1.059463f; /* C#4 (~276 Hz / 277.18 Hz) */
static const float PITCH_RATIO_DSHARP4 = 1.189207f; /* D#4 (311.13 Hz, +2 semitones above C#4) */

static volatile float s_current_pitch_ratio = 1.059463f; /* Defaults to C#4 (~276 Hz) */
static volatile float g_sample_phase = 0.0f;             /* Sub-sample phase accumulator */
static volatile bool g_play_active = true;

#if defined(ESP_PLATFORM) && defined(VHL_HAS_ONESHOT_DAC)
static dac_oneshot_handle_t s_dac_handle = NULL;
#endif

/* Fast DAC Byte Write */
static inline void vhl_hw_write_dac(uint8_t dac_val)
{
#if defined(ESP_PLATFORM) && defined(VHL_HAS_ONESHOT_DAC)
    if (s_dac_handle) {
        dac_oneshot_output_voltage(s_dac_handle, dac_val);
    }
#elif defined(ESP_PLATFORM) && defined(VHL_HAS_LEGACY_DAC)
    dac_output_voltage(DAC_CHANNEL_1, dac_val);
#else
    dacWrite(VHL_PIN_TEST_DAC, dac_val);
#endif
}

static inline uint8_t pcm16_to_dac8(int16_t pcm_sample)
{
    int32_t val = (int32_t)pcm_sample + 32768;
    if (val < 0)     val = 0;
    if (val > 65535) val = 65535;
    return (uint8_t)(val >> 8);
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
            uint32_t pcmBytes = pSmpl->total_frames * sizeof(int16_t);

            Serial.printf("\n[VHL SAMPLE %u]\n", s);
            Serial.printf("  - Data Offset: %u bytes (Absolute Flash Offset: %u)\n",
                          (unsigned int)pSmpl->data_offset,
                          (unsigned int)(pHdr->offset_pcm_data + pSmpl->data_offset));
            Serial.printf("  - Total Frames: %u frames (%0.2f seconds @ %u Hz)\n",
                          (unsigned int)pSmpl->total_frames, durationSec, (unsigned int)pSmpl->sample_rate);
            Serial.printf("  - Loop Start Frame: %u | Loop End Frame: %u\n",
                          (unsigned int)pSmpl->loop_start_frame, (unsigned int)pSmpl->loop_end_frame);
            Serial.printf("  - Root Key: %u (MIDI) | Fine Tune: %d cents | Bit Depth: %u-bit | Channels: %u (Mono)\n",
                          pSmpl->root_key, pSmpl->fine_tune_cents, pSmpl->bit_depth, pSmpl->channels);
            Serial.printf("  - Total PCM Data Size: %u bytes\n", (unsigned int)pcmBytes);
        }
    }

    Serial.println("=============================================================");
}

/* Core 1 Real-Time 44.1 kHz Pitch-Transposed PCM DAC Streaming Task */
static void vhl_audio_task(void* pvParameters)
{
    (void)pvParameters;

    while (1) {
        if (g_play_active && g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
            const VHLSampleEntry* pSmpl = &g_vhl_loader.pSamples[0];
            const int16_t* pPcmData = (const int16_t*)(g_vhl_loader.pPcmBase + pSmpl->data_offset);
            uint32_t totalFrames = pSmpl->total_frames;

            if (pPcmData && totalFrames > 0) {
                float pitchRatio = s_current_pitch_ratio;

                /* Fractional Sub-Sample Interpolation */
                uint32_t idx0 = (uint32_t)g_sample_phase;
                uint32_t idx1 = (idx0 + 1 < totalFrames) ? (idx0 + 1) : 0;
                float frac = g_sample_phase - (float)idx0;

                int16_t s0 = pPcmData[idx0];
                int16_t s1 = pPcmData[idx1];
                float interpolated = (1.0f - frac) * (float)s0 + frac * (float)s1;

                uint8_t dacByte = pcm16_to_dac8((int16_t)interpolated);
                vhl_hw_write_dac(dacByte);

                /* Advance Phase Accumulator by Pitch Ratio */
                g_sample_phase += pitchRatio;
                if (g_sample_phase >= (float)totalFrames) {
                    g_sample_phase -= (float)totalFrames; /* Seamless Loop */
                }

                /* 44.1 kHz Timing (22.67 us per frame) */
                delayMicroseconds(22);
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else {
            vhl_hw_write_dac(128);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* Switch & LED Hardware Scanner with Note Re-Trigger (Reset Sample Position to 0):
 * Switch 1 (GPIO32): Re-triggers & plays C#4 (~276 Hz / 277.18 Hz) from start + LED 1 ON
 * Switch 2 (GPIO33): Re-triggers & plays D#4 (311.13 Hz, +2 semitones above C#4) from start + LED 2 ON
 */
static bool s_sw1_last = false;
static bool s_sw2_last = false;

static void scan_switches_and_update(void)
{
    int raw1 = digitalRead(VHL_PIN_TEST_SWITCH_1);
    int raw2 = digitalRead(VHL_PIN_TEST_SWITCH_2);

    /* Active LOW condition (LOW = Pressed) */
    bool sw1_pressed = (raw1 == LOW);
    bool sw2_pressed = (raw2 == LOW);

    /* Switch 1: Re-trigger C#4 (~276 Hz / 277.18 Hz) from start */
    if (sw1_pressed && !s_sw1_last) {
        digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
        s_current_pitch_ratio = PITCH_RATIO_CSHARP4;
        g_sample_phase = 0.0f; /* RE-TRIGGER: Start sample from frame 0! */
        g_play_active = true;
        Serial.println("\n[SWITCH 1 RE-TRIGGER] C#4 (~276 Hz) Started from Frame 0! | Pitch Ratio: 1.0595x | LED 1 ON | LED 2 OFF");
    } else if (!sw1_pressed && s_sw1_last && !sw2_pressed) {
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    }
    s_sw1_last = sw1_pressed;

    /* Switch 2: Re-trigger D#4 (311.13 Hz, +2 semitones above C#4) from start */
    if (sw2_pressed && !s_sw2_last) {
        digitalWrite(VHL_PIN_TEST_LED_2, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
        s_current_pitch_ratio = PITCH_RATIO_DSHARP4;
        g_sample_phase = 0.0f; /* RE-TRIGGER: Start sample from frame 0! */
        g_play_active = true;
        Serial.println("\n[SWITCH 2 RE-TRIGGER] D#4 (311 Hz, +2 semitones) Started from Frame 0! | Pitch Ratio: 1.1892x | LED 2 ON | LED 1 OFF");
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

    /* Default: LED 1 ON (since default pitch is C#4 ~276 Hz) */
    digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
    digitalWrite(VHL_PIN_TEST_LED_2, LOW);

#if defined(ESP_PLATFORM) && defined(VHL_HAS_ONESHOT_DAC)
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0, /* GPIO25 */
    };
    dac_oneshot_new_channel(&dac_cfg, &s_dac_handle);
#elif defined(ESP_PLATFORM) && defined(VHL_HAS_LEGACY_DAC)
    dac_output_enable(DAC_CHANNEL_1);
#else
    dacWrite(VHL_PIN_TEST_DAC, 128);
#endif
}

/* ESP32 Setup Entry Point */
void setup()
{
    Serial.begin(115200);
    delay(300);

    /* 1. Decompile and parse 000_first try.vhl */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader, g_vhl_harmonium_package, g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader Initialization Failed!");
        return;
    }

    decompile_and_print_vhl(&g_vhl_loader);

    /* 2. Initialize Hardware Pins */
    vhl_hw_init();
    Serial.println("[HARDWARE] Pins Configured: GPIO32 (SW1: C#4 276Hz), GPIO33 (SW2: D#4 311Hz), GPIO18 (LED1), GPIO19 (LED2), GPIO25 (DAC)");
    Serial.println("[AUTOPLAY] Autoplay Active! Default Note: C#4 (~276 Hz / 277.18 Hz). LED 1 ON.");
    Serial.println("[USAGE] Press Switch 1 for C#4 (~276 Hz) from start. Press Switch 2 for D#4 (311 Hz, +2 semitones higher) from start.\n");

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
        const char* noteName = (s_current_pitch_ratio == PITCH_RATIO_CSHARP4) ? "C#4 (~276 Hz)" : "D#4 (311 Hz)";
        Serial.printf("[MONITOR] GPIO32 (SW1)=%d | GPIO33 (SW2)=%d | Active Note=%s | Pitch Ratio=%0.4fx\n",
                      sw1_val, sw2_val, noteName, s_current_pitch_ratio);
    }

    delay(5);
}
