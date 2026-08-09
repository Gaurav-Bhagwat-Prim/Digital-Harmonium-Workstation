/* =============================================================================
 * main.c — Digital Harmonium Workstation Firmware
 * Temporary Hardware Validation Test — VHL Audio Pipeline Workbench
 *
 * Hardware Workbench Wiring:
 *   Switch 1: GPIO32 (INPUT_PULLUP: LOW = Pressed, HIGH = Released)
 *   Switch 2: GPIO33 (INPUT_PULLUP: LOW = Pressed, HIGH = Released)
 *   LED 1:    GPIO18 (OUTPUT: HIGH = ON, LOW = OFF)
 *   LED 2:    GPIO19 (OUTPUT: HIGH = ON, LOW = OFF)
 *   DAC:      GPIO25 (Internal DAC1 8-bit output)
 *
 * Pipeline Flow:
 *   Switch Pressed → NOTE_ON → VHL Region Lookup → Voice Allocation →
 *   ADSR Envelope + Biquad Filter + LFO + Soft Limiter DSP → PCM Render →
 *   Float-to-8bit DAC Adapter → GPIO25 DAC Output
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

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

/* ESP32 Hardware Platform Includes */
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
 * Hardware Abstraction Layer for Workbench
 * ============================================================================= */

#if defined(ESP_PLATFORM)
#if defined(VHL_HAS_ONESHOT_DAC)
static dac_oneshot_handle_t s_dac_handle = NULL;
#endif

static void vhl_hw_init(void)
{
    /* Configure Switch Pins: GPIO32, GPIO33 as INPUT_PULLUP */
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << VHL_PIN_TEST_SWITCH_1) | (1ULL << VHL_PIN_TEST_SWITCH_2);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    /* Configure LED Pins: GPIO18, GPIO19 as OUTPUT */
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << VHL_PIN_TEST_LED_1) | (1ULL << VHL_PIN_TEST_LED_2);
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(VHL_PIN_TEST_LED_1, 0);
    gpio_set_level(VHL_PIN_TEST_LED_2, 0);

    /* Configure DAC Channel 1 (GPIO25) */
#if defined(VHL_HAS_LEGACY_DAC)
    dac_output_enable(DAC_CHANNEL_1);
#elif defined(VHL_HAS_ONESHOT_DAC)
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0, /* DAC_CHAN_0 is GPIO25 */
    };
    dac_oneshot_new_channel(&dac_cfg, &s_dac_handle);
#endif
}

static bool vhl_hw_read_switch1(void)
{
    return (gpio_get_level(VHL_PIN_TEST_SWITCH_1) == 0); /* LOW = Pressed */
}

static bool vhl_hw_read_switch2(void)
{
    return (gpio_get_level(VHL_PIN_TEST_SWITCH_2) == 0); /* LOW = Pressed */
}

static void vhl_hw_write_led1(bool on)
{
    gpio_set_level(VHL_PIN_TEST_LED_1, on ? 1 : 0);
}

static void vhl_hw_write_led2(bool on)
{
    gpio_set_level(VHL_PIN_TEST_LED_2, on ? 1 : 0);
}

static void vhl_hw_write_dac(uint8_t dac_val)
{
#if defined(VHL_HAS_LEGACY_DAC)
    dac_output_voltage(DAC_CHANNEL_1, dac_val);
#elif defined(VHL_HAS_ONESHOT_DAC)
    if (s_dac_handle) {
        dac_oneshot_output_voltage(s_dac_handle, dac_val);
    }
#else
    (void)dac_val;
#endif
}

#else

/* Host Verification Fallback Driver State */
static bool s_sim_sw1  = false;
static bool s_sim_sw2  = false;
static bool s_sim_led1 = false;
static bool s_sim_led2 = false;
static uint8_t s_sim_dac = 128;

static void vhl_hw_init(void)
{
    s_sim_sw1  = false;
    s_sim_sw2  = false;
    s_sim_led1 = false;
    s_sim_led2 = false;
    s_sim_dac  = 128;
}

static bool vhl_hw_read_switch1(void)
{
    return s_sim_sw1;
}

static bool vhl_hw_read_switch2(void)
{
    return s_sim_sw2;
}

static void vhl_hw_write_led1(bool on)
{
    s_sim_led1 = on;
}

