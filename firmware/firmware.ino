/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware (Pure I2S Mode)
 * Target: ESP32-WROOM-32 / ESP32 DevKit Physical Board
 *
 * System Architecture:
 *   1. 61-Key Matrix Keyboard Scanner (8 Rows x 8 Columns)
 *      - 8 Rows driven by dedicated ESP32 GPIOs (125 us per row, 1000 Hz scan rate)
 *      - 8 Columns read via 74HC165 parallel-in serial-out shift register
 *      - 61 keys: C2 (0.250x, MIDI 36) to C7 (8.000x, MIDI 96)
 *      - Root key: C4 (1.000x, MIDI 60, harmonium base sample)
 *
 *   2. 5-Voice Polyphonic Synthesizer Engine
 *      - 5 independent voices with LRU (Least-Recently-Used) voice stealing
 *      - High-quality linear interpolation on 44.1 kHz 16-bit harmonium sample
 *      - Polyphonic headroom scaling & anti-clipping limiter
 *
 *   3. Wired Audio Output (I2S DMA to PCM5102A DAC)
 *      - High-priority FreeRTOS task running on Core 0
 *      - Standard I2S 16-bit stereo at 44,100 Hz
 *      - Latency: ~2.9 ms DMA buffer
 *
 * Hardware Pin Map:
 *   Matrix Rows (Outputs from ESP32, active HIGH for 125 us):
 *     Row 0: GPIO23 | Row 1: GPIO22 | Row 2: GPIO21 | Row 3: GPIO17
 *     Row 4: GPIO16 | Row 5: GPIO4  | Row 6: GPIO13 | Row 7: GPIO14
 *   74HC165 Columns (Shift Register Inputs):
 *     165 QH:     GPIO34 (Data In)
 *     165 CLK:    GPIO18 (Clock Out)
 *     165 /SH-LD: GPIO19 (Shift/Load Out)
 *   PCM5102A I2S DAC:
 *     DOUT (DIN): GPIO25
 *     BCLK (BCK): GPIO26
 *     LRCLK (WS): GPIO27
 * ============================================================================= */

#include <Arduino.h>
#include <math.h>

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"
#include "vhl_loader.h"
#include "vhl_harmonium_package.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/i2s.h"
#endif

/* =============================================================================
 * Hardware & Audio Configuration
 * ============================================================================= */

/* 61-Key Matrix Keyboard */
#define MATRIX_ROWS          8
#define MATRIX_COLS          8
#define TOTAL_KEYS           61
#define KEY_ROOT_INDEX       24   /* Index 24 = Key 25 = C4 (Root sample) */
#define KEY_BASE_MIDI        36   /* C2 = MIDI Note 36 */

static const uint8_t ROW_PINS[MATRIX_ROWS] = { 23, 22, 21, 17, 16, 4, 13, 14 };

#define HC165_QH_PIN         34
#define HC165_CLK_PIN        18
#define HC165_SHLD_PIN       19

/* PCM5102A I2S DAC Configuration */
#define I2S_PORT_NUM         I2S_NUM_0
#define I2S_DOUT_PIN         25          /* GPIO25 -> PCM5102A DIN  */
#define I2S_BCLK_PIN         26          /* GPIO26 -> PCM5102A BCK  */
#define I2S_LRCLK_PIN        27          /* GPIO27 -> PCM5102A LRCK */
#define I2S_SAMPLE_RATE      44100
#define I2S_DMA_BUF_CNT      8           /* DMA buffer count */
#define I2S_DMA_BUF_LEN      64          /* DMA buffer length (frames) */
#define AUDIO_BUF_FRAMES     64          /* frames generated per write call */
#define MASTER_VOLUME_PERCENT 60         /* Master output volume limit (60%) */

/* Polyphony */
#define MAX_POLYPHONY        5

/* Serial Debug baud rate */
#define DEBUG_SERIAL_BAUD    115200

/* Enable key event logging (set to 0 for silent ultra-fast production mode) */
#define ENABLE_SERIAL_KEY_LOG 1

/* =============================================================================
 * Musical Scale & Key Definitions
 * ============================================================================= */

typedef struct {
    char        name[16];      /* e.g. "C2", "C#4", "C4 (Root)", "C7" */
    float       pitch_ratio;   /* 2^((i - 24) / 12.0f) */
    int8_t      semitones;     /* Relative to C4 (index 24) */
    uint8_t     midi_note;     /* 36 to 96 */
} key_note_t;

