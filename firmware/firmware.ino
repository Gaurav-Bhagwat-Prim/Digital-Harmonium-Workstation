/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Features:
 *   1. 61-Key Matrix Keyboard Scanner (8 Rows x 8 Columns)
 *      - 8 Rows driven by dedicated ESP32 GPIO outputs (125 us per row, 1000 Hz)
 *      - 8 Columns read via 74HC165 parallel-in serial-out shift register
 *      - 61 keys: C2 (0.250x, MIDI 36) to C7 (8.000x, MIDI 96)
 *      - Root key: C4 (1.000x, MIDI 60, harmonium base sample)
 *   2. 5-Voice Polyphonic Synthesizer Engine (simultaneous multi-key chords)
 *      - Up to 5 independent voices with LRU voice stealing
 *      - Independent sample phase interpolation for wired I²S and Bluetooth
 *      - Headroom scaling and anti-clipping limiter for crystal clear chord mixing
 *   3. Full Harmonium audio playback via PCM5102A I²S DAC (44.1 kHz, 16-bit)
 *   4. Wireless Bluetooth A2DP Source streaming to "Airdopes 219" headphones
 *   5. Dual Audio Output: Wired Speaker + Wireless Headphones simultaneously
 *   6. VHR v2.0 Recording Engine (auto-records first 20 seconds on boot)
 *   7. Automatic Intel HEX serialization & transmission over Serial
 *   8. Memory usage monitor with MALLOC_CAP diagnostics
 *
 * Hardware Map:
 *   Matrix Rows (Dedicated GPIO Outputs, active HIGH for 125 us):
 *     Row 0: GPIO23 | Row 1: GPIO22 | Row 2: GPIO21 | Row 3: GPIO17
 *     Row 4: GPIO16 | Row 5: GPIO4  | Row 6: GPIO13 | Row 7: GPIO14
 *   74HC165 Columns (Shift Register Inputs):
 *     165 QH:     GPIO34 (INPUT)
 *     165 CLK:    GPIO18 (OUTPUT)
 *     165 /SH-LD: GPIO19 (OUTPUT)
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
#include "vhr_format.h"
#include "recorder_engine.h"

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#endif

#include "BluetoothA2DPSource.h"

/* =============================================================================
 * 61-Key Matrix Keyboard Pin Configuration
 *
 * 8 Rows (Outputs from ESP32, driven HIGH one row at a time for 125 us):
 *   Row 0 (FI0/SI0): GPIO23
 *   Row 1 (FI1/SI1): GPIO22
 *   Row 2 (FI2/SI2): GPIO21
 *   Row 3 (FI3/SI3): GPIO17
 *   Row 4 (FI4/SI4): GPIO16
 *   Row 5 (FI5/SI5): GPIO4
 *   Row 6 (FI6/SI6): GPIO13
 *   Row 7 (FI7/SI7): GPIO14
 *
 * 8 Columns (Inputs to ESP32 via 74HC165 Shift Register):
 *   165 QH:     GPIO34 (Data In)
 *   165 CLK:    GPIO18 (Clock Out)
 *   165 /SH-LD: GPIO19 (Shift/Load Out)
 * ============================================================================= */
#define MATRIX_ROWS          8
#define MATRIX_COLS          8
#define TOTAL_KEYS           61
#define KEY_ROOT_INDEX       24   /* Index 24 = Key 25 = C4 (Root sample) */
#define KEY_BASE_MIDI        36   /* C2 = MIDI Note 36 */

static const uint8_t ROW_PINS[MATRIX_ROWS] = { 23, 22, 21, 17, 16, 4, 13, 14 };

#define HC165_QH_PIN         34
#define HC165_CLK_PIN        18
#define HC165_SHLD_PIN       19

#define ROW_ACTIVE_LEVEL     HIGH
#define ROW_INACTIVE_LEVEL   LOW

/* =============================================================================
 * 61-Key Musical Structure (5 Octaves: C2 to C7)
 * ============================================================================= */
typedef struct {
    char        name[16];      /* e.g. "C2", "C#4", "C4 (Root)", "C7" */
    float       pitch_ratio;   /* 2^((i - 24) / 12.0f) */
    int8_t      semitones;     /* Relative to C4 (index 24) */
    uint8_t     midi_note;     /* 36 to 96 */
} key_note_t;

static key_note_t g_keyboard[TOTAL_KEYS];

