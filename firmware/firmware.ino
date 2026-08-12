/* =============================================================================
 * firmware.ino â€” ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Features:
 *   1. Harmonium audio playback (restored from Tabla)
 *   2. VHR v2.0 Recording Demo â€” Serial-based (no SD card required)
 *      - Recording starts automatically on boot
 *      - Switch 1 (GPIO32) = NOTE_ON  (plays note + records event)
 *      - Switch 2 (GPIO33) = NOTE_OFF (stops note + records event)
 *      - After 20 seconds, recording stops and the full .vhr binary
 *        is transmitted over Serial as Intel HEX for capture on the laptop
 *
 * Hardware Map:
 *   I2C LCD:   SDA = GPIO21, SCL = GPIO22 (PCF8574 HD44780 16x2 LCD @ VIN 5V)
 *   Switch 1:  GPIO32 (INPUT_PULLUP) -> NOTE_ON (press = play + record)
 *   Switch 2:  GPIO33 (INPUT_PULLUP) -> NOTE_OFF (press = stop note)
 *   LED 1:     GPIO18 (OUTPUT) -> ON while recording is active
 *   LED 2:     GPIO19 (OUTPUT) -> ON when a note is held
 *   DAC:       GPIO25 (Internal DAC1 Output for Headphone Jack)
 * ============================================================================= */

#include <Arduino.h>
#include <Wire.h>
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
#endif

/* =============================================================================
 * Recording Demo Configuration
 * ============================================================================= */

/* =============================================================================
 * Recording Demo Configuration
 * ============================================================================= */

#define VHR_DEMO_RECORD_DURATION_MS  (20000U)
#define VHR_DEMO_INSTRUMENT_NAME     "harmonium"
#define VHR_DEMO_TITLE               "Boot Demo Recording"
#define VHR_DEMO_BPM_X10            (800U)      /* 80.0 BPM context metadata */

/* MIDI note for the harmonium root note (C4 = MIDI 60) */
#define VHR_NOTE_ROOT   (60U)   /* SW1: root pitch  (C4, 0 semitones)  */
#define VHR_NOTE_SHIFT  (62U)   /* SW2: root + 2st  (D4, +2 semitones) */
#define VHR_VELOCITY    (100U)

/* Semitone ratio: 2^(1/12) = 1.05946...
 * +2 semitones = 2^(2/12) = 1.12246...  */
#define VHR_SEMITONE_RATIO   (1.05946309436f)
#define VHR_SHIFT_SEMITONES  (2)

/* DAC audio clock: delayMicroseconds(10) = 100,000 Hz effective sample clock.
 * base_pitch_ratio = sample_rate / 100000.0
 * This corrects the playback speed to exactly match the VHL sample rate.
 * (Without this the sample plays 2.27x too fast at 44100 Hz, causing the
 *  audio to sound like a fluctuating chipmunk voice.) */
#define VHR_DAC_CLOCK_HZ    (100000U)  /* 1 / 10us delay */

#define VHR_SERIAL_BAUD     (115200)

/* =============================================================================
 * I2C LCD Driver (Self-Contained PCF8574 HD44780)
 * ============================================================================= */

class I2C_LCD {
private:
    uint8_t _addr;
    uint8_t _backlight;

    void writeNibble(uint8_t nibble, uint8_t rs) {
        uint8_t data = (nibble & 0xF0) | _backlight | rs;
        Wire.beginTransmission(_addr);
        Wire.write(data | 0x04);
        Wire.endTransmission();
        delayMicroseconds(1);
        Wire.beginTransmission(_addr);
        Wire.write(data & ~0x04);
        Wire.endTransmission();
        delayMicroseconds(50);
    }

    void sendByte(uint8_t val, uint8_t rs) {
        writeNibble(val & 0xF0, rs);
        writeNibble((val << 4) & 0xF0, rs);
    }

public:
    I2C_LCD(uint8_t addr = 0x27) : _addr(addr), _backlight(0x08) {}
    uint8_t getAddress() const { return _addr; }