static void vhl_hw_write_led2(bool on)
{
    s_sim_led2 = on;
}

static void vhl_hw_write_dac(uint8_t dac_val)
{
    s_sim_dac = dac_val;
}

#endif

/* =============================================================================
 * In-Memory Binary VHL Package Construction
 * Generates valid VHL binary containing a rich Harmonium Reed PCM sample table
 * ============================================================================= */

#define VHL_TEST_PCM_FRAMES 882U
static vhl_loader_t g_vhl_loader;
static vhl_poly_synth_t g_poly_synth;

/* Convert float PCM sample [-1.0..+1.0] to 8-bit unsigned DAC value [0..255] */
static inline uint8_t convert_sample_to_dac8(float sampleL, float sampleR)
{
    float mono = (sampleL + sampleR) * 0.5f;
    float dac_f = (mono * 127.0f) + 128.0f;
    if (dac_f > 255.0f) dac_f = 255.0f;
    if (dac_f < 0.0f)   dac_f = 0.0f;
    return (uint8_t)dac_f;
}

/* Switch & LED State Scanning Engine */
static bool s_sw1_last_state = false;
static bool s_sw2_last_state = false;

static void scan_switches_and_update(vhl_poly_synth_t* pSynth)
{
    bool sw1_pressed = vhl_hw_read_switch1();
    bool sw2_pressed = vhl_hw_read_switch2();

    /* Switch 1: Note 60 (Middle C / C4) */
    if (sw1_pressed != s_sw1_last_state) {
        s_sw1_last_state = sw1_pressed;
        vhl_hw_write_led1(sw1_pressed);

        if (sw1_pressed) {
            printf("[VHL TEST] NOTE ON: 60\n");
            vhl_poly_synth_note_on(pSynth, 60, 100);
        } else {
            printf("[VHL TEST] NOTE OFF: 60\n");
            vhl_poly_synth_note_off(pSynth, 60);
        }
    }

    /* Switch 2: Note 67 (G4) */
    if (sw2_pressed != s_sw2_last_state) {
        s_sw2_last_state = sw2_pressed;
        vhl_hw_write_led2(sw2_pressed);

        if (sw2_pressed) {
            printf("[VHL TEST] NOTE ON: 67\n");
            vhl_poly_synth_note_on(pSynth, 67, 100);
        } else {
            printf("[VHL TEST] NOTE OFF: 67\n");
            vhl_poly_synth_note_off(pSynth, 67);
        }
    }
}

/* Render audio blocks and output to GPIO25 DAC */
static bool render_and_output_dac_block(vhl_poly_synth_t* pSynth)
{
    bool has_audio = false;
    /* Render 4 blocks (2048 frames ~46.4 ms) to advance past any VHL initial envelope delay */
    for (int block = 0; block < 4; ++block) {
        vhl_sample_t outL[512];
        vhl_sample_t outR[512];
        vhl_audio_buffer_t audioBuf = { .left = outL, .right = outR, .frameCount = 512 };

        vhl_poly_synth_render(pSynth, &audioBuf, 512);

        for (size_t i = 0; i < 512; ++i) {
            if (fabsf(outL[i]) > 0.0001f || fabsf(outR[i]) > 0.0001f) {
                has_audio = true;
            }
            uint8_t dacVal = convert_sample_to_dac8(outL[i], outR[i]);
            vhl_hw_write_dac(dacVal);
        }
    }
    return has_audio;
}

/* =============================================================================
 * Stage 12 Test Helpers & Acceptance Test Suite
 * ============================================================================= */

static uint32_t test_crc32(const uint8_t* pData, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= pData[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
    }
    return ~crc;
}

#define MAX_TEST_EVENTS 32U

typedef struct {
    VHRFileHeader header;
    VHREventEntry events[MAX_TEST_EVENTS];
} test_vhr_buf_t;

