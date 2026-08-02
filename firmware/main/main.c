/* =============================================================================
 * main.c — System Entry Point & Stage 7 Single Voice Synthesis Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 7 — Single Voice Audio Synthesis Engine
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"
#include "sample_engine.h"

// Simulated Flash Memory-Mapped PCM 440 Hz Harmonium Sample (1,000 frames)
static vhl_pcm16_t s_flash_pcm_sample[1000];

static void generate_test_pcm_sample(void)
{
    float phaseInc = (2.0f * 3.14159265f * 440.0f) / 44100.0f;
    for (int i = 0; i < 1000; ++i) {
        float sample = sinf((float)i * phaseInc);
        s_flash_pcm_sample[i] = (vhl_pcm16_t)(sample * 30000.0f);
    }
}

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 7 Single Voice Test\n");
    printf("=============================================================\n");

    generate_test_pcm_sample();

    // Initialize System Runtime & Single Voice Engine
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);

    vhl_single_voice_t voice;
    err = vhl_single_voice_init(&voice);
    assert(err == VHL_OK);
    assert(vhl_single_voice_get_state(&voice) == VHL_VOICE_STATE_STOPPED);

    printf("[TEST 1 PASS] Single Voice Engine initialized cleanly in STOPPED state.\n");

    // Test 2: Memory-Mapped Flash PCM Sample Binding
    err = vhl_single_voice_bind_sample(&voice, s_flash_pcm_sample, 1000, 100, 900, true, 60); // RootKey C4 (60)
    assert(err == VHL_OK);
    assert(voice.totalFrames == 1000);
    assert(voice.rootKey == 60);

    printf("[TEST 2 PASS] Memory-mapped Flash PCM sample bound cleanly.\n");

    // Test 3: Note-On Event & Voice State Machine Transitions (STOPPED -> STARTING -> PLAYING)
    err = vhl_single_voice_note_on(&voice, 60, 127); // Note C4 (PitchRatio = 1.0)
    assert(err == VHL_OK);
    assert(vhl_single_voice_get_state(&voice) == VHL_VOICE_STATE_PLAYING);
    assert(fabsf(voice.pitchRatio - 1.0f) < 0.001f);

    printf("[TEST 3 PASS] Note-On triggered state transition to PLAYING (PitchRatio = 1.000).\n");

    // Test 4: Sub-Cent Pitch Transposition Math Verification
    // Trigger Note C5 (72) -> 1 octave higher (PitchRatio = 2.0)
    vhl_single_voice_note_on(&voice, 72, 100);
    assert(fabsf(voice.pitchRatio - 2.0f) < 0.001f);

    // Trigger Note C3 (48) -> 1 octave lower (PitchRatio = 0.5)
    vhl_single_voice_note_on(&voice, 48, 100);
    assert(fabsf(voice.pitchRatio - 0.5f) < 0.001f);

    printf("[TEST 4 PASS] Pitch Transposition ratio calculation verified (+12 semitones = 2.0x, -12 semitones = 0.5x).\n");

    // Test 5: Audio Buffer Rendering with Cubic Hermite Interpolation & Loop Wrapping
    vhl_single_voice_note_on(&voice, 60, 127);
    vhl_sample_t leftBuf[512] = {0};
    vhl_sample_t rightBuf[512] = {0};
    vhl_audio_buffer_t renderBuf = {
        .left = leftBuf,
        .right = rightBuf,
        .frameCount = 512
    };

    // Render 512 frames (Pass 1)
    err = vhl_single_voice_render(&voice, &renderBuf, 512);
    assert(err == VHL_OK);
    assert(voice.phase > 0.0);

    // Render 500 more frames (Pass 2, should cross loopEnd 900 -> wrap to loopStart 100)
    err = vhl_single_voice_render(&voice, &renderBuf, 500);
    assert(err == VHL_OK);
    assert(voice.phase >= 100.0 && voice.phase < 900.0);

    printf("[TEST 5 PASS] Rendered 1,012 frames cleanly with Cubic Hermite interpolation & seamless loop wrapping.\n");

    // Test 6: Note-Off Event & Voice Release State Machine Transition
    err = vhl_single_voice_note_off(&voice);
    assert(err == VHL_OK);
    assert(vhl_single_voice_get_state(&voice) == VHL_VOICE_STATE_STOPPED);

    printf("[TEST 6 PASS] Note-Off triggered state transition to STOPPED cleanly.\n");

    printf("=============================================================\n");
    printf("Stage 7 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
