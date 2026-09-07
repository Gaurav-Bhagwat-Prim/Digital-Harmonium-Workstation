/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Features:
 *   1. Full Harmonium audio playback via PCM5102A I²S DAC (44.1 kHz, 16-bit)
 *   2. Real-time pitch control via keys (C4 root / D4 +2 semitones)
 *   3. VHR v2.0 Recording Engine (auto-records first 20 seconds on boot)
 *   4. Automatic Intel HEX serialization & transmission over Serial
 *   5. LCD panel completely disabled (no I2C delays or bus lockups)
 *
 * Hardware Map:
 *   I2S Audio: GPIO25 (DIN), GPIO26 (BCK), GPIO27 (LRCK) -> PCM5102A DAC
 *   Switch 1:  GPIO32 (INPUT_PULLUP) -> Note C4 (1.000x pitch) | LED 1 (GPIO18)
 *   Switch 2:  GPIO33 (INPUT_PULLUP) -> Note D4 (+2 semitones, 1.122x pitch) | LED 2 (GPIO19)
 *   LED 1:     GPIO18 (OUTPUT) -> ON while Switch 1 is active
 *   LED 2:     GPIO19 (OUTPUT) -> ON while Switch 2 is active
 * ============================================================================= */

#include <Arduino.h>
#include <math.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"
#include "vhl_loader.h"
#include "vhl_harmonium_package.h"
#include "vhr_format.h"
#include "recorder_engine.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/i2s.h"
#endif

/* =============================================================================
 * Recording & Audio Configuration
 * ============================================================================= */

#define VHR_DEMO_RECORD_DURATION_MS  (20000U)
#define VHR_DEMO_INSTRUMENT_NAME     "harmonium"
#define VHR_DEMO_TITLE               "Boot Demo Recording"
#define VHR_DEMO_BPM_X10             (800U)      /* 80.0 BPM context metadata */

/* MIDI notes for harmonium keys */
#define VHR_NOTE_ROOT   (60U)   /* SW1: root pitch  (C4, 0 semitones)  */
#define VHR_NOTE_SHIFT  (62U)   /* SW2: root + 2st  (D4, +2 semitones) */
#define VHR_VELOCITY    (100U)

#define VHR_SEMITONE_RATIO   (1.05946309436f)
#define VHR_SHIFT_SEMITONES  (2)

#define VHR_SERIAL_BAUD      (115200)

/* PCM5102A I²S Output Configuration */
#define I2S_PORT_NUM         I2S_NUM_0
#define I2S_DOUT_PIN         25          /* GPIO25 -> PCM5102A DIN  */
#define I2S_BCLK_PIN         26          /* GPIO26 -> PCM5102A BCK  */
#define I2S_LRCLK_PIN        27          /* GPIO27 -> PCM5102A LRCK */
#define I2S_SAMPLE_RATE      44100
#define I2S_DMA_BUF_CNT      8           /* DMA buffer count */
#define I2S_DMA_BUF_LEN      64          /* DMA buffer length (frames) */
#define AUDIO_BUF_FRAMES     64          /* frames generated per write call */

/* Pitch Ratios:
 * C4 (root): 1.000000x
 * D4 (+2 semitones): 2^(2/12) = 1.122462x
 */
#define PITCH_RATIO_C4       (1.000000f)
#define PITCH_RATIO_D4       (1.122462f)

#define VHR_BINARY_MAX_SIZE  (96U + VHR_RECORDER_RING_SIZE * 8U)

/* =============================================================================
 * State Machine & Globals
 * ============================================================================= */

typedef enum {
    REC_IDLE = 0,
    REC_RECORDING,
    REC_DONE,
    REC_SENT
} demo_rec_state_t;

static vhl_loader_t          g_vhl_loader;
static vhl_recorder_engine_t g_recorder;
static uint8_t               g_vhr_binary[VHR_BINARY_MAX_SIZE];
static uint32_t              g_vhr_binary_size = 0;

static demo_rec_state_t      g_rec_state    = REC_IDLE;
static uint32_t              g_rec_start_ms = 0;

/* Audio playback state */
static volatile float        g_sample_phase = 0.0f;
static volatile float        g_active_pitch = PITCH_RATIO_C4;

/* Switch edge tracking */
static bool                  s_sw1_last = false;
static bool                  s_sw2_last = false;
static bool                  s_sw1_note_active = false;
static bool                  s_sw2_note_active = false;

/* Audio Frame Interpolation Helper */
static inline float get_frame(const int16_t* p, uint32_t idx, uint8_t ch) {
    if (ch == 2) return (float)(((int32_t)p[2*idx] + (int32_t)p[2*idx+1]) / 2);
    return (float)p[idx];
}