/* =============================================================================
 * Recording & Audio Configuration
 * ============================================================================= */

#define VHR_DEMO_RECORD_DURATION_MS  (20000U)
#define VHR_DEMO_INSTRUMENT_NAME     "harmonium"
#define VHR_DEMO_TITLE               "Boot Demo Recording"
#define VHR_DEMO_BPM_X10             (800U)      /* 80.0 BPM context metadata */
#define VHR_VELOCITY                 (100U)
#define VHR_SERIAL_BAUD              (115200)

/* Dynamic Bluetooth Target Device Tracking */
static char s_bt_connected_name[64] = "Any Audio Device";

/* PCM5102A I²S Output Configuration */
#define I2S_PORT_NUM         I2S_NUM_0
#define I2S_DOUT_PIN         25          /* GPIO25 -> PCM5102A DIN  */
#define I2S_BCLK_PIN         26          /* GPIO26 -> PCM5102A BCK  */
#define I2S_LRCLK_PIN        27          /* GPIO27 -> PCM5102A LRCK */
#define I2S_SAMPLE_RATE      44100
#define I2S_DMA_BUF_CNT      8           /* DMA buffer count */
#define I2S_DMA_BUF_LEN      64          /* DMA buffer length (frames) */
#define AUDIO_BUF_FRAMES     64          /* frames generated per write call */

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

/* =============================================================================
 * 5-Voice Polyphony Structure & Voice Pool
 * ============================================================================= */
#define MAX_POLYPHONY 5

typedef struct {
    volatile bool     active;
    int               key_idx;           /* 0 to 60 */
    uint8_t           midi_note;         /* 36 to 96 */
    float             pitch_ratio;       /* 2^((key - 24) / 12.0f) */
    volatile float    sample_phase;      /* Playback position for wired PCM5102A */
    volatile float    bt_sample_phase;   /* Playback position for Bluetooth A2DP */
    uint32_t          note_on_time_us;   /* Timestamp for voice stealing (LRU) */
} synth_voice_t;

static synth_voice_t         g_voices[MAX_POLYPHONY];

/* Bluetooth A2DP Source Instance */
static BluetoothA2DPSource   s_a2dp_source;

/* 64-bit keyboard matrix state tracking */
static uint64_t              s_last_key_map    = 0ULL;

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
 * Bluetooth A2DP Audio Callback (Streams to Airdopes 219)
 * Feeds 44.1 kHz 16-bit stereo PCM frames to the Bluetooth SBC encoder
 * ============================================================================= */

/* =============================================================================
 * Bluetooth A2DP Audio Callback (Streams to Airdopes 219)
 * 5-Voice Polyphonic Mixer: Feeds 44.1 kHz 16-bit stereo PCM frames to SBC encoder
 * ============================================================================= */

static int32_t bt_audio_data_cb(Frame* data, int32_t frame_count)
{
    if (!g_vhl_loader.isLoaded || !g_vhl_loader.pPcmBase) {
        for (int32_t i = 0; i < frame_count; i++) {
            data[i].channel1 = 0;
            data[i].channel2 = 0;
        }
        return frame_count;
    }

    const VHLSampleEntry* pS = &g_vhl_loader.pSamples[0];
    const int16_t* pPcm      = (const int16_t*)(g_vhl_loader.pPcmBase + pS->data_offset);
    uint32_t nFrames         = pS->total_frames;
    uint8_t  ch              = pS->channels > 0 ? pS->channels : 1;

    for (int32_t i = 0; i < frame_count; i++) {
        int32_t mixed = 0;
        int active_cnt = 0;

        if (pPcm && nFrames > 0) {
            for (int v = 0; v < MAX_POLYPHONY; v++) {
                if (g_voices[v].active) {
                    active_cnt++;
                    float phase = g_voices[v].bt_sample_phase;

                    while (phase >= (float)nFrames) {
                        phase -= (float)nFrames;
                    }

                    uint32_t i0   = (uint32_t)phase;
                    uint32_t i1   = (i0 + 1 >= nFrames) ? 0 : i0 + 1;
                    float    frac = phase - (float)i0;

                    float interp  = (1.0f - frac) * get_frame(pPcm, i0, ch)
                                  + frac          * get_frame(pPcm, i1, ch);

                    mixed += (int32_t)interp;
                    phase += g_voices[v].pitch_ratio;
                    g_voices[v].bt_sample_phase = phase;
                }
            }
        }

        int16_t out_sample = 0;
        if (active_cnt > 0) {
            // Headroom scaling to prevent multi-voice clipping
            if (active_cnt == 2) {
                mixed = (mixed * 4) / 5;
            } else if (active_cnt > 2) {
                mixed = (mixed * 2) / 3;
            }

            // Hard clamp to 16-bit range
            if (mixed > 32767) mixed = 32767;
            else if (mixed < -32768) mixed = -32768;
            out_sample = (int16_t)mixed;
        }

        data[i].channel1 = out_sample;
        data[i].channel2 = out_sample;
    }

    return frame_count;
}

