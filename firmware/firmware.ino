/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Features:
 *   1. 8-Key 74HC165 Shift Register Keyboard Scanner (Sa, Re, Ga, Ma, Pa, Dha, Ni, Sa)
 *      - Key 1: Sa  (-12 semitones lower, 0.5000x pitch) [MIDI 48]
 *      - Key 2: Re  (-10 semitones,        0.5612x pitch) [MIDI 50]
 *      - Key 3: Ga  ( -8 semitones,        0.6300x pitch) [MIDI 52]
 *      - Key 4: Ma  ( -7 semitones,        0.6674x pitch) [MIDI 53]
 *      - Key 5: Pa  ( -5 semitones,        0.7492x pitch) [MIDI 55]
 *      - Key 6: Dha ( -3 semitones,        0.8409x pitch) [MIDI 57]
 *      - Key 7: Ni  ( -1 semitone,         0.9439x pitch) [MIDI 59]
 *      - Key 8: Sa' (  0 semitones,        1.0000x pitch) [MIDI 60, original sample]
 *   2. Full Harmonium audio playback via PCM5102A I²S DAC (44.1 kHz, 16-bit)
 *   3. Wireless Bluetooth A2DP Source streaming to "Airdopes 219" headphones
 *   4. Dual Audio Output: Wired Speaker + Wireless Headphones simultaneously
 *   5. VHR v2.0 Recording Engine (auto-records first 20 seconds on boot)
 *   6. Automatic Intel HEX serialization & transmission over Serial
 *   7. Memory usage monitor with MALLOC_CAP diagnostics
 *
 * Hardware Map:
 *   74HC165 Shift Register:
 *     LOAD_PIN:  GPIO19 (OUTPUT)
 *     CLOCK_PIN: GPIO18 (OUTPUT)
 *     DATA_PIN:  GPIO34 (INPUT)
 *   PCM5102A I2S DAC:
 *     DOUT:      GPIO25 (DIN)
 *     BCLK:      GPIO26 (BCK)
 *     LRCLK:     GPIO27 (LRCK)
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
 * 74HC165 Shift Register Pin Configuration
 * ============================================================================= */
#define HC165_LOAD_PIN   19
#define HC165_CLOCK_PIN  18
#define HC165_DATA_PIN   34

/* Configuration:
 * Set HC165_KEY8_IS_MSB to 1 if 10000000 is Key 8 (Sa' High) and 00000001 is Key 1 (Sa Low).
 * Set to 0 if 10000000 is Key 1 and 00000001 is Key 8.
 */
#define HC165_KEY8_IS_MSB  1


/* =============================================================================
 * Swara Musical Definitions (Bilawal Thaat / 1 Octave Scale)
 * ============================================================================= */
typedef struct {
    const char* name;          /* Swara name */
    float       pitch_ratio;   /* Playback speed ratio */
    int8_t      semitones;     /* Relative to original sample (Key 8) */
    uint8_t     midi_note;     /* MIDI Note number */
} swara_t;

/* 8 Swaras from lower octave Sa (-12 semitones) to original sample Sa (0 semitones) */
static const swara_t SWARAS[8] = {
    { "Sa",  0.500000f, -12, 48 },  /* Key 1: 1 octave lower (C3)  */
    { "Re",  0.561231f, -10, 50 },  /* Key 2: 10 st lower (D3)     */
    { "Ga",  0.629961f,  -8, 52 },  /* Key 3: 8 st lower (E3)      */
    { "Ma",  0.667420f,  -7, 53 },  /* Key 4: 7 st lower (F3)      */
    { "Pa",  0.749154f,  -5, 55 },  /* Key 5: 5 st lower (G3)      */
    { "Dha", 0.840896f,  -3, 57 },  /* Key 6: 3 st lower (A3)      */
    { "Ni",  0.943874f,  -1, 59 },  /* Key 7: 1 st lower (B3)      */
    { "Sa'", 1.000000f,   0, 60 }   /* Key 8: Original sample (C4) */
};

/* =============================================================================
 * Recording & Audio Configuration
 * ============================================================================= */

#define VHR_DEMO_RECORD_DURATION_MS  (20000U)
#define VHR_DEMO_INSTRUMENT_NAME     "harmonium"
#define VHR_DEMO_TITLE               "Boot Demo Recording"
#define VHR_DEMO_BPM_X10             (800U)      /* 80.0 BPM context metadata */
#define VHR_VELOCITY                 (100U)
#define VHR_SERIAL_BAUD              (115200)

/* Target Bluetooth Headphone Name */
#define BT_TARGET_DEVICE_NAME "Airdopes 219"

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

/* Audio playback states */
static volatile float        g_sample_phase    = 0.0f; /* Wired I2S phase */
static volatile float        g_bt_sample_phase = 0.0f; /* Wireless BT phase */
static volatile float        g_active_pitch    = 1.000000f; /* Default to original sample */
static volatile bool         g_key_pressed     = false;     /* true while any key is pressed */