/* =============================================================================
 * Serial Binary VHR Transmission
 * ============================================================================= */

static void serial_transmit_vhr(const uint8_t* pData, uint32_t size)
{
    Serial.println("\n");
    Serial.println(">>>VHR_BEGIN<<<");
    Serial.printf("# VHR v2.0 binary | size=%u bytes\n", size);

    for (uint32_t i = 0; i < size; i++) {
        Serial.printf("%02X", pData[i]);
        if ((i % 16) == 15 || i == size - 1) {
            Serial.println();
        } else {
            Serial.print(' ');
        }
    }

    Serial.println(">>>VHR_END<<<");
    Serial.printf("# Transmission complete: %u bytes\n\n", size);
}

/* =============================================================================
 * Assemble Final VHR Binary after recording stops
 * ============================================================================= */

static uint32_t assemble_vhr_binary(void)
{
    VHREventEntry drain_buf[64];
    uint32_t drained = 0;
    uint32_t total_written = 0;

    uint8_t* pBase = g_vhr_binary;
    uint32_t event_offset = sizeof(VHRFileHeader);

    while (true) {
        drained = 0;
        vhl_recorder_flush_block(&g_recorder, drain_buf, 64, &drained);
        if (drained == 0) break;

        if (event_offset + drained * sizeof(VHREventEntry) > VHR_BINARY_MAX_SIZE) {
            Serial.println("[VHR] WARNING: binary buffer full, truncating");
            break;
        }

        memcpy(pBase + event_offset, drain_buf, drained * sizeof(VHREventEntry));
        event_offset += drained * sizeof(VHREventEntry);
        total_written += drained;
    }

    VHRFileHeader hdr;
    vhl_recorder_finalize_header(&g_recorder, &hdr);
    memcpy(pBase, &hdr, sizeof(VHRFileHeader));

    return sizeof(VHRFileHeader) + total_written * sizeof(VHREventEntry);
}

/* =============================================================================
 * I²S Audio Task (PCM5102A at 44.1 kHz, 16-bit stereo)
 * ============================================================================= */

static void audio_i2s_task(void* arg)
{
    int16_t buf[AUDIO_BUF_FRAMES * 2];
    bool    first_write = true;

    while (true) {
        for (int i = 0; i < AUDIO_BUF_FRAMES; i++) {
            int16_t sample = 0;

            if (g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
                const VHLSampleEntry* pS = &g_vhl_loader.pSamples[0];
                const int16_t* pPcm      = (const int16_t*)(g_vhl_loader.pPcmBase + pS->data_offset);
                uint32_t nFrames         = pS->total_frames;
                uint8_t  ch              = pS->channels > 0 ? pS->channels : 1;

                if (pPcm && nFrames > 0) {
                    while (g_sample_phase >= (float)nFrames) {
                        g_sample_phase -= (float)nFrames;
                    }

                    uint32_t i0   = (uint32_t)g_sample_phase;
                    uint32_t i1   = (i0 + 1 >= nFrames) ? 0 : i0 + 1;
                    float    frac = g_sample_phase - (float)i0;

                    float interp = (1.0f - frac) * get_frame(pPcm, i0, ch)
                                 + frac           * get_frame(pPcm, i1, ch);
                    sample = (int16_t)interp;
                    g_sample_phase += g_active_pitch;
                }
            }

            buf[i * 2]     = sample; /* Left */
            buf[i * 2 + 1] = sample; /* Right (stereo mono mirror) */
        }

        size_t bytes_written = 0;
        i2s_write(I2S_PORT_NUM, buf, sizeof(buf), &bytes_written, portMAX_DELAY);

        if (first_write && bytes_written > 0) {
            Serial.println(">>> I2S AUDIO STREAM: ACTIVE <<<");
            first_write = false;
        }
    }
}

/* =============================================================================
 * Switch Scanning & Real-Time Pitch Handler
 * ============================================================================= */