/* Bluetooth Discovery Callback: Accept ANY audio device in pairing mode */
static bool bt_ssid_callback(const char* ssid, esp_bd_addr_t address, int rssi)
{
    if (ssid != NULL && strlen(ssid) > 0) {
        strncpy(s_bt_connected_name, ssid, sizeof(s_bt_connected_name) - 1);
        s_bt_connected_name[sizeof(s_bt_connected_name) - 1] = '\0';
    } else {
        snprintf(s_bt_connected_name, sizeof(s_bt_connected_name),
                 "[%02X:%02X:%02X:%02X:%02X:%02X]",
                 address[0], address[1], address[2], address[3], address[4], address[5]);
    }
    Serial.printf("\n[BLUETOOTH] Discovered active audio device: '%s' (RSSI: %d dBm)\n",
                  s_bt_connected_name, rssi);
    Serial.println("[BLUETOOTH] Auto-connecting now...");
    return true; /* Return true to connect to this device */
}

/* Bluetooth Connection State Observer */
static void bt_connection_state_cb(esp_a2d_connection_state_t state, void* obj)
{
    (void)obj;
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            Serial.println("\n[BLUETOOTH] Disconnected from audio device.");
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.printf("\n[BLUETOOTH] Connecting to '%s'...\n", s_bt_connected_name);
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            Serial.println("\n============================================================");
            Serial.printf(">>> [BLUETOOTH] CONNECTED TO: %s! <<<\n", s_bt_connected_name);
            Serial.println(">>> Wireless 44.1 kHz 16-bit harmonium streaming ACTIVE! <<<");
            Serial.println("============================================================\n");
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            Serial.println("\n[BLUETOOTH] Disconnecting...");
            break;
        default:
            break;
    }
}

/* =============================================================================
 * I²S Audio Task (PCM5102A at 44.1 kHz, 16-bit stereo)
 * 5-Voice Polyphonic Real-Time Mixer
 * ============================================================================= */

static void audio_i2s_task(void* arg)
{
    int16_t buf[AUDIO_BUF_FRAMES * 2];
    bool    first_write = true;

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
                                         + frac           * get_frame(pPcm, i1, ch);

                            mixed += (int32_t)interp;
                            phase += g_voices[v].pitch_ratio;
                            g_voices[v].sample_phase = phase;
                        }
                    }
                }
            }

            int16_t out_sample = 0;
            if (active_cnt > 0) {
                // Headroom scaling to prevent multi-voice clipping
                if (active_cnt == 2) {
                    mixed = (mixed * 4) / 5;
                } else if (active_cnt > 2) {
                    mixed = (mixed * 2) / 3;
                }

                // Hard clamp to 16-bit signed range
                if (mixed > 32767) mixed = 32767;
                else if (mixed < -32768) mixed = -32768;
                out_sample = (int16_t)mixed;
            }

            buf[i * 2]     = out_sample; /* Left */
            buf[i * 2 + 1] = out_sample; /* Right (stereo mirror) */
        }

        size_t bytes_written = 0;
        i2s_write(I2S_PORT_NUM, buf, sizeof(buf), &bytes_written, portMAX_DELAY);

        if (first_write && bytes_written > 0) {
            Serial.println(">>> WIRED I2S AUDIO STREAM: ACTIVE (PCM5102A) <<<");
            first_write = false;
        }
    }
}

/* =============================================================================
 * 61-Key Matrix Keyboard Driver (1000 Hz Matrix Scan, 125 us per row)
 *
 * 8 Rows driven by dedicated ESP32 GPIOs (FI0-FI7 / SI0-SI7 combined):
 *   Row 0: GPIO23, Row 1: GPIO22, Row 2: GPIO21, Row 3: GPIO17,
 *   Row 4: GPIO16, Row 5: GPIO4,  Row 6: GPIO13, Row 7: GPIO14
 *
 * 8 Columns read via 74HC165 shift register:
 *   QH: GPIO34, CLK: GPIO18, /SH-LD: GPIO19
 *
 * Each row is activated HIGH for 125 microseconds.
 * Full 8-row scan = 8 * 125 us = 1000 us = 1 ms (1000 scans per second).
 * ============================================================================= */