    bool begin(int sda = 21, int scl = 22) {
        Wire.begin(sda, scl);
        delay(100);
        uint8_t found = 0;
        uint8_t addrs[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x3E};
        for (size_t i = 0; i < sizeof(addrs); i++) {
            Wire.beginTransmission(addrs[i]);
            if (Wire.endTransmission() == 0) { found = addrs[i]; break; }
        }
        if (found) _addr = found;
        delay(50);
        writeNibble(0x30, 0); delay(5);
        writeNibble(0x30, 0); delay(5);
        writeNibble(0x30, 0); delayMicroseconds(150);
        writeNibble(0x20, 0);
        sendByte(0x28, 0);
        sendByte(0x0C, 0);
        sendByte(0x01, 0); delay(2);
        sendByte(0x06, 0);
        return (found != 0);
    }

    void command(uint8_t cmd)      { sendByte(cmd, 0); }
    void writeChar(char c)         { sendByte((uint8_t)c, 1); }
    void setCursor(uint8_t col, uint8_t row) {
        static const uint8_t off[] = {0x00, 0x40, 0x14, 0x54};
        command(0x80 | (col + off[row]));
    }
    void printStr(const char* s)   { while (*s) writeChar(*s++); }
    void clear()                   { command(0x01); delay(2); }
};

/* =============================================================================
 * Globals
 * ============================================================================= */

static I2C_LCD              g_lcd(0x27);
static vhl_loader_t         g_vhl_loader;
static vhl_recorder_engine_t g_recorder;

/* Audio playback state */
static volatile float    g_sample_phase    = 0.0f;
static volatile float    g_pitch_ratio_sw1 = 1.0f;  /* corrected for SW1 (root) */
static volatile float    g_pitch_ratio_sw2 = 1.0f;  /* corrected for SW2 (+2st) */
static volatile float    g_active_pitch    = 1.0f;  /* currently active ratio   */

/* Recording lifecycle state */
enum RecordState {
    REC_IDLE       = 0,
    REC_RECORDING  = 1,
    REC_DONE       = 2,
    REC_SENT       = 3
};
static volatile RecordState g_rec_state   = REC_IDLE;
static uint32_t              g_rec_start_ms = 0;

/* The completed VHR binary, assembled after recording stops.
 * Max size: 96 (header) + 512 events Ã— 8 bytes = 4192 bytes worst case.
 * This fits comfortably in ESP32's 520 KB SRAM. */
#define VHR_BINARY_MAX_SIZE  (96U + VHR_RECORDER_RING_SIZE * 8U)
static uint8_t  g_vhr_binary[VHR_BINARY_MAX_SIZE];
static uint32_t g_vhr_binary_size = 0;

/* Switch state tracking */
static bool s_sw1_last = false;
static bool s_sw2_last = false;

/* Per-switch note state (press = change pitch + record, release = record NOTE_OFF) */
static bool s_sw1_note_active = false;   /* root pitch  */
static bool s_sw2_note_active = false;   /* +2 semitones */

/* =============================================================================
 * LCD Display Helpers
 * ============================================================================= */

static void lcd_show(const char* row0, const char* row1) {
    g_lcd.clear();
    g_lcd.setCursor(0, 0); g_lcd.printStr(row0);
    g_lcd.setCursor(0, 1); g_lcd.printStr(row1);
}

/* Write a 2-digit decimal number into buf (no null terminator) */
static void write_dec2(char* buf, uint8_t val) {
    buf[0] = '0' + (val / 10);
    buf[1] = '0' + (val % 10);
}

/* =============================================================================
 * Serial VHR Binary Transmission
 *
 * Protocol: plain hex dump with framing markers so the laptop Python script
 * can extract the binary from the Serial log regardless of other debug output.
 *
 * Format:
 *   >>>VHR_BEGIN<<<
 *   <hex bytes, 16 per line, uppercase, space-separated>
 *   >>>VHR_END<<<
 *
 * The Python receiver strips everything outside the markers and writes the
 * raw binary to a .vhr file on the laptop.
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
 * Layout: [VHRFileHeader (96 bytes)] [VHREventEntry Ã— N (8 bytes each)]
 * ============================================================================= */