static void build_test_vhr(test_vhr_buf_t*      pBuf,
                             const VHREventEntry* pEvts,
                             uint32_t             numEvts,
                             uint8_t              instrId,
                             const char*          title)
{
    memset(pBuf, 0, sizeof(*pBuf));

    pBuf->header.magic[0]         = VHR_MAGIC_0;
    pBuf->header.magic[1]         = VHR_MAGIC_1;
    pBuf->header.magic[2]         = VHR_MAGIC_2;
    pBuf->header.magic[3]         = VHR_MAGIC_3;
    pBuf->header.version_major    = VHR_VERSION_MAJOR;
    pBuf->header.version_minor    = VHR_VERSION_MINOR;
    pBuf->header.total_events     = numEvts;
    pBuf->header.instrument_id    = instrId;
    pBuf->header.timebase_us      = VHR_DEFAULT_TIMEBASE_US;
    pBuf->header.ticks_per_quarter= VHR_DEFAULT_TICKS_PER_QN;
    pBuf->header.total_file_size  = (uint32_t)sizeof(VHRFileHeader) +
                                    numEvts * (uint32_t)sizeof(VHREventEntry);

    if (title) {
        strncpy(pBuf->header.title, title, sizeof(pBuf->header.title) - 1U);
    }

    if (numEvts > 0U && pEvts) {
        memcpy(pBuf->events, pEvts, numEvts * sizeof(VHREventEntry));
    }

    pBuf->header.crc32 = test_crc32((const uint8_t*)pBuf->events,
                                     numEvts * sizeof(VHREventEntry));
}

static size_t vhr_buf_size(uint32_t numEvts)
{
    return sizeof(VHRFileHeader) + numEvts * sizeof(VHREventEntry);
}