/* Capture 8 column inputs into 74HC165 parallel latches */
static inline void load165(void)
{
    // /SH-LD LOW = parallel load
    digitalWrite(HC165_SHLD_PIN, LOW);
    delayMicroseconds(2);

    // /SH-LD HIGH = shift mode
    digitalWrite(HC165_SHLD_PIN, HIGH);
    delayMicroseconds(1);
}

/* Read 8 column inputs from 74HC165 (KC0 to KC7) */
static inline uint8_t read165Columns(void)
{
    uint8_t columns = 0;
    digitalWrite(HC165_CLK_PIN, LOW);
    delayMicroseconds(1);

    for (int bit = 7; bit >= 0; bit--)
    {
        // Read current QH BEFORE clocking
        if (digitalRead(HC165_QH_PIN))
        {
            columns |= (1 << bit);
        }

        // Shift next bit toward QH with clean 1us clock pulse
        digitalWrite(HC165_CLK_PIN, HIGH);
        delayMicroseconds(1);
        digitalWrite(HC165_CLK_PIN, LOW);
        delayMicroseconds(1);
    }

    return columns;
}

/* Scan all 8 rows. Each row has a 125 microsecond time slot (1000 Hz total matrix scan).
 * Inactive rows remain in high-impedance INPUT (Hi-Z) mode to prevent cross-row leakage.
 * Active row is temporarily switched to OUTPUT HIGH for 25 us, latched, then returned to INPUT. */
static uint64_t matrix_scan_keys(void)
{
    uint64_t key_map = 0;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++)
    {
        uint32_t tRowStart = (uint32_t)esp_timer_get_time();

        // 1. Switch this row from high-impedance INPUT to OUTPUT HIGH
        pinMode(ROW_PINS[row], OUTPUT);
        digitalWrite(ROW_PINS[row], HIGH);

        // 2. Allow 20 microseconds for contact and trace charging
        delayMicroseconds(20);

        // 3. Latch column inputs into 74HC165
        load165();

        // 4. Return row to high-impedance INPUT (Hi-Z) so it completely disconnects
        digitalWrite(ROW_PINS[row], LOW);
        pinMode(ROW_PINS[row], INPUT);

        // 5. Read the 8 latched column bits from 74HC165
        uint8_t cols = read165Columns();

        // 6. Map column bits to key bitmap
        for (uint8_t col = 0; col < MATRIX_COLS; col++)
        {
            if (cols & (1 << col))
            {
                uint8_t key_idx = row * MATRIX_COLS + col;
                if (key_idx < TOTAL_KEYS)
                {
                    key_map |= ((uint64_t)1 << key_idx);
                }
            }
        }

        // 7. Wait out the remaining time of this row's 125 us slot
        while (((uint32_t)esp_timer_get_time() - tRowStart) < 125)
        {
            // microsecond precision wait (allows complete column line discharge)
        }
    }

    return key_map;
}

/* =============================================================================
 * 5-Voice Polyphonic Voice Manager & Keyboard Processor
 * ============================================================================= */

/* Find which voice is currently assigned to key_idx, or -1 if none */
static int find_voice_by_key(int key_idx)
{
    for (int v = 0; v < MAX_POLYPHONY; v++) {
        if (g_voices[v].active && g_voices[v].key_idx == key_idx) {
            return v;
        }
    }
    return -1;
}