static key_note_t g_keyboard[TOTAL_KEYS];

/* =============================================================================
 * Synthesizer Voice Structures
 * ============================================================================= */

typedef struct {
    volatile bool     active;
    int               key_idx;           /* 0 to 60 */
    uint8_t           midi_note;         /* 36 to 96 */
    float             pitch_ratio;       /* 2^((key - 24) / 12.0f) */
    volatile float    sample_phase;      /* Playback position in sample */
    uint32_t          note_on_time_us;   /* Timestamp for LRU voice stealing */
} synth_voice_t;

static synth_voice_t g_voices[MAX_POLYPHONY];
static vhl_loader_t  g_vhl_loader;
static uint64_t      s_last_key_map = 0ULL;

/* Audio Frame Interpolation Helper */
static inline float get_frame(const int16_t* p, uint32_t idx, uint8_t ch) {
    if (ch == 2) return (float)(((int32_t)p[2 * idx] + (int32_t)p[2 * idx + 1]) / 2);
    return (float)p[idx];
}

/* =============================================================================
 * Real-Time I2S Audio Task (Core 0, Priority 5)
 * ============================================================================= */

static void audio_i2s_task(void* arg)
{
    int16_t buf[AUDIO_BUF_FRAMES * 2];

    while (true) {
        for (int i = 0; i < AUDIO_BUF_FRAMES; i++) {
            int32_t mixed = 0;
            int active_cnt = 0;

            if (g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
                const VHLSampleEntry* pS = &g_vhl_loader.pSamples[0];
                const int16_t* pPcm      = (const int16_t*)(g_vhl_loader.pPcmBase + pS->data_offset);
                uint32_t nFrames         = pS->total_frames;
                uint8_t  ch              = pS->channels > 0 ? pS->channels : 1;

                if (pPcm && nFrames > 0) {
                    for (int v = 0; v < MAX_POLYPHONY; v++) {
                        if (g_voices[v].active) {
                            active_cnt++;
                            float phase = g_voices[v].sample_phase;

                            while (phase >= (float)nFrames) {
                                phase -= (float)nFrames;
                            }

                            uint32_t i0   = (uint32_t)phase;
                            uint32_t i1   = (i0 + 1 >= nFrames) ? 0 : i0 + 1;
                            float    frac = phase - (float)i0;

                            float interp = (1.0f - frac) * get_frame(pPcm, i0, ch)
                                         + frac          * get_frame(pPcm, i1, ch);

                            mixed += (int32_t)interp;
                            phase += g_voices[v].pitch_ratio;
                            g_voices[v].sample_phase = phase;
                        }
                    }
                }
            }

            int16_t out_sample = 0;
            if (active_cnt > 0) {
                // Multi-voice headroom scaling to prevent chord clipping
                if (active_cnt == 2) {
                    mixed = (mixed * 4) / 5;
                } else if (active_cnt > 2) {
                    mixed = (mixed * 2) / 3;
                }

                // Apply master volume limit (60%)
                mixed = (mixed * MASTER_VOLUME_PERCENT) / 100;

                // Anti-clipping limiter (16-bit range)
                if (mixed > 32767) mixed = 32767;
                else if (mixed < -32768) mixed = -32768;
                out_sample = (int16_t)mixed;
            }

            buf[i * 2]     = out_sample; /* Left channel */
            buf[i * 2 + 1] = out_sample; /* Right channel (stereo mirror) */
        }

        size_t bytes_written = 0;
        i2s_write(I2S_PORT_NUM, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
    }
}

/* =============================================================================
 * 61-Key Matrix Keyboard Driver (1000 Hz Scan Rate, 125 us per row)
 * ============================================================================= */

/* Latch 8 column inputs into 74HC165 */
static inline void load165(void)
{
    digitalWrite(HC165_SHLD_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(HC165_SHLD_PIN, HIGH);
    delayMicroseconds(1);
}

/* Read 8 column bits from 74HC165 shift register */
static inline uint8_t read165Columns(void)
{
    uint8_t columns = 0;
    digitalWrite(HC165_CLK_PIN, LOW);
    delayMicroseconds(1);

    for (int bit = 7; bit >= 0; bit--) {
        if (digitalRead(HC165_QH_PIN)) {
            columns |= (1 << bit);
        }
        digitalWrite(HC165_CLK_PIN, HIGH);
        delayMicroseconds(1);
        digitalWrite(HC165_CLK_PIN, LOW);
        delayMicroseconds(1);
    }

    return columns;
}

/* Scan all 8 matrix rows at 125 us per row (1000 Hz total matrix scan) */
static uint64_t matrix_scan_keys(void)
{
    uint64_t key_map = 0;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        uint32_t tRowStart = (uint32_t)esp_timer_get_time();

        // 1. Activate row: switch from high-impedance INPUT to OUTPUT HIGH
        pinMode(ROW_PINS[row], OUTPUT);
        digitalWrite(ROW_PINS[row], HIGH);

        // 2. Allow 20 microseconds for contact/trace charging
        delayMicroseconds(20);

        // 3. Latch column inputs into 74HC165
        load165();

        // 4. Return row to high-impedance INPUT (Hi-Z) to prevent cross-row ghosting
        digitalWrite(ROW_PINS[row], LOW);
        pinMode(ROW_PINS[row], INPUT);

        // 5. Read the 8 latched column bits
        uint8_t cols = read165Columns();

        // 6. Map column bits to 61-key bitmap
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if (cols & (1 << col)) {
                uint8_t key_idx = row * MATRIX_COLS + col;
                if (key_idx < TOTAL_KEYS) {
                    key_map |= ((uint64_t)1 << key_idx);
                }
            }
        }

        // 7. Microsecond-precision wait for the remainder of this row's 125 us slot
        while (((uint32_t)esp_timer_get_time() - tRowStart) < 125) {
            // allows complete column discharge
        }
    }

    return key_map;
}