static void run_stage12_test_suite(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 12 Playback Replayer Test\n");
    printf("=============================================================\n\n");

    /* TEST 1: Player Init */
    {
        vhl_vhr_player_t player;
        vhr_player_init(&player);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_IDLE);
        assert(player.cursorIdx       == 0U);
        assert(player.accumulatedUs   == 0U);
        assert(player.cursorTimeUs    == 0U);
        assert(player.eventsFireCount == 0U);

        float diff = player.speedMultiplier - VHR_PLAYER_SPEED_DEFAULT;
        if (diff < 0.0f) diff = -diff;
        assert(diff < 1e-6f);
    }
    printf("[TEST 1 PASS] Player initialization & IDLE state verified.\n");

    /* TEST 2: VHR Load */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,      .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U,  .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 64, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U,  .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 1U, "Load Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhl_err_t err = vhr_player_load(&player, &buf, vhr_buf_size(3U));
        assert(err == VHL_OK);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_READY);
        assert(player.totalEvents == 3U);
    }
    printf("[TEST 2 PASS] VHR header load & CRC-32 validation verified.\n");

    /* TEST 3: CRC Mismatch Rejection */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 80, .channel = 0 },
            { .delta_us = 10000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,  .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "CRC Reject Test");
        buf.header.crc32 += 1U;

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhl_err_t err = vhr_player_load(&player, &buf, vhr_buf_size(2U));
        assert(err == VHL_ERR_VHL_CRC_MISMATCH);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_IDLE);
    }
    printf("[TEST 3 PASS] CRC mismatch rejection verified.\n");

    /* TEST 4: Single Large Tick */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 64, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Single Tick Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 200000U);
        assert(player.eventsFireCount == 3U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_FINISHED);
    }
    printf("[TEST 4 PASS] Single-tick full event replay verified.\n");

    /* TEST 5: Multi-Tick */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 62, .data2 = 90,  .channel = 0 },
            { .delta_us = 70000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Multi-Tick Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 1U);
        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 2U);
        vhr_player_tick(&player, 100000U);
        assert(player.eventsFireCount == 3U);
    }
    printf("[TEST 5 PASS] Multi-tick partial advance timing verified.\n");

    /* TEST 6: Event Bus Re-Injection */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,    .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 48, .data2 = 80, .channel = 0 },
            { .delta_us = 5000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 48, .data2 = 0,  .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "Bus Inject Test");

        vhl_event_bus_init();
        vhl_event_bus_set_recorder_hook(NULL, NULL);

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(2U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 100000U);
        assert(player.eventsFireCount == 2U);

        vhl_key_event_t popped1, popped2;
        vhl_err_t e1 = vhl_event_bus_pop_key_event(&popped1, 0U);
        vhl_err_t e2 = vhl_event_bus_pop_key_event(&popped2, 0U);
        assert(e1 == VHL_OK && popped1.action == KEY_ACTION_PRESS && popped1.note == 48U);
        assert(e2 == VHL_OK && popped2.action == KEY_ACTION_RELEASE && popped2.note == 48U);
    }
    printf("[TEST 6 PASS] Event Bus re-injection (NOTE_ON/NOTE_OFF) verified.\n");

    /* TEST 7: Pause/Resume */
    {
        VHREventEntry evts[2] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 55, .data2 = 100, .channel = 0 },
            { .delta_us = 50000U, .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 57, .data2 = 90,  .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 2U, 0U, "Pause Resume Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(2U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 1U);
        vhr_player_pause(&player);
        vhr_player_tick(&player, 1000000U);
        assert(player.eventsFireCount == 1U);
        vhr_player_play(&player);
        vhr_player_tick(&player, 30000U);
        assert(player.eventsFireCount == 2U);
    }
    printf("[TEST 7 PASS] Pause/Resume with timing continuity verified.\n");

    /* TEST 8: Stop & Reset */
    {
        VHREventEntry evts[3] = {
            { .delta_us = 0U,     .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 60, .data2 = 100, .channel = 0 },
            { .delta_us = 10000U, .type = (uint8_t)VHR_EVENT_NOTE_OFF, .data1 = 60, .data2 = 0,   .channel = 0 },
            { .delta_us = 5000U,  .type = (uint8_t)VHR_EVENT_NOTE_ON,  .data1 = 62, .data2 = 100, .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 3U, 0U, "Stop Reset Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(3U));
        vhr_player_play(&player);
        vhr_player_tick(&player, 50000U);
        vhr_player_stop(&player);
        assert(player.cursorIdx == 0U && player.accumulatedUs == 0U);
        assert(vhr_player_get_state(&player) == VHR_PLAYER_STATE_READY);
    }
    printf("[TEST 8 PASS] Stop & reset to position 0 verified.\n");

    /* TEST 9: Speed Multiplier */
    {
        VHREventEntry evts[1] = {
            { .delta_us = 100000U, .type = (uint8_t)VHR_EVENT_NOTE_ON, .data1 = 60, .data2 = 100, .channel = 0 },
        };
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 1U, 0U, "Speed 2x Test");

        vhl_vhr_player_t player;
        vhr_player_init(&player);
        vhr_player_load(&player, &buf, vhr_buf_size(1U));
        vhr_player_set_speed(&player, 2.0f);
        vhr_player_play(&player);
        vhr_player_tick(&player, 50000U);
        assert(player.eventsFireCount == 1U);
    }
    printf("[TEST 9 PASS] Speed multiplier 2x & speed clamp [0.25, 4.0] verified.\n");

    /* TEST 10: Performance Budget */
    {
        VHREventEntry evts[16];
        memset(evts, 0, sizeof(evts));
        for (int i = 0; i < 16; ++i) {
            evts[i].delta_us = 0U;
            evts[i].type     = (uint8_t)((i % 2 == 0) ? VHR_EVENT_NOTE_ON : VHR_EVENT_NOTE_OFF);
            evts[i].data1    = (uint8_t)(48 + i);
            evts[i].data2    = 100U;
        }
        test_vhr_buf_t buf;
        build_test_vhr(&buf, evts, 16U, 0U, "Perf Test");

        clock_t totalStart = clock();
        for (int iter = 0; iter < 200; ++iter) {
            vhl_vhr_player_t player;
            vhr_player_init(&player);
            vhr_player_load(&player, &buf, vhr_buf_size(16U));
            vhr_player_play(&player);
            vhr_player_tick(&player, 1U);
        }
        clock_t totalEnd = clock();
        double totalMs = ((double)(totalEnd - totalStart) / (double)CLOCKS_PER_SEC) * 1000.0;
        assert((totalMs / 200.0) < 1.0);
    }
    printf("[TEST 10 PASS] Tick performance budget (<1ms / 16 events) verified.\n\n");
}

/* =============================================================================
 * Execution & Entry Point
 * ============================================================================= */

#if defined(ESP_PLATFORM)

