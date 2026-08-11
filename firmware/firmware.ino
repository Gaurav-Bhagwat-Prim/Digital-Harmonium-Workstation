/* =============================================================================
 * firmware.ino — ESP32 Digital Harmonium Workstation Firmware
 * Target: ESP32-WROOM / ESP32 DevKit Physical Board
 *
 * Feature: 
 *   1. 200ms Audio Loop Restart Delay
 *   2. Ektaal 12-Matra Visualization with Sam, Khali, and Taali (1st, 5th, 9th, 11th) Custom Glyphs
 *
 * Hardware Map:
 *   I2C LCD:   SDA = GPIO21, SCL = GPIO22 (PCF8574 HD44780 16x2 LCD @ VIN 5V)
 *   Switch 1:  GPIO32 (INPUT_PULLUP) -> Press to transition to EKTAAL / Re-trigger
 *   Switch 2:  GPIO33 (INPUT_PULLUP) -> Test input
 *   LED 1:     GPIO18 (OUTPUT) -> HIGH when Switch 1 is pressed
 *   LED 2:     GPIO19 (OUTPUT) -> HIGH when Switch 2 is pressed
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
 * 16x2 I2C HD44780 LCD Driver (Self-Contained PCF8574 Protocol)
 * ============================================================================= */

class I2C_LCD {
private:
    uint8_t _addr;
    uint8_t _backlight;

    void writeNibble(uint8_t nibble, uint8_t rs) {
        uint8_t data = (nibble & 0xF0) | _backlight | rs;
        Wire.beginTransmission(_addr);
        Wire.write(data | 0x04); // EN = 1
        Wire.endTransmission();
        delayMicroseconds(1);
        Wire.beginTransmission(_addr);
        Wire.write(data & ~0x04); // EN = 0
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
        delay(100); // Allow 5V power domain to stabilize
        
        /* Auto-detect I2C address across common PCF8574 addresses */
        uint8_t found_addr = 0;
        uint8_t test_addrs[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x3E};
        for (size_t i = 0; i < sizeof(test_addrs); i++) {
            Wire.beginTransmission(test_addrs[i]);
            if (Wire.endTransmission() == 0) {
                found_addr = test_addrs[i];
                break;
            }
        }

        if (found_addr != 0) {
            _addr = found_addr;
        }

        delay(50);
        /* HD44780 4-bit Initialization Sequence */
        writeNibble(0x30, 0); delay(5);
        writeNibble(0x30, 0); delay(5);
        writeNibble(0x30, 0); delayMicroseconds(150);
        writeNibble(0x20, 0); // 4-bit mode

        sendByte(0x28, 0); // Function set: 2 lines, 5x8 font
        sendByte(0x0C, 0); // Display ON, Cursor OFF, Blink OFF
        sendByte(0x01, 0); delay(2); // Clear display
        sendByte(0x06, 0); // Entry mode set: Increment, no shift
        return (found_addr != 0);
    }

    void command(uint8_t cmd) {
        sendByte(cmd, 0);
    }

    void writeChar(char c) {
        sendByte((uint8_t)c, 1); // RS = 1 (Data)
    }

    void setCursor(uint8_t col, uint8_t row) {
        static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
        command(0x80 | (col + row_offsets[row]));
    }

    void printStr(const char* str) {
        while (*str) {
            writeChar(*str++);
        }
    }

    void createCustomChar(uint8_t location, const uint8_t charmap[8]) {
        location &= 0x07; // 0..7
        command(0x40 | (location << 3));
        for (int i = 0; i < 8; i++) {
            writeChar(charmap[i]);
        }
    }

    void clear() {
        command(0x01);
        delay(2);
    }
};

static I2C_LCD g_lcd(0x27);

/* =============================================================================
 * Custom 5x8 HD44780 LCD Glyphs for Sam, Khali, Taali, and Active Highlight
 * ============================================================================= */

/* Glyph 0: Sam Symbol (Filled Diamond) */
static const uint8_t GLYPH_SAM_DATA[8] = {
    0b00100, //   *  
    0b01110, //  *** 
    0b11111, // *****
    0b11111, // *****
    0b01110, //  *** 
    0b00100, //   *  
    0b00000,
    0b00000
};

/* Glyph 1: Khali Symbol (Hollow Circle) */
static const uint8_t GLYPH_KHALI_DATA[8] = {
    0b00000,
    0b01110, //  *** 
    0b10001, // *   *
    0b10001, // *   *
    0b01110, //  *** 
    0b00000,
    0b00000,
    0b00000
};

/* Glyph 2: Active Matra Marker (Inverted Highlight Box) */
static const uint8_t GLYPH_ACTIVE_DATA[8] = {
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111
};