/* Allocate a voice: first look for idle voice, then steal oldest (LRU) */
static int allocate_voice(void)
{
    // 1. Look for free voice
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

/* Handle Note On event for a key */
static void voice_note_on(int key_idx, uint32_t nowUs)
{
    if (key_idx < 0 || key_idx >= (int)TOTAL_KEYS) return;

    int v = find_voice_by_key(key_idx);
    if (v < 0) {
        v = allocate_voice();
    }

    const key_note_t* pNote = &g_keyboard[key_idx];

    // If stealing an active voice during recording, push NOTE_OFF for old note
    if (g_voices[v].active && g_rec_state == REC_RECORDING) {
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_OFF, g_voices[v].midi_note, 0, 0, nowUs);
    }

    g_voices[v].active          = true;
    g_voices[v].key_idx         = key_idx;
    g_voices[v].midi_note       = pNote->midi_note;
    g_voices[v].pitch_ratio     = pNote->pitch_ratio;
    g_voices[v].sample_phase    = 0.0f; /* Retrigger from frame 0 */
    g_voices[v].bt_sample_phase = 0.0f;
    g_voices[v].note_on_time_us = nowUs;

    // VHR performance recorder integration
    if (g_rec_state == REC_RECORDING) {
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_ON, pNote->midi_note, VHR_VELOCITY, 0, nowUs);
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_TRANSPOSE_CHANGE, (uint8_t)(int8_t)pNote->semitones, 0, 0, nowUs);
    }

    int active_cnt = 0;
    for (int i = 0; i < MAX_POLYPHONY; i++) if (g_voices[i].active) active_cnt++;

    uint8_t r = (uint8_t)(key_idx / MATRIX_COLS);
    uint8_t c = (uint8_t)(key_idx % MATRIX_COLS);
    Serial.printf("[NOTE ON]  Voice %d | Row %u (GPIO%2d), Col %u (KC%u) -> Key %2d [%-10s] | Pitch: %7.4fx | Polyphony: %d/%d\n",
                  v + 1, r, ROW_PINS[r], c, c, key_idx + 1, pNote->name, pNote->pitch_ratio, active_cnt, MAX_POLYPHONY);
}

/* Handle Note Off event for a key */
static void voice_note_off(int key_idx, uint32_t nowUs)
{
    int v = find_voice_by_key(key_idx);
    if (v >= 0) {
        g_voices[v].active = false;

        if (g_rec_state == REC_RECORDING) {
            vhl_recorder_push_event(&g_recorder,
                VHR_EVENT_NOTE_OFF, g_voices[v].midi_note, 0, 0, nowUs);
        }

        int active_cnt = 0;
        for (int i = 0; i < MAX_POLYPHONY; i++) if (g_voices[i].active) active_cnt++;

        Serial.printf("[NOTE OFF] Voice %d | Key %2d [%-10s] | Polyphony: %d/%d\n",
                      v + 1, key_idx + 1, g_keyboard[key_idx].name, active_cnt, MAX_POLYPHONY);
    }
}

