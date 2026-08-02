/* =============================================================================
 * main.c — System Entry Point & Stage 6 Audio HAL Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 6 — Audio HAL & Dual-Buffer I²S DMA Driver
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"
#include "task_manager.h"
#include "event_bus.h"
#include "i2s_audio_driver.h"

static float s_test_sine_phase = 0.0f;

/* External Render Callback Registered with HAL (Simulates Synthesis Engine) */
static void test_sine_render_callback(vhl_audio_buffer_t* pBuf, size_t frameCount)
{
    float phaseInc = (2.0f * 3.14159265f * 440.0f) / (float)VHL_SAMPLE_RATE;

    for (size_t i = 0; i < frameCount; ++i) {
        // Generate 440 Hz sine wave
        float sample = sinf(s_test_sine_phase);
        pBuf->left[i]  = sample;
        pBuf->right[i] = sample;

        s_test_sine_phase += phaseInc;
        if (s_test_sine_phase >= (2.0f * 3.14159265f)) {
            s_test_sine_phase -= (2.0f * 3.14159265f);
        }
    }
}

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 6 Audio HAL Test\n");
    printf("=============================================================\n");

    // Initialize System Runtime & Audio HAL
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);

    err = vhl_hal_i2s_init();
    assert(err == VHL_OK);
    printf("[TEST 1 PASS] Audio HAL & I2S DMA Dual-Buffer Engine initialized cleanly.\n");

    // Test 2: Register External Render Callback (Generic Callback Contract)
    err = vhl_hal_i2s_register_render_callback(test_sine_render_callback);
    assert(err == VHL_OK);
    printf("[TEST 2 PASS] Generic Audio Render Callback registered cleanly.\n");

    // Test 3: Start Audio Driver and Process 1,000 DMA Blocks (11.6 seconds simulated playback)
    err = vhl_hal_i2s_start();
    assert(err == VHL_OK);

    uint32_t simulatedTimeUs = 1000;
    for (int block = 0; block < 1000; ++block) {
        err = vhl_hal_i2s_process_block(simulatedTimeUs);
        assert(err == VHL_OK);
        simulatedTimeUs += 11610; // Advance 11.61 ms per block
    }

    err = vhl_hal_i2s_stop();
    assert(err == VHL_OK);

    printf("[TEST 3 PASS] 1,000 DMA blocks (11.61 ms each) processed cleanly via render callback.\n");

    // Test 4: DMA Performance & Timing Instrumentation Audit
    vhl_i2s_stats_t stats;
    err = vhl_hal_i2s_get_stats(&stats);
    assert(err == VHL_OK);

    assert(stats.dmaBlocksTransmitted == 1000);
    assert(stats.underrunCount == 0);
    assert(stats.deadlineMisses == 0);
    assert(stats.renderTimeUs > 0);
    assert(stats.maxRenderTimeUs > 0);
    assert(stats.cpuLoadPercent > 0.0f && stats.cpuLoadPercent < 100.0f);

    printf("[TEST 4 PASS] Real-time DMA timing stats audited: 1,000 blocks sent, 0 underruns, CPU Load: %.1f%%.\n", stats.cpuLoadPercent);

    printf("=============================================================\n");
    printf("Stage 6 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