/* Bluetooth A2DP Source Instance */
static BluetoothA2DPSource   s_a2dp_source;

/* Currently pressed key tracking (-1 = none, 0..7 = Key 1..8) */
static int                   s_current_key_idx = -1;
static uint8_t               s_last_raw_bits   = 0xFF;

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
        int16_t sample = 0;

        if (g_key_pressed && pPcm && nFrames > 0) {
            while (g_bt_sample_phase >= (float)nFrames) {
                g_bt_sample_phase -= (float)nFrames;
            }

            uint32_t i0   = (uint32_t)g_bt_sample_phase;
            uint32_t i1   = (i0 + 1 >= nFrames) ? 0 : i0 + 1;
            float    frac = g_bt_sample_phase - (float)i0;

            float interp  = (1.0f - frac) * get_frame(pPcm, i0, ch)
                          + frac          * get_frame(pPcm, i1, ch);
            sample = (int16_t)interp;
            g_bt_sample_phase += g_active_pitch;
        }

        data[i].channel1 = sample;
        data[i].channel2 = sample;
    }

    return frame_count;
}

/* Bluetooth Connection State Observer */
static void bt_connection_state_cb(esp_a2d_connection_state_t state, void* obj)
{
    (void)obj;
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            Serial.println("\n[BLUETOOTH] Disconnected from headphones.");
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.printf("\n[BLUETOOTH] Connecting to '%s'...\n", BT_TARGET_DEVICE_NAME);
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            Serial.println("\n============================================================");
            Serial.printf(">>> [BLUETOOTH] CONNECTED TO %s! <<<\n", BT_TARGET_DEVICE_NAME);
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
 * ============================================================================= */

static void audio_i2s_task(void* arg)
{
    int16_t buf[AUDIO_BUF_FRAMES * 2];
    bool    first_write = true;

    while (true) {
        for (int i = 0; i < AUDIO_BUF_FRAMES; i++) {
            int16_t sample = 0;

            if (g_key_pressed && g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
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
            Serial.println(">>> WIRED I2S AUDIO STREAM: ACTIVE (PCM5102A) <<<");
            first_write = false;
        }
    }
}

/* =============================================================================
 * 74HC165 Shift Register Driver
 * ============================================================================= */
static inline void hc165_clock_pulse(void)
{
    digitalWrite(HC165_CLOCK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(HC165_CLOCK_PIN, LOW);
}

static uint8_t hc165_read_byte(void)
{
    /* Latch parallel inputs (A-H) */
    digitalWrite(HC165_LOAD_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(HC165_LOAD_PIN, HIGH);
    delayMicroseconds(2);

    uint8_t raw = 0;
    for (int i = 0; i < 8; i++) {
        int bit = digitalRead(HC165_DATA_PIN);
        raw = (raw << 1) | (bit & 1);
        hc165_clock_pulse();
    }
    return raw;
}

/* Decode pressed key index (0 to 7) or -1 if no key is pressed */
static int decode_key_index(uint8_t raw)
{
    if (raw == 0x00) return -1;

#if HC165_KEY8_IS_MSB
    /* 10000000 (0x80) -> Key 8 (index 7: Sa')
     * 00000001 (0x01) -> Key 1 (index 0: Sa)
     */
    for (int k = 7; k >= 0; k--) {
        if (raw & (1 << k)) {
            return k; /* 0..7 matches SWARAS[0..7] */
        }
    }
#else
    /* 10000000 (0x80) -> Key 1 (index 0: Sa)
     * 00000001 (0x01) -> Key 8 (index 7: Sa')
     */
    for (int k = 7; k >= 0; k--) {
        if (raw & (1 << k)) {
            return (7 - k);
        }
    }
#endif
    return -1;
}

/* =============================================================================
 * 74HC165 Keyboard Scanner & Swara Pitch Processor
 * ============================================================================= */
static void process_keyboard(void)
{
    uint8_t raw = hc165_read_byte();
    int key_idx = decode_key_index(raw);

    /* Check if key state changed */
    if (raw != s_last_raw_bits) {
        s_last_raw_bits = raw;
        uint32_t nowUs = (uint32_t)esp_timer_get_time();

        /* Print raw 8-bit string to Serial */
        Serial.print("[74HC165] Bits: ");
        for (int i = 7; i >= 0; i--) {
            Serial.print((raw >> i) & 1);
        }

        if (key_idx >= 0 && key_idx < 8) {
            g_key_pressed = true; /* Key held: audio active */

            /* Only retrigger from beginning if key actually CHANGED */
            if (key_idx != s_current_key_idx) {
                const swara_t* pSwara = &SWARAS[key_idx];
                g_active_pitch    = pSwara->pitch_ratio;
                g_sample_phase    = 0.0f; /* Reset wired I2S DAC to frame 0 */
                g_bt_sample_phase = 0.0f; /* Reset Bluetooth to frame 0 */

                if (g_rec_state == REC_RECORDING) {
                    if (s_current_key_idx >= 0) {
                        vhl_recorder_push_event(&g_recorder,
                            VHR_EVENT_NOTE_OFF, SWARAS[s_current_key_idx].midi_note, 0, 0, nowUs);
                    }
                    vhl_recorder_push_event(&g_recorder,
                        VHR_EVENT_NOTE_ON, pSwara->midi_note, VHR_VELOCITY, 0, nowUs);
                    vhl_recorder_push_event(&g_recorder,
                        VHR_EVENT_TRANSPOSE_CHANGE, (uint8_t)(int8_t)pSwara->semitones, 0, 0, nowUs);
                }

                s_current_key_idx = key_idx;
                Serial.printf(" -> Key %d [%s] | Pitch: %.4fx (%+d st) | MIDI %u (Attack Retrigger)\n",
                              key_idx + 1, pSwara->name, pSwara->pitch_ratio, pSwara->semitones, pSwara->midi_note);
            } else {
                Serial.println();
            }
        } else {
            /* Keys released: stop audio playback immediately */
            g_key_pressed = false;

            if (s_current_key_idx >= 0) {
                if (g_rec_state == REC_RECORDING) {
                    vhl_recorder_push_event(&g_recorder,
                        VHR_EVENT_NOTE_OFF, SWARAS[s_current_key_idx].midi_note, 0, 0, nowUs);
                }
                Serial.printf(" -> Released [%s] (sound stopped)\n", SWARAS[s_current_key_idx].name);
                s_current_key_idx = -1;
            } else {
                Serial.println(" -> None");
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
    Serial.println(" ESP32 Digital Harmonium — 8-Key 74HC165 Workstation");
    Serial.println("============================================================");

    /* 1. 74HC165 Shift Register Pins */
    pinMode(HC165_LOAD_PIN, OUTPUT);
    pinMode(HC165_CLOCK_PIN, OUTPUT);
    pinMode(HC165_DATA_PIN, INPUT);
    digitalWrite(HC165_CLOCK_PIN, LOW);
    digitalWrite(HC165_LOAD_PIN, HIGH);

    Serial.println("--- 74HC165 KEYBOARD CONFIG ---");
    Serial.printf("LOAD_PIN  : GPIO%d\n", HC165_LOAD_PIN);
    Serial.printf("CLOCK_PIN : GPIO%d\n", HC165_CLOCK_PIN);
    Serial.printf("DATA_PIN  : GPIO%d\n", HC165_DATA_PIN);
    Serial.println("Swaras (8 Keys):");
    for (int i = 0; i < 8; i++) {
        Serial.printf("  Key %d: %-3s | Pitch: %.4fx | Semitones: %+3d | MIDI: %u\n",
                      i + 1, SWARAS[i].name, SWARAS[i].pitch_ratio, SWARAS[i].semitones, SWARAS[i].midi_note);
    }
    Serial.println("--------------------------------\n");

    /* 2. Load Harmonium VHL Package */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader,
                                     g_vhl_harmonium_package,
                                     g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader failed!");
        return;
    }
    Serial.printf("[VHL] Package loaded: %u bytes\n", (unsigned int)g_vhl_harmonium_package_size);

    g_active_pitch    = 1.000000f;
    g_sample_phase    = 0.0f;
    g_bt_sample_phase = 0.0f;

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

    /* 5. Initialize Bluetooth A2DP Source (Wireless Headphones Output) */
    Serial.println("--- BLUETOOTH A2DP SOURCE (Wireless Output) ---");
    Serial.printf("Target Device : '%s'\n", BT_TARGET_DEVICE_NAME);
    Serial.println("Mode          : A2DP Source (Transmitter)");
    Serial.println("Sample rate   : 44100 Hz, 16-bit stereo SBC");
    Serial.println("Instruction   : Put your Airdopes 219 in Pairing Mode!");

    s_a2dp_source.set_ssp_enabled(true);
    s_a2dp_source.set_auto_reconnect(true);
    s_a2dp_source.set_on_connection_state_changed(bt_connection_state_cb);
    s_a2dp_source.start(BT_TARGET_DEVICE_NAME, bt_audio_data_cb);

    Serial.println("Bluetooth A2DP Source initialized successfully.\n");
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
        Serial.printf("[BLUETOOTH] Scanning / Waiting for '%s' in pairing mode...\n", BT_TARGET_DEVICE_NAME);
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
            Serial.printf("[REC PROGRESS] %u seconds remaining | Pitch: %.3fx | BT: %s\n",
                          remaining_s, g_active_pitch,
                          s_a2dp_source.is_connected() ? "CONNECTED" : "SCANNING");
        }

        /* --- 20-Second Recording Finished --- */
        if (elapsed_ms >= VHR_DEMO_RECORD_DURATION_MS) {
            uint32_t nowUs = (uint32_t)esp_timer_get_time();
            if (s_current_key_idx >= 0) {
                vhl_recorder_push_event(&g_recorder,
                    VHR_EVENT_NOTE_OFF, SWARAS[s_current_key_idx].midi_note, 0, 0, nowUs);
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

    delay(5);
}