static uint32_t assemble_vhr_binary(void)
{
    /* Drain all remaining events from ring buffer */
    VHREventEntry drain_buf[64];
    uint32_t drained = 0;
    uint32_t total_written = 0;

    /* Write placeholder header first (will be overwritten at end) */
    uint8_t* pBase = g_vhr_binary;
    uint32_t event_offset = sizeof(VHRFileHeader);

    /* Drain ring buffer into the binary buffer after the header space */
    vhr_recorder_state_t st = vhl_recorder_get_state(&g_recorder);
    (void)st;

    while (true) {
        drained = 0;
        vhl_recorder_flush_block(&g_recorder, drain_buf, 64, &drained);
        if (drained == 0) break;

        /* Bounds check */
        if (event_offset + drained * sizeof(VHREventEntry) > VHR_BINARY_MAX_SIZE) {
            Serial.println("[VHR] WARNING: binary buffer full, truncating");
            break;
        }

        memcpy(pBase + event_offset, drain_buf, drained * sizeof(VHREventEntry));
        event_offset += drained * sizeof(VHREventEntry);
        total_written += drained;
    }

    /* Finalize header and write at offset 0 */
    VHRFileHeader hdr;
    vhl_recorder_finalize_header(&g_recorder, &hdr);
    memcpy(pBase, &hdr, sizeof(VHRFileHeader));

    return sizeof(VHRFileHeader) + total_written * sizeof(VHREventEntry);
}

/* =============================================================================
 * Audio Fast Helper
 * ============================================================================= */

static inline uint8_t pcm16_to_dac8(int16_t s) {
    static uint32_t dstate = 123456789U;
    dstate = dstate * 1664525U + 1013904223U;
    int32_t v = (int32_t)s + 32768 + (int32_t)(dstate >> 25) - 64;
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint8_t)(v >> 8);
}

static inline float get_frame(const int16_t* p, uint32_t idx, uint8_t ch) {
    if (ch == 2) return (float)(((int32_t)p[2*idx] + (int32_t)p[2*idx+1]) / 2);
    return (float)p[idx];
}

/* =============================================================================
 * Core 0 — Real-Time Audio Task (Harmonium Playback)
 * ============================================================================= */

