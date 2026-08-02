/* =============================================================================
 * main.c — System Entry Point & Stage 8 Polyphonic Synthesizer Unit Test Harness
 * Project: Digital Harmonium Workstation Firmware
 * Stage 8 — 16-Voice Polyphonic Synthesizer & ADSR Envelope Generator
 * ============================================================================= */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "system_manager.h"
#include "sample_engine.h"
#include "adsr_envelope.h"
#include "poly_synth.h"

static vhl_pcm16_t s_harmonium_pcm_sample[1000];

static void generate_test_harmonium_sample(void)
{
    float phaseInc = (2.0f * 3.14159265f * 440.0f) / 44100.0f;
    for (int i = 0; i < 1000; ++i) {
        float sample = sinf((float)i * phaseInc);
        s_harmonium_pcm_sample[i] = (vhl_pcm16_t)(sample * 30000.0f);
    }
}

int main(void)
{
    printf("=============================================================\n");
    printf("Digital Harmonium Workstation — Stage 8 Polyphonic Synth Test\n");
    printf("=============================================================\n");

    generate_test_harmonium_sample();

    // Initialize System Runtime & Polyphonic Synthesizer Engine
    vhl_err_t err = vhl_system_init();
    assert(err == VHL_OK);

    vhl_poly_synth_t synth;
    err = vhl_poly_synth_init(&synth);
    assert(err == VHL_OK);
    assert(synth.activeVoiceCount == 0);
    assert(synth.masterVolume == 0.8f);

    printf("[TEST 1 PASS] 16-Voice Polyphonic Synthesizer initialized cleanly.\n");

    // Test 2: Instrument-Driven ADSR Parameter Configuration
    vhl_instrument_params_t instParams = {
        .attackMs = 15.0f,
        .decayMs = 80.0f,
        .sustainLevel = 0.7f,
        .releaseMs = 150.0f,
        .masterGain = 0.9f
    };
    err = vhl_poly_synth_set_instrument_params(&synth, &instParams);
    assert(err == VHL_OK);
    assert(synth.currentInstParams.attackMs == 15.0f);
    assert(synth.currentInstParams.sustainLevel == 0.7f);

    printf("[TEST 2 PASS] VHL Instrument-Driven ADSR envelope parameters configured cleanly.\n");

    // Test 3: Sample Binding to 16 Voices & Extensible Struct Pointer Audit
    err = vhl_poly_synth_bind_sample(&synth, s_harmonium_pcm_sample, 1000, 100, 900, true, 60);
    assert(err == VHL_OK);

    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        assert(synth.voices[i].voiceEngine.pSampleData == s_harmonium_pcm_sample);
        // Assert reserved extensible slots are NULL (ready for Stage 9 biquad filter & LFOs)
        assert(synth.voices[i].pFilterState == NULL);
        assert(synth.voices[i].pModulationState == NULL);
        assert(synth.voices[i].pLayerState == NULL);
    }

    printf("[TEST 3 PASS] Sample bound to all 16 voices & reserved extension slots audited cleanly.\n");

    // Test 4: 3-Note Chord Harmony Rendering (C4, E4, G4 -> Notes 60, 64, 67)
    err = vhl_poly_synth_note_on(&synth, 60, 127); assert(err == VHL_OK);
    err = vhl_poly_synth_note_on(&synth, 64, 127); assert(err == VHL_OK);
    err = vhl_poly_synth_note_on(&synth, 67, 127); assert(err == VHL_OK);

    vhl_sample_t leftBuf[512] = {0};
    vhl_sample_t rightBuf[512] = {0};
    vhl_audio_buffer_t outBuf = { .left = leftBuf, .right = rightBuf, .frameCount = 512 };

    err = vhl_poly_synth_render(&synth, &outBuf, 512);
    assert(err == VHL_OK);
    assert(synth.activeVoiceCount == 3);
    assert(outBuf.left[256] != 0.0f);

    printf("[TEST 4 PASS] 3-Note Polyphonic Chord (C-E-G) rendered cleanly into stereo float mixer.\n");

    // Test 5: LRU & Quietest Voice Stealing Engine (Trigger 24 simultaneous Note-Ons)
    for (uint8_t note = 48; note < 72; ++note) { // 24 notes
        err = vhl_poly_synth_note_on(&synth, note, 100);
        assert(err == VHL_OK);
    }

    // Verify 16-voice hard limit is maintained gracefully
    err = vhl_poly_synth_render(&synth, &outBuf, 512);
    assert(err == VHL_OK);
    assert(synth.activeVoiceCount <= VHL_MAX_POLYPHONY);

    printf("[TEST 5 PASS] LRU Voice Stealing Engine handled 24 simultaneous Note-Ons with 0 crashes.\n");

    // Test 6: Polyphonic Note-Off & ADSR Release Transition
    for (uint8_t note = 48; note < 72; ++note) {
        vhl_poly_synth_note_off(&synth, note);
    }

    // Render remaining release tail
    for (int pass = 0; pass < 20; ++pass) {
        vhl_poly_synth_render(&synth, &outBuf, 512);
    }
    assert(synth.activeVoiceCount == 0);

    printf("[TEST 6 PASS] Polyphonic Note-Off released all voices cleanly.\n");

    printf("=============================================================\n");
    printf("Stage 8 Acceptance Gate Passed 100%% Cleanly.\n");
    printf("=============================================================\n");
    return 0;
}

void app_main(void)
{
    main();
}