/* =============================================================================
 * Polyphonic Voice Manager
 * ============================================================================= */

/* Find active voice assigned to key_idx, or -1 if none */
static int find_voice_by_key(int key_idx)
{
    for (int v = 0; v < MAX_POLYPHONY; v++) {
        if (g_voices[v].active && g_voices[v].key_idx == key_idx) {
            return v;
        }
    }
    return -1;
}

/* Allocate a voice: idle voice first, then steal oldest active voice (LRU) */
static int allocate_voice(void)
{
    // 1. Check for free voice
    for (int v = 0; v < MAX_POLYPHONY; v++) {
        if (!g_voices[v].active) {
            return v;
        }
    }

    // 2. All 5 voices active: steal oldest (lowest note_on_time_us)
    int oldest_v = 0;
    uint32_t oldest_time = g_voices[0].note_on_time_us;
    for (int v = 1; v < MAX_POLYPHONY; v++) {
        if (g_voices[v].note_on_time_us < oldest_time) {
            oldest_time = g_voices[v].note_on_time_us;
            oldest_v = v;
        }
    }
    return oldest_v;
}

/* Handle Note On */
static void voice_note_on(int key_idx, uint32_t nowUs)
{
    if (key_idx < 0 || key_idx >= (int)TOTAL_KEYS) return;

    int v = find_voice_by_key(key_idx);
    if (v < 0) {
        v = allocate_voice();
    }

    const key_note_t* pNote = &g_keyboard[key_idx];

    g_voices[v].active          = true;
    g_voices[v].key_idx         = key_idx;
    g_voices[v].midi_note       = pNote->midi_note;
    g_voices[v].pitch_ratio     = pNote->pitch_ratio;
    g_voices[v].sample_phase    = 0.0f; /* Retrigger from frame 0 */
    g_voices[v].note_on_time_us = nowUs;

#if ENABLE_SERIAL_KEY_LOG
    int active_cnt = 0;
    for (int i = 0; i < MAX_POLYPHONY; i++) if (g_voices[i].active) active_cnt++;
    Serial.printf("[NOTE ON]  Key %2d [%-10s] | Pitch: %7.4fx | Polyphony: %d/%d\n",
                  key_idx + 1, pNote->name, pNote->pitch_ratio, active_cnt, MAX_POLYPHONY);
#endif
}

/* Handle Note Off */
static void voice_note_off(int key_idx)
{
    int v = find_voice_by_key(key_idx);
    if (v >= 0) {
        g_voices[v].active = false;

#if ENABLE_SERIAL_KEY_LOG
        int active_cnt = 0;
        for (int i = 0; i < MAX_POLYPHONY; i++) if (g_voices[i].active) active_cnt++;
        Serial.printf("[NOTE OFF] Key %2d [%-10s] | Polyphony: %d/%d\n",
                      key_idx + 1, g_keyboard[key_idx].name, active_cnt, MAX_POLYPHONY);
#endif
    }
}