/* Glyph 3: Taali Symbol (Plus / Star Clap Symbol) */
static const uint8_t GLYPH_TAALI_DATA[8] = {
    0b00100, //   *  
    0b00100, //   *  
    0b11111, // *****
    0b00100, //   *  
    0b00100, //   *  
    0b00000,
    0b00000,
    0b00000
};

/* =============================================================================
 * Shared Runtime & UI State
 * ============================================================================= */

enum TablaUIState {
    TABLA_UI_READY = 0,
    TABLA_UI_EKTAAL = 1
};

static vhl_loader_t g_vhl_loader;

static volatile uint32_t g_sample_cursor = 0;   /* Direct integer frame index */
static volatile float    g_sample_phase  = 0.0f; /* Sub-sample position */
static volatile float    g_pitch_ratio   = 1.0f; /* Fixed 1.0x native pitch */

static volatile TablaUIState g_ui_state = TABLA_UI_READY;
static volatile uint8_t      g_current_matra = 0;  /* Matra index 0..11 */

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

/* Render Screen 1: TABLA READY */
static void render_ready_screen(void)
{
    g_lcd.clear();
    g_lcd.setCursor(0, 0);
    g_lcd.printStr("     TABLA      ");
    g_lcd.setCursor(0, 1);
    g_lcd.printStr("     READY      ");
}

/* Helper to get base character for Ektaal Matra m (0..11):
 * Matra 0 (1st beat):  Sam   (Glyph 0)
 * Matra 2 (3rd beat):  Khali (Glyph 1)
 * Matra 4 (5th beat):  Taali (Glyph 3)
 * Matra 6 (7th beat):  Khali (Glyph 1)
 * Matra 8 (9th beat):  Taali (Glyph 3)
 * Matra 10 (11th beat): Taali (Glyph 3)
 * Other matras:        Normal Dot '.'
 */
static char get_matra_base_char(uint8_t m)
{
    if (m == 0)                   return '\x00'; // Sam (1st beat)
    if (m == 2 || m == 6)          return '\x01'; // Khali (3rd & 7th beats)
    if (m == 4 || m == 8 || m == 10) return '\x03'; // Taali (5th, 9th & 11th beats)
    return '.';                                   // Normal Matras
}

/* Full Render of Screen 2: EKTAAL Mode */
static void render_ektaal_screen_full(uint8_t activeMatra)
{
    g_lcd.setCursor(0, 0);
    g_lcd.printStr("EKTAAL       80 "); // Row 0: EKTAAL at 80 BPM

    g_lcd.setCursor(0, 1);
    g_lcd.printStr("  "); // 2 padding spaces (columns 0..1)

    for (uint8_t m = 0; m < 12; m++) {
        if (m == activeMatra) {
            g_lcd.writeChar('\x02'); // Active Matra Highlight (Glyph 2)
        } else {
            g_lcd.writeChar(get_matra_base_char(m));
        }
    }
    g_lcd.printStr("  "); // 2 padding spaces (columns 14..15)
}

/* Ultra-Fast Selective Update (2ms): Updates ONLY changed matras (prev -> new) */
static void update_ektaal_matra_fast(uint8_t prevMatra, uint8_t newMatra)
{
    /* Restore previous matra to its base character */
    if (prevMatra < 12) {
        g_lcd.setCursor(2 + prevMatra, 1);
        g_lcd.writeChar(get_matra_base_char(prevMatra));
    }
    /* Highlight new active matra */
    if (newMatra < 12) {
        g_lcd.setCursor(2 + newMatra, 1);
        g_lcd.writeChar('\x02'); // Active Highlight (Glyph 2)
    }
}

/* Core 1 Real-Time Audio Task with 200ms Audible Loop Restart Delay */
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

                /* When loop finishes, reset to frame 0 and insert 200ms delay */
                if (idx0 >= totalFrames) {
                    g_sample_phase = 0.0f;
                    idx0 = 0;
                    idx1 = 1;
                    dacWrite(VHL_PIN_TEST_DAC, 128); // Silences output during pause
                    vTaskDelay(pdMS_TO_TICKS(200));  // 200ms loop restart delay
                }
                if (idx1 >= totalFrames) {
                    idx1 = 0;
                }

                /* Synchronize Matra index (0..11) directly with audio sample frames */
                uint32_t framesPerMatra = totalFrames / 12;
                if (framesPerMatra > 0) {
                    g_current_matra = (uint8_t)((idx0 / framesPerMatra) % 12);
                }

                /* Mono or Stereo sample extraction */
                float s0 = get_frame_sample(pPcmData, idx0, channels);
                float s1 = get_frame_sample(pPcmData, idx1, channels);
                float interpolated = (1.0f - frac) * s0 + frac * s1;

                int16_t rawSample = (int16_t)interpolated;

                /* Apply Triangular Dither to smooth 8-bit DAC quantization */
                uint8_t dacByte = pcm16_to_dac8_dither(rawSample);

                /* Write directly to GPIO25 DAC */
                dacWrite(VHL_PIN_TEST_DAC, dacByte);

                /* Advance sub-sample phase accumulator */
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