/* Process keyboard matrix and route transitions to polyphonic voices */
static void process_keyboard(void)
{
    uint64_t key_map = matrix_scan_keys();

    /* Detect bitwise transitions across all 61 keys */
    if (key_map != s_last_key_map) {
        uint64_t pressed_now  = key_map & ~s_last_key_map;
        uint64_t released_now = s_last_key_map & ~key_map;
        s_last_key_map = key_map;
        uint32_t nowUs = (uint32_t)esp_timer_get_time();

        /* Optional diagnostic print if keys are held */
        if (key_map != 0ULL) {
            Serial.print("[MATRIX RAW] ");
            for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
                uint8_t row_bits = (uint8_t)((key_map >> (r * MATRIX_COLS)) & 0xFF);
                if (row_bits != 0) {
                    Serial.printf("R%u=0x%02X ", r, row_bits);
                }
            }
            Serial.println();
        }

        // 1. Process all newly released keys first
        for (int i = 0; i < (int)TOTAL_KEYS; i++) {
            if (released_now & ((uint64_t)1 << i)) {
                voice_note_off(i, nowUs);
            }
        }

        // 2. Process all newly pressed keys
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
    Serial.begin(VHR_SERIAL_BAUD);
    delay(300);

    Serial.println("\n============================================================");
    Serial.println(" ESP32 Digital Harmonium — 61-Key Matrix Workstation (1000 Hz)");
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

    /* Build 61-Key Table (5 Octaves: C2 to C7) */
    static const char* const NOTE_NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    for (int i = 0; i < (int)TOTAL_KEYS; i++) {
        int semitones = i - KEY_ROOT_INDEX;
        int oct = 2 + (i / 12);
        int note_idx = i % 12;
        if (i == KEY_ROOT_INDEX) {
            snprintf(g_keyboard[i].name, sizeof(g_keyboard[i].name), "C4 (Root)");
        } else {
            snprintf(g_keyboard[i].name, sizeof(g_keyboard[i].name), "%s%d", NOTE_NAMES[note_idx], oct);
        }
        g_keyboard[i].pitch_ratio = powf(2.0f, (float)semitones / 12.0f);
        g_keyboard[i].semitones   = (int8_t)semitones;
        g_keyboard[i].midi_note   = (uint8_t)(KEY_BASE_MIDI + i);
    }

    Serial.println("--- 61-KEY MATRIX KEYBOARD CONFIG ---");
    Serial.printf("Total Keys    : %u (5 octaves: C2 to C7)\n", TOTAL_KEYS);
    Serial.printf("Matrix Rate   : 1000 Hz (125 us per row, 8 rows)\n");
    Serial.printf("Row Pins      : R0=%d, R1=%d, R2=%d, R3=%d, R4=%d, R5=%d, R6=%d, R7=%d\n",
                  ROW_PINS[0], ROW_PINS[1], ROW_PINS[2], ROW_PINS[3],
                  ROW_PINS[4], ROW_PINS[5], ROW_PINS[6], ROW_PINS[7]);
    Serial.printf("165 Col Pins  : QH=GPIO%d, CLK=GPIO%d, /SH-LD=GPIO%d\n",
                  HC165_QH_PIN, HC165_CLK_PIN, HC165_SHLD_PIN);
    Serial.printf("Root Key      : Key %d [%s] -> 1.0000x pitch (MIDI %u)\n",
                  KEY_ROOT_INDEX + 1, g_keyboard[KEY_ROOT_INDEX].name, g_keyboard[KEY_ROOT_INDEX].midi_note);
    Serial.printf("First Key     : Key 1  [%s] -> %.4fx pitch (MIDI %u, %d st)\n",
                  g_keyboard[0].name, g_keyboard[0].pitch_ratio, g_keyboard[0].midi_note, g_keyboard[0].semitones);
    Serial.printf("Last Key      : Key 61 [%s] -> %.4fx pitch (MIDI %u, %+d st)\n",
                  g_keyboard[TOTAL_KEYS - 1].name, g_keyboard[TOTAL_KEYS - 1].pitch_ratio,
                  g_keyboard[TOTAL_KEYS - 1].midi_note, g_keyboard[TOTAL_KEYS - 1].semitones);
    Serial.println("-------------------------------------\n");

    /* 2. Load Harmonium VHL Package */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader,
                                     g_vhl_harmonium_package,
                                     g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader failed!");
        return;
    }
    Serial.printf("[VHL] Package loaded: %u bytes\n", (unsigned int)g_vhl_harmonium_package_size);

    /* Initialize 5 polyphonic voices */
    for (int v = 0; v < MAX_POLYPHONY; v++) {
        g_voices[v].active          = false;
        g_voices[v].key_idx         = -1;
        g_voices[v].midi_note       = 0;
        g_voices[v].pitch_ratio     = 1.0f;
        g_voices[v].sample_phase    = 0.0f;
        g_voices[v].bt_sample_phase = 0.0f;
        g_voices[v].note_on_time_us = 0;
    }

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

    /* 4. Initialize I2S for PCM5102A (Wired Speaker Output) */
    Serial.println("--- PCM5102A I2S CONFIG (Wired Output) ---");
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

    /* 5. Initialize Bluetooth A2DP Source (Wireless Audio Output) */
    Serial.println("--- BLUETOOTH A2DP SOURCE (Wireless Output) ---");
    Serial.println("Target Device : Any active Bluetooth audio device (headphones, speaker, etc.)");
    Serial.println("Mode          : A2DP Source (Transmitter)");
    Serial.println("Sample rate   : 44100 Hz, 16-bit stereo SBC");
    Serial.println("Instruction   : Put your headphones/speaker in Pairing Mode!");

    s_a2dp_source.set_ssp_enabled(true);
    s_a2dp_source.set_auto_reconnect(true);
    s_a2dp_source.set_on_connection_state_changed(bt_connection_state_cb);
    s_a2dp_source.set_ssid_callback(bt_ssid_callback);
    s_a2dp_source.start(bt_audio_data_cb);

    Serial.println("Bluetooth A2DP Source initialized successfully (Auto-Discovery Active).\n");
}

/* =============================================================================
 * loop()
 * ============================================================================= */

static uint32_t s_last_sec_log = 0;
static uint32_t s_last_bt_status_ms = 0;