/* Scan matrix and dispatch key state transitions */
static void process_keyboard(void)
{
    uint64_t key_map = matrix_scan_keys();

    if (key_map != s_last_key_map) {
        uint64_t pressed_now  = key_map & ~s_last_key_map;
        uint64_t released_now = s_last_key_map & ~key_map;
        s_last_key_map = key_map;
        uint32_t nowUs = (uint32_t)esp_timer_get_time();

        // 1. Process releases first to free voices
        for (int i = 0; i < (int)TOTAL_KEYS; i++) {
            if (released_now & ((uint64_t)1 << i)) {
                voice_note_off(i);
            }
        }

        // 2. Process newly pressed keys
        for (int i = 0; i < (int)TOTAL_KEYS; i++) {
            if (pressed_now & ((uint64_t)1 << i)) {
                voice_note_on(i, nowUs);
            }
        }
    }
}

/* =============================================================================
 * setup()
 * ============================================================================= */

void setup()
{
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(200);

    Serial.println("\n============================================================");
    Serial.println("  ESP32 Digital Harmonium — Pure I2S Workstation");
    Serial.println("============================================================");

    /* 1. Matrix Row Pins (all set to high-impedance INPUT at boot) */
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        pinMode(ROW_PINS[r], INPUT);
    }

    /* 74HC165 Column Shift Register Pins */
    pinMode(HC165_QH_PIN, INPUT);
    pinMode(HC165_CLK_PIN, OUTPUT);
    pinMode(HC165_SHLD_PIN, OUTPUT);
    digitalWrite(HC165_CLK_PIN, LOW);
    digitalWrite(HC165_SHLD_PIN, HIGH);

    /* 2. Build 61-Key Musical Table (5 Octaves: C2 to C7) */
    static const char* const NOTE_NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    for (int i = 0; i < (int)TOTAL_KEYS; i++) {
        int semitones = i - KEY_ROOT_INDEX;
        int oct       = 2 + (i / 12);
        int note_idx  = i % 12;
        if (i == KEY_ROOT_INDEX) {
            snprintf(g_keyboard[i].name, sizeof(g_keyboard[i].name), "C4 (Root)");
        } else {
            snprintf(g_keyboard[i].name, sizeof(g_keyboard[i].name), "%s%d", NOTE_NAMES[note_idx], oct);
        }
        g_keyboard[i].pitch_ratio = powf(2.0f, (float)semitones / 12.0f);
        g_keyboard[i].semitones   = (int8_t)semitones;
        g_keyboard[i].midi_note   = (uint8_t)(KEY_BASE_MIDI + i);
    }

    /* 3. Initialize Polyphonic Voices */
    for (int v = 0; v < MAX_POLYPHONY; v++) {
        g_voices[v].active          = false;
        g_voices[v].key_idx         = -1;
        g_voices[v].midi_note       = 0;
        g_voices[v].pitch_ratio     = 1.0f;
        g_voices[v].sample_phase    = 0.0f;
        g_voices[v].note_on_time_us = 0;
    }

    /* 4. Load Harmonium VHL Sample Package */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader,
                                    g_vhl_harmonium_package,
                                    g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] Failed to load Harmonium VHL sample package!");
        return;
    }
    Serial.printf("[VHL] Harmonium package loaded (%u bytes)\n", (unsigned int)g_vhl_harmonium_package_size);

    /* 5. Initialize PCM5102A I2S DAC */
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
        Serial.println("[ERROR] I2S Driver installation failed!");
        return;
    }
    i2s_set_pin(I2S_PORT_NUM, &i2s_pins);
    i2s_zero_dma_buffer(I2S_PORT_NUM);

    /* Launch high-priority audio generation task on Core 0 */
    xTaskCreatePinnedToCore(audio_i2s_task, "audio_i2s", 4096, NULL, 5, NULL, 0);

    Serial.println("[I2S] PCM5102A DAC initialized (44.1 kHz, 16-bit stereo)");
    Serial.printf("[AUDIO] Master Volume: %d%% | Polyphony: %d voices\n", MASTER_VOLUME_PERCENT, MAX_POLYPHONY);
    Serial.println("[SYSTEM] Ready! Play keys on the physical keyboard.\n");
}

/* =============================================================================
 * loop() — Core 1 Main Loop
 * ============================================================================= */

void loop()
{
    process_keyboard();
    yield();
}