static void vhl_audio_task(void* pv)
{
    (void)pv;
    uint64_t next_sample_us = esp_timer_get_time();

    while (1) {
        if (g_vhl_loader.isLoaded && g_vhl_loader.pPcmBase) {
            const VHLSampleEntry* pS = &g_vhl_loader.pSamples[0];
            const int16_t* pPcm = (const int16_t*)(g_vhl_loader.pPcmBase + pS->data_offset);
            uint32_t nFrames = pS->total_frames;
            uint8_t ch = pS->channels > 0 ? pS->channels : 1;

            if (pPcm && nFrames > 0) {
                /* Seamless sample looping -- no 200ms delay or drop at sample end */
                while (g_sample_phase >= (float)nFrames) {
                    g_sample_phase -= (float)nFrames;
                }

                uint32_t i0 = (uint32_t)g_sample_phase;
                uint32_t i1 = (i0 + 1 >= nFrames) ? 0 : i0 + 1;
                float frac  = g_sample_phase - (float)i0;

                float interp = (1.0f - frac) * get_frame(pPcm, i0, ch)
                             + frac           * get_frame(pPcm, i1, ch);
                dacWrite(VHL_PIN_TEST_DAC, pcm16_to_dac8((int16_t)interp));
                g_sample_phase += g_active_pitch;  /* use active pitch (sw1 or sw2) */

                /* Target microsecond pacing: exactly 10us per sample iteration */
                next_sample_us += 10;
                uint64_t now_us = esp_timer_get_time();
                if (next_sample_us > now_us) {
                    uint32_t wait_us = (uint32_t)(next_sample_us - now_us);
                    if (wait_us > 0) {
                        delayMicroseconds(wait_us);
                    }
                } else {
                    /* Resync target if processing ever falls behind */
                    next_sample_us = now_us;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
                next_sample_us = esp_timer_get_time();
            }
        } else {
            dacWrite(VHL_PIN_TEST_DAC, 128);
            vTaskDelay(pdMS_TO_TICKS(1));
            next_sample_us = esp_timer_get_time();
        }
    }
}

/* =============================================================================
 * Core 0 — Recording Demo Lifecycle + Switch Handler
 * ============================================================================= */

/* ============================================================================
 * Switch Scanning - Pitch Selection Keys
 *
 * SW1 (GPIO32): Root pitch  (natural sample speed, 0 semitone shift)
 *   Press   -> set audio pitch to base ratio, record NOTE_ON (MIDI 60)
 *   Release -> record NOTE_OFF (MIDI 60)
 *
 * SW2 (GPIO33): +2 semitones above root
 *   Press   -> set audio pitch to base x 2^(2/12), record NOTE_ON (MIDI 62)
 *   Release -> record NOTE_OFF (MIDI 62), revert pitch to SW1 if SW1 held
 *
 * Holding SW1 while pressing SW2 keeps both active (chording).
 * Serial.printf is kept OUT of this function (1x per second in loop() instead).
 * ============================================================================ */

static void handle_switch_press(bool sw1_down, bool sw2_down)
{
    if (g_rec_state != REC_RECORDING) return;

    uint32_t nowUs = (uint32_t)(esp_timer_get_time());

    /* --- SW1: Root pitch (C4 / MIDI 60) --- */
    if (sw1_down && !s_sw1_last) {
        g_active_pitch    = g_pitch_ratio_sw1;     /* switch to root pitch */
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_ON, VHR_NOTE_ROOT, VHR_VELOCITY, 0, nowUs);
        s_sw1_note_active = true;
        digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
    } else if (!sw1_down && s_sw1_last && s_sw1_note_active) {
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_OFF, VHR_NOTE_ROOT, 0, 0, nowUs);
        s_sw1_note_active = false;
        /* If SW2 is still held, restore its pitch */
        if (s_sw2_note_active) g_active_pitch = g_pitch_ratio_sw2;
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    }

    /* --- SW2: +2 semitones (D4 / MIDI 62) --- */
    if (sw2_down && !s_sw2_last) {
        g_active_pitch    = g_pitch_ratio_sw2;     /* switch to +2st pitch */
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_ON, VHR_NOTE_SHIFT, VHR_VELOCITY, 0, nowUs);
        /* Record a TRANSPOSE_CHANGE event so the VHR knows this note is shifted */
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_TRANSPOSE_CHANGE,
            (uint8_t)(int8_t)VHR_SHIFT_SEMITONES, 0, 0, nowUs);
        s_sw2_note_active = true;
        digitalWrite(VHL_PIN_TEST_LED_2, HIGH);
    } else if (!sw2_down && s_sw2_last && s_sw2_note_active) {
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_NOTE_OFF, VHR_NOTE_SHIFT, 0, 0, nowUs);
        /* Record transpose back to 0 on release */
        vhl_recorder_push_event(&g_recorder,
            VHR_EVENT_TRANSPOSE_CHANGE, 0, 0, 0, nowUs);
        s_sw2_note_active = false;
        /* Restore root pitch if SW1 still held */
        if (s_sw1_note_active) g_active_pitch = g_pitch_ratio_sw1;
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
    }
}

/* =============================================================================
 * compute_base_pitch_ratio — Correct audio playback speed from VHL sample rate
 *
 * The audio task uses delayMicroseconds(10), giving a DAC clock of 100,000 Hz.
 * A 44100 Hz sample played at g_pitch_ratio = 1.0 runs 2.27x too fast.
 * This function computes the exact ratio so the sample plays at its intended speed.
 *
 * +2 semitones = base * 2^(2/12) = base * VHR_SEMITONE_RATIO^2
 * ============================================================================= */
static float compute_base_pitch_ratio(uint32_t sampleRate)
{
    if (sampleRate == 0) sampleRate = VHL_SAMPLE_RATE;
    return (float)sampleRate / (float)VHR_DAC_CLOCK_HZ;
}

/* =============================================================================
 * setup() -- Boot Entry Point
 * ============================================================================= */