void loop()
{
    /* Scan 74HC165 keyboard and update active swara pitch */
    process_keyboard();

    uint32_t now = millis();

    /* Periodic Memory Diagnostic Monitor every 10 seconds */
    static uint32_t s_last_mem_log_ms = 0;
    if (now - s_last_mem_log_ms >= 10000) {
        s_last_mem_log_ms = now;
        uint32_t free_heap      = ESP.getFreeHeap();
        uint32_t min_free_heap  = ESP.getMinFreeHeap();
        uint32_t max_alloc_heap = ESP.getMaxAllocHeap();
        uint32_t total_heap     = ESP.getHeapSize();
        uint32_t used_heap      = total_heap - free_heap;
        float    used_pct       = ((float)used_heap / (float)total_heap) * 100.0f;

        /* Extended cap-based measurements */
        uint32_t free_internal  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t free_dma       = heap_caps_get_free_size(MALLOC_CAP_DMA);
        uint32_t free_iram      = heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT);
        uint32_t free_spiram    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        Serial.println("\n----------------- [MEMORY USAGE (10s)] -----------------");
        Serial.printf("  Total Heap      : %7u bytes (%6.1f KB)\n", total_heap,    total_heap    / 1024.0f);
        Serial.printf("  Used Heap       : %7u bytes (%6.1f KB) [%4.1f%%]\n", used_heap, used_heap / 1024.0f, used_pct);
        Serial.printf("  Free Heap       : %7u bytes (%6.1f KB)\n", free_heap,     free_heap     / 1024.0f);
        Serial.printf("  Lowest Ever Free: %7u bytes (%6.1f KB)\n", min_free_heap, min_free_heap / 1024.0f);
        Serial.printf("  Max Alloc Block : %7u bytes (%6.1f KB)\n", max_alloc_heap,max_alloc_heap/ 1024.0f);
        Serial.println("  --- Extended Breakdown ---");
        Serial.printf("  Free Internal   : %7u bytes (%6.1f KB)  [DRAM+IRAM visible to heap]\n", free_internal, free_internal / 1024.0f);
        Serial.printf("  Free DMA-capable: %7u bytes (%6.1f KB)  [DMA-safe DRAM]\n",             free_dma,      free_dma      / 1024.0f);
        Serial.printf("  Free IRAM       : %7u bytes (%6.1f KB)  [instruction RAM, 8-bit]\n",    free_iram,     free_iram     / 1024.0f);
        Serial.printf("  Free PSRAM      : %7u bytes (%6.1f KB)  [0 = no external PSRAM chip]\n",free_spiram,   free_spiram   / 1024.0f);
        Serial.printf("  Bluetooth State : %s\n", s_a2dp_source.is_connected() ? "CONNECTED" : "SCANNING/DISCONNECTED");
        Serial.println("--------------------------------------------------------\n");
    }

    /* Periodic Bluetooth discovery status reminder every 4 seconds if not connected */
    if (!s_a2dp_source.is_connected() && (now - s_last_bt_status_ms >= 4000)) {
        s_last_bt_status_ms = now;
        Serial.println("[BLUETOOTH] Scanning for any available audio device in pairing mode...");
    }

    /* --- Phase 1: RECORDING --- */
    if (g_rec_state == REC_RECORDING) {
        uint32_t elapsed_ms  = now - g_rec_start_ms;
        uint32_t remaining_s = (VHR_DEMO_RECORD_DURATION_MS > elapsed_ms)
                             ? (VHR_DEMO_RECORD_DURATION_MS - elapsed_ms) / 1000
                             : 0;

        /* Print countdown to Serial once per second */
        if (remaining_s != s_last_sec_log) {
            s_last_sec_log = remaining_s;
            int active_cnt = 0;
            for (int i = 0; i < MAX_POLYPHONY; i++) if (g_voices[i].active) active_cnt++;
            Serial.printf("[REC PROGRESS] %u seconds remaining | Polyphony: %d/%d | BT: %s\n",
                          remaining_s, active_cnt, MAX_POLYPHONY,
                          s_a2dp_source.is_connected() ? "CONNECTED" : "SCANNING");
        }

        /* --- 20-Second Recording Finished --- */
        if (elapsed_ms >= VHR_DEMO_RECORD_DURATION_MS) {
            uint32_t nowUs = (uint32_t)esp_timer_get_time();
            for (int v = 0; v < MAX_POLYPHONY; v++) {
                if (g_voices[v].active) {
                    vhl_recorder_push_event(&g_recorder,
                        VHR_EVENT_NOTE_OFF, g_voices[v].midi_note, 0, 0, nowUs);
                }
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

    yield();
}