static void handle_switch_press(bool sw1_down, bool sw2_down)
{
    uint32_t nowUs = (uint32_t)(esp_timer_get_time());

    /* --- SW1: Root pitch (C4 / MIDI 60) --- */
    if (sw1_down && !s_sw1_last) {
        g_active_pitch = PITCH_RATIO_C4;
        if (g_rec_state == REC_RECORDING) {
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_NOTE_ON, VHR_NOTE_ROOT, VHR_VELOCITY, 0, nowUs);
        }
        s_sw1_note_active = true;
        digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
        Serial.printf(">>> [SW1 PRESSED]  Pitch: C4 (%.4fx) | LED1 ON <<<\n", g_active_pitch);
    } else if (!sw1_down && s_sw1_last && s_sw1_note_active) {
        if (g_rec_state == REC_RECORDING) {
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_NOTE_OFF, VHR_NOTE_ROOT, 0, 0, nowUs);
        }
        s_sw1_note_active = false;
        if (s_sw2_note_active) {
            g_active_pitch = PITCH_RATIO_D4;
        } else {
            g_active_pitch = PITCH_RATIO_C4;
        }
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
        Serial.printf("    [SW1 RELEASED] Pitch: %.4fx | LED1 OFF\n", g_active_pitch);
    }

    /* --- SW2: +2 semitones (D4 / MIDI 62) --- */
    if (sw2_down && !s_sw2_last) {
        g_active_pitch = PITCH_RATIO_D4;
        if (g_rec_state == REC_RECORDING) {
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_NOTE_ON, VHR_NOTE_SHIFT, VHR_VELOCITY, 0, nowUs);
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_TRANSPOSE_CHANGE,
                (uint8_t)(int8_t)VHR_SHIFT_SEMITONES, 0, 0, nowUs);
        }
        s_sw2_note_active = true;
        digitalWrite(VHL_PIN_TEST_LED_2, HIGH);
        Serial.printf(">>> [SW2 PRESSED]  Pitch: D4 (+2st, %.4fx) | LED2 ON <<<\n", g_active_pitch);
    } else if (!sw2_down && s_sw2_last && s_sw2_note_active) {
        if (g_rec_state == REC_RECORDING) {
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_NOTE_OFF, VHR_NOTE_SHIFT, 0, 0, nowUs);
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_TRANSPOSE_CHANGE, 0, 0, 0, nowUs);
        }
        s_sw2_note_active = false;
        /* Revert back to C4 on release */
        g_active_pitch = PITCH_RATIO_C4;
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
        Serial.printf("    [SW2 RELEASED] Pitch: Revert C4 (%.4fx) | LED2 OFF\n", g_active_pitch);
    }
}

/* =============================================================================
 * setup()
 * ============================================================================= */

void setup()
{
    Serial.begin(VHR_SERIAL_BAUD);
    delay(300);

    Serial.println("\n============================================================");
    Serial.println(" ESP32 Digital Harmonium Workstation — VHR v2.0 (No LCD)");
    Serial.println("============================================================");

    /* 1. Hardware Pins */
    pinMode(VHL_PIN_TEST_SWITCH_1, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_SWITCH_2, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_LED_1, OUTPUT);
    pinMode(VHL_PIN_TEST_LED_2, OUTPUT);
    digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    digitalWrite(VHL_PIN_TEST_LED_2, LOW);

    /* 2. Load Harmonium VHL Package */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader,
                                     g_vhl_harmonium_package,
                                     g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader failed!");
        return;
    }
    Serial.printf("[VHL] Package loaded: %u bytes\n", (unsigned int)g_vhl_harmonium_package_size);

    g_active_pitch = PITCH_RATIO_C4;
    g_sample_phase = 0.0f;

    /* 3. Arm and Start VHR Performance Recorder */
    vhl_recorder_arm(&g_recorder,
                     VHR_DEMO_INSTRUMENT_NAME,
                     VHR_DEMO_TITLE,
                     VHR_DEMO_BPM_X10,
                     0,      /* transpose = 0 */
                     200U);  /* firmwareVersion = 200 (v2.0.0) */

    vhl_recorder_start(&g_recorder,
                       VHR_DEMO_INSTRUMENT_NAME,
                       VHR_DEMO_TITLE,
                       VHR_DEMO_BPM_X10,
                       0, 200U);

    g_rec_start_ms = millis();
    g_rec_state    = REC_RECORDING;

    Serial.println("[VHR] Performance recording STARTED (20 seconds).");
    Serial.println("  SW1 (GPIO32) -> Note C4 (1.000x pitch) | LED1 (GPIO18)");
    Serial.println("  SW2 (GPIO33) -> Note D4 (1.122x pitch) | LED2 (GPIO19)");
    Serial.println("  Hold SW1 + tap SW2 to chord both pitches!");
    Serial.println("  [LCD is disabled — no I2C delays]\n");

    /* 4. Initialize I2S for PCM5102A */
    Serial.println("--- PCM5102A I2S CONFIG ---");
    Serial.printf("Sample rate : %d Hz\n", I2S_SAMPLE_RATE);
    Serial.println("Format      : Standard I2S, 16-bit stereo");
    Serial.printf("DOUT        : GPIO%d\n", I2S_DOUT_PIN);
    Serial.printf("BCLK        : GPIO%d\n", I2S_BCLK_PIN);
    Serial.printf("LRCK        : GPIO%d\n", I2S_LRCLK_PIN);
    {
        const i2s_config_t i2s_cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = I2S_SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = I2S_DMA_BUF_CNT,
            .dma_buf_len          = I2S_DMA_BUF_LEN,
            .use_apll             = false,
            .tx_desc_auto_clear   = true,
            .fixed_mclk           = 0,
        };
        const i2s_pin_config_t i2s_pins = {
            .mck_io_num   = I2S_PIN_NO_CHANGE,
            .bck_io_num   = I2S_BCLK_PIN,
            .ws_io_num    = I2S_LRCLK_PIN,
            .data_out_num = I2S_DOUT_PIN,
            .data_in_num  = I2S_PIN_NO_CHANGE,
        };

        esp_err_t ret = i2s_driver_install(I2S_PORT_NUM, &i2s_cfg, 0, NULL);
        if (ret != ESP_OK) {
            Serial.println("[ERROR] I2S init failed!");
            return;
        }
        i2s_set_pin(I2S_PORT_NUM, &i2s_pins);
        i2s_zero_dma_buffer(I2S_PORT_NUM);
        Serial.println("I2S Driver  : OK\n");

        /* Start audio generation task on Core 0 */
        xTaskCreatePinnedToCore(audio_i2s_task, "audio_i2s", 4096, NULL, 5, NULL, 0);
    }
}