void setup()
{
    /* Disable Core 0 Watchdog so high-priority audio task on Core 0 can run continuous DSP */
    disableCore0WDT();

    Serial.begin(VHR_SERIAL_BAUD);
    delay(300);

    Serial.println("============================================================");
    Serial.println(" Digital Harmonium Workstation — VHR Recording Demo v2.0");
    Serial.println("============================================================");

    /* 1. Load harmonium VHL package */
    vhl_err_t err = vhl_loader_init(&g_vhl_loader,
                                     g_vhl_harmonium_package,
                                     g_vhl_harmonium_package_size);
    if (err != VHL_OK) {
        Serial.println("[ERROR] VHL Loader failed! Check embed_vhl.py output.");
        return;
    }
    Serial.printf("[VHL] Harmonium package loaded: %u bytes\n",
                  g_vhl_harmonium_package_size);

    /* 2. Hardware init */
    pinMode(VHL_PIN_TEST_SWITCH_1, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_SWITCH_2, INPUT_PULLUP);
    pinMode(VHL_PIN_TEST_LED_1, OUTPUT);
    pinMode(VHL_PIN_TEST_LED_2, OUTPUT);
    digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    digitalWrite(VHL_PIN_TEST_LED_2, LOW);
    dacWrite(VHL_PIN_TEST_DAC, 128);

    /* 3. LCD init */
    bool lcd_ok = g_lcd.begin(21, 22);
    Serial.printf("[LCD] I2C: %s (0x%02X)\n",
                  lcd_ok ? "OK" : "NOT FOUND", g_lcd.getAddress());

    /* Compute pitch ratios from the VHL sample's actual sample_rate.
     * base = sample_rate / DAC_CLOCK_HZ ensures the sample plays at
     * exactly its recorded speed regardless of the 10us delay loop. */
    if (g_vhl_loader.isLoaded && g_vhl_loader.pSamples) {
        uint32_t sr = g_vhl_loader.pSamples[0].sample_rate;
        float base   = compute_base_pitch_ratio(sr);
        float shift2 = base * VHR_SEMITONE_RATIO * VHR_SEMITONE_RATIO; /* x 2^(2/12) */
        g_pitch_ratio_sw1 = base;
        g_pitch_ratio_sw2 = shift2;
        g_active_pitch    = base;   /* start at root pitch */
        Serial.printf("[VHL] sample_rate=%uHz  base_pitch=%.4f  +2st_pitch=%.4f\n",
                      sr, base, shift2);
    } else {
        /* Fallback if loader didn't fill sample table yet */
        g_pitch_ratio_sw1 = compute_base_pitch_ratio(VHL_SAMPLE_RATE);
        g_pitch_ratio_sw2 = g_pitch_ratio_sw1 * VHR_SEMITONE_RATIO * VHR_SEMITONE_RATIO;
        g_active_pitch    = g_pitch_ratio_sw1;
    }
    g_sample_phase = 0.0f;

    vhl_recorder_arm(&g_recorder,
                     VHR_DEMO_INSTRUMENT_NAME,
                     VHR_DEMO_TITLE,
                     VHR_DEMO_BPM_X10,
                     0,      /* transpose = 0 */
                     200U);  /* firmwareVersion = 200 (v2.0.0) */

    /* Transition directly to RECORDING (don't wait for NOTE_ON) */
    vhl_recorder_start(&g_recorder,
                       VHR_DEMO_INSTRUMENT_NAME,
                       VHR_DEMO_TITLE,
                       VHR_DEMO_BPM_X10,
                       0, 200U);

    g_rec_start_ms = millis();
    g_rec_state    = REC_RECORDING;
    digitalWrite(VHL_PIN_TEST_LED_1, HIGH); /* LED1 = recording indicator */

    /* 5. Show recording screen on LCD */
    lcd_show(" VHR RECORDING  ", "SW1=root SW2=+2 ");

    Serial.println("[VHR] Recording STARTED. Duration: 20 seconds.");
    Serial.println("[VHR] SW1 (GPIO32) = Root pitch  (C4)  : PRESS=NOTE_ON  RELEASE=NOTE_OFF");
    Serial.println("[VHR] SW2 (GPIO33) = +2 semitones (D4) : PRESS=NOTE_ON  RELEASE=NOTE_OFF");
    Serial.println("[VHR] Hold SW1 + tap SW2 to chord both pitches!\n");

    /* 6. Launch audio task on Core 0 at priority 5 (Core 0 dedicated for DSP) */
    xTaskCreatePinnedToCore(vhl_audio_task, "vhl_audio", 4096, NULL, 5, NULL, 0);
}