/* Core 0 Switch Scanner — ZERO Delay in LCD Display & Button Response */
static bool s_sw1_last = false;
static bool s_sw2_last = false;

static void scan_switches_and_update(void)
{
    int raw1 = digitalRead(VHL_PIN_TEST_SWITCH_1);
    int raw2 = digitalRead(VHL_PIN_TEST_SWITCH_2);

    bool sw1_pressed = (raw1 == LOW);
    bool sw2_pressed = (raw2 == LOW);

    /* Switch 1: Instant Transition to Ektaal Mode */
    if (sw1_pressed && !s_sw1_last) {
        digitalWrite(VHL_PIN_TEST_LED_1, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);

        if (g_ui_state == TABLA_UI_READY) {
            g_ui_state = TABLA_UI_EKTAAL;
            g_lcd.clear();
            render_ektaal_screen_full(0);
            g_sample_phase = 0.0f; // Instant audio restart
            Serial.println("\n[UI SWITCH] Instant Transition to EKTAAL Mode!");
        } else {
            g_sample_phase = 0.0f; // Instant audio restart
            Serial.println("\n[UI SWITCH] Instant Re-trigger of Ektaal from Sam!");
        }
    } else if (!sw1_pressed && s_sw1_last && !sw2_pressed) {
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
    }
    s_sw1_last = sw1_pressed;

    /* Switch 2: Instant Re-trigger */
    if (sw2_pressed && !s_sw2_last) {
        digitalWrite(VHL_PIN_TEST_LED_2, HIGH);
        digitalWrite(VHL_PIN_TEST_LED_1, LOW);
        g_sample_phase = 0.0f;
    } else if (!sw2_pressed && s_sw2_last && !sw1_pressed) {
        digitalWrite(VHL_PIN_TEST_LED_2, LOW);
    }
    s_sw2_last = sw2_pressed;
}

/* Hardware Interface & LCD Setup */
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

    /* Initialize 16x2 I2C LCD on GPIO21 (SDA) and GPIO22 (SCL) */
    bool lcd_found = g_lcd.begin(21, 22);

    Serial.printf("[LCD SCAN] I2C Backpack Auto-Detection: %s (Address: 0x%02X)\n",
                  lcd_found ? "SUCCESS" : "FAIL", g_lcd.getAddress());

    /* Register Custom 5x8 Glyphs */
    g_lcd.createCustomChar(0, GLYPH_SAM_DATA);    // Glyph 0: Sam
    g_lcd.createCustomChar(1, GLYPH_KHALI_DATA);  // Glyph 1: Khali
    g_lcd.createCustomChar(2, GLYPH_ACTIVE_DATA); // Glyph 2: Active Highlight
    g_lcd.createCustomChar(3, GLYPH_TAALI_DATA);  // Glyph 3: Taali

    /* Render initial READY screen */
    render_ready_screen();
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

    /* 2. Initialize Hardware & 16x2 LCD */
    vhl_hw_init();
    Serial.println("[HARDWARE] Pins Configured: GPIO32 (SW1: Enter Ektaal), GPIO33 (SW2), GPIO18 (LED1), GPIO19 (LED2), GPIO25 (DAC)");
    Serial.println("[MODE] TABLA EKTAAL 12-MATRA EXPERIMENT (200ms Loop Delay & Sam/Khali/Taali Glyphs Ready).\n");

    /* 3. Launch Core 1 Audio Task */
    xTaskCreatePinnedToCore(vhl_audio_task, "vhl_audio_task", 4096, NULL, 1, NULL, 1);
}

/* Core 0 Main Input Scan & High-Speed LCD UI Update Loop */
static uint8_t s_last_rendered_matra = 255;

void loop()
{
    scan_switches_and_update();

    /* Selective Ultra-Fast LCD Rendering: Update ONLY when matra changes (2ms latency) */
    if (g_ui_state == TABLA_UI_EKTAAL) {
        uint8_t curr = g_current_matra;
        if (curr != s_last_rendered_matra) {
            if (s_last_rendered_matra == 255) {
                render_ektaal_screen_full(curr);
            } else {
                update_ektaal_matra_fast(s_last_rendered_matra, curr);
            }
            s_last_rendered_matra = curr;
        }
    }

    delay(5);
}