/* =============================================================================
 * loop()
 * ============================================================================= */

static uint32_t s_last_sec_log = 0;

void loop()
{
    /* Read switches (INPUT_PULLUP: LOW = pressed, HIGH = open) */
    bool sw1 = (digitalRead(VHL_PIN_TEST_SWITCH_1) == LOW);
    bool sw2 = (digitalRead(VHL_PIN_TEST_SWITCH_2) == LOW);

    /* Switch handling is always active for live harmonium playing */
    handle_switch_press(sw1, sw2);

    /* --- Phase 1: RECORDING --- */
    if (g_rec_state == REC_RECORDING) {
        uint32_t elapsed_ms  = millis() - g_rec_start_ms;
        uint32_t remaining_s = (VHR_DEMO_RECORD_DURATION_MS > elapsed_ms)
                             ? (VHR_DEMO_RECORD_DURATION_MS - elapsed_ms) / 1000
                             : 0;

        /* Print countdown to Serial once per second */
        if (remaining_s != s_last_sec_log) {
            s_last_sec_log = remaining_s;
            Serial.printf("[REC PROGRESS] %u seconds remaining | Pitch: %.3fx\n", remaining_s, g_active_pitch);
        }

        /* --- 20-Second Recording Finished --- */
        if (elapsed_ms >= VHR_DEMO_RECORD_DURATION_MS) {
            uint32_t nowUs = (uint32_t)esp_timer_get_time();
            if (s_sw1_note_active) {
                vhl_recorder_push_event(&g_recorder,
                    VHR_EVENT_NOTE_OFF, VHR_NOTE_ROOT, 0, 0, nowUs);
            }
            if (s_sw2_note_active) {
                vhl_recorder_push_event(&g_recorder,
                    VHR_EVENT_NOTE_OFF, VHR_NOTE_SHIFT, 0, 0, nowUs);
            }

            vhl_recorder_stop(&g_recorder);
            g_vhr_binary_size = assemble_vhr_binary();

            uint32_t total_evts = 0, dropped = 0;
            vhl_recorder_get_stats(&g_recorder, &total_evts, &dropped);

            g_rec_state = REC_DONE;

            Serial.println("\n============================================================");
            Serial.println("[VHR] RECORDING COMPLETE!");
            Serial.printf("[VHR] Events: %u recorded, %u dropped\n", total_evts, dropped);
            Serial.printf("[VHR] Binary Size: %u bytes\n", g_vhr_binary_size);
            Serial.println("[VHR] Transmitting Intel HEX over Serial for vhr_receiver.py...");
            Serial.println("============================================================");

            serial_transmit_vhr(g_vhr_binary, g_vhr_binary_size);
            g_rec_state = REC_SENT;

            Serial.println("============================================================");
            Serial.printf("[VHR] Transmission finished. %u events saved.\n", total_evts);
            Serial.println("[VHR] Harmonium keys remain fully active for live playing!");
            Serial.println("============================================================\n");
        }
    }

    s_sw1_last = sw1;
    s_sw2_last = sw2;

    delay(5);
}