/* =============================================================================
 * loop() - Core 0 Main Loop
 *
 * Phase 1 (REC_RECORDING): 20-second recording window.
 *   SW1 / SW2 record notes. LCD shows countdown.
 *
 * Phase 2 (REC_DONE -> REC_SENT): Recording stops, VHR binary is
 *   transmitted over Serial automatically. Receiver saves it.
 *   LCD shows event count. Nothing more happens until next reset.
 * ============================================================================= */

void loop()
{
    bool sw1 = (digitalRead(VHL_PIN_TEST_SWITCH_1) == LOW);
    bool sw2 = (digitalRead(VHL_PIN_TEST_SWITCH_2) == LOW);

    /* --- Phase 1: RECORDING --- */
    if (g_rec_state == REC_RECORDING) {

        handle_switch_press(sw1, sw2);

        uint32_t elapsed_ms  = millis() - g_rec_start_ms;
        uint32_t remaining_s = (VHR_DEMO_RECORD_DURATION_MS > elapsed_ms)
                             ? (VHR_DEMO_RECORD_DURATION_MS - elapsed_ms) / 1000
                             : 0;

        /* Update LCD countdown ONLY when the remaining seconds number changes (no flicker) */
        static int last_shown_s = -1;
        if ((int)remaining_s != last_shown_s) {
            last_shown_s = (int)remaining_s;
            char row1[17] = "STOP IN 00s     ";
            write_dec2(&row1[8], (uint8_t)(remaining_s % 100));
            g_lcd.setCursor(0, 1);
            g_lcd.printStr(row1);
        }

        /* --- Recording window finished --- */
        if (elapsed_ms >= VHR_DEMO_RECORD_DURATION_MS) {
            /* Release any held notes cleanly */
            uint32_t nowUs = (uint32_t)esp_timer_get_time();
            if (s_sw1_note_active) {
                vhl_recorder_push_event(&g_recorder,
                    VHR_EVENT_NOTE_OFF, VHR_NOTE_ROOT, 0, 0, nowUs);
                s_sw1_note_active = false;
            }
            if (s_sw2_note_active) {
                vhl_recorder_push_event(&g_recorder,
                    VHR_EVENT_NOTE_OFF, VHR_NOTE_SHIFT, 0, 0, nowUs);
                s_sw2_note_active = false;
            }

            vhl_recorder_stop(&g_recorder);
            g_vhr_binary_size = assemble_vhr_binary();

            uint32_t total_evts = 0, dropped = 0;
            vhl_recorder_get_stats(&g_recorder, &total_evts, &dropped);

            digitalWrite(VHL_PIN_TEST_LED_1, LOW);
            digitalWrite(VHL_PIN_TEST_LED_2, LOW);
            g_rec_state = REC_DONE;

            Serial.println("[VHR] === RECORDING COMPLETE ===");
            Serial.printf("[VHR] Events: %u recorded, %u dropped\n", total_evts, dropped);
            Serial.printf("[VHR] Binary: %u bytes\n", g_vhr_binary_size);
            Serial.println("[VHR] Transmitting over Serial...\n");

            lcd_show("VHR DONE!       ", "Sending...      ");
            delay(200);

            /* Transmit once, automatically */
            serial_transmit_vhr(g_vhr_binary, g_vhr_binary_size);
            g_rec_state = REC_SENT;

            /* Show final count on LCD */
            char row0[17] = "DONE  000 events";
            if (total_evts < 1000) {
                row0[6] = '0' + (total_evts / 100);
                row0[7] = '0' + ((total_evts / 10) % 10);
                row0[8] = '0' + (total_evts % 10);
            }
            lcd_show(row0, "Reset to record ");

            Serial.printf("[VHR] Done. %u events saved.\n", total_evts);
            Serial.println("[VHR] Run: python vhr_receiver.py");
            Serial.println("[VHR] Then: python vhr_decompiler.py\n");
        }
    }

    /* --- Phase 2: SENT -- idle until next reset --- */
    /* Nothing to do. Audio keeps playing. Reset ESP32 for a new recording. */

    s_sw1_last = sw1;
    s_sw2_last = sw2;

    delay(5);
}