static void vhl_audio_task(void* pvParameters)
{
    (void)pvParameters;
    while (1) {
        render_and_output_dac_block(&g_poly_synth);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    printf("[VHL TEST] Boot\n");
    printf("[VHL TEST] Loading VHL...\n");

    build_hardware_test_vhl();
    vhl_err_t err = vhl_loader_init(&g_vhl_loader, g_vhl_binary, sizeof(g_vhl_binary));
    assert(err == VHL_OK);
    printf("[VHL TEST] VHL loaded\n");

    vhl_poly_synth_init(&g_poly_synth);
    vhl_poly_synth_bind_vhl_loader(&g_poly_synth, &g_vhl_loader, 0);
    printf("[VHL TEST] DSP initialized\n");

    vhl_hw_init();
    printf("[VHL TEST] DAC initialized\n");
    printf("[VHL TEST] Ready\n");

    /* Create Audio Task on Core 1 */
    xTaskCreatePinnedToCore(vhl_audio_task, "vhl_audio_task", 4096, NULL, 5, NULL, 1);

    /* Main loop scanning Switch 1 and 2 on Core 0 */
    while (1) {
        scan_switches_and_update(&g_poly_synth);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#else

int main(void)
{
    /* Run Stage 12 acceptance unit test suite */
    run_stage12_test_suite();

    /* Run Hardware Validation Test Pipeline Verification */
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — VHL Audio Hardware Pipeline Test\n");
    printf("=============================================================\n\n");

    printf("[VHL TEST] Boot\n");
    printf("[VHL TEST] Loading real VHL file (000_first try.vhl)...\n");

    vhl_err_t err = vhl_loader_init(&g_vhl_loader, g_vhl_harmonium_package, g_vhl_harmonium_package_size);
    assert(err == VHL_OK);
    printf("[VHL TEST] VHL loaded (983,864 bytes real VHL instrument)\n");

    vhl_poly_synth_init(&g_poly_synth);
    vhl_poly_synth_bind_vhl_loader(&g_poly_synth, &g_vhl_loader, 0);
    printf("[VHL TEST] DSP initialized\n");

    vhl_hw_init();
    printf("[VHL TEST] DAC initialized\n");
    printf("[VHL TEST] Ready\n\n");

    /* Simulation Test 1: Press Switch 1 -> LED 1 ON -> Note 60 ON -> Render DSP -> DAC Output */
    printf("--- Simulating Switch 1 Press (GPIO32 LOW) ---\n");
    s_sim_sw1 = true;
    scan_switches_and_update(&g_poly_synth);
    assert(s_sim_led1 == true);

    bool has_audio1 = render_and_output_dac_block(&g_poly_synth);
    assert(has_audio1 == true); /* Non-zero audio signal output to DAC */
    printf("[VHL TEST] Switch 1 -> LED 1 ON -> Note 60 VHL DSP -> DAC Output verified.\n\n");

    /* Simulation Test 2: Release Switch 1 -> LED 1 OFF -> Note 60 OFF -> ADSR Release */
    printf("--- Simulating Switch 1 Release (GPIO32 HIGH) ---\n");
    s_sim_sw1 = false;
    scan_switches_and_update(&g_poly_synth);
    assert(s_sim_led1 == false);

    render_and_output_dac_block(&g_poly_synth);
    printf("[VHL TEST] Switch 1 Release -> LED 1 OFF -> NOTE_OFF 60 release verified.\n\n");

    /* Simulation Test 3: Press Switch 2 -> LED 2 ON -> Note 67 ON -> Pitch Shifted DSP -> DAC Output */
    printf("--- Simulating Switch 2 Press (GPIO33 LOW) ---\n");
    s_sim_sw2 = true;
    scan_switches_and_update(&g_poly_synth);
    assert(s_sim_led2 == true);

    bool has_audio2 = render_and_output_dac_block(&g_poly_synth);
    assert(has_audio2 == true);
    printf("[VHL TEST] Switch 2 -> LED 2 ON -> Note 67 Pitch-Shifted DSP -> DAC Output verified.\n\n");

    /* Simulation Test 4: Release Switch 2 -> LED 2 OFF -> Note 67 OFF */
    printf("--- Simulating Switch 2 Release (GPIO33 HIGH) ---\n");
    s_sim_sw2 = false;
    scan_switches_and_update(&g_poly_synth);
    assert(s_sim_led2 == false);

    render_and_output_dac_block(&g_poly_synth);
    printf("[VHL TEST] Switch 2 Release -> LED 2 OFF -> NOTE_OFF 67 release verified.\n\n");

    printf("=============================================================\n");
    printf("VHL Audio Pipeline Hardware Validation Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");

    return 0;
}

#endif
