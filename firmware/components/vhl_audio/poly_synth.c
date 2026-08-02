/* =============================================================================
 * poly_synth.c — 16-Voice Polyphonic Synthesizer Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 8 — 16-Voice Polyphonic Synthesizer Architecture
 * ============================================================================= */

#include "poly_synth.h"
#include <string.h>

/* -----------------------------------------------------------------------------
 * Voice Allocation & Stealing Engine (LRU + Quietest)
 * ----------------------------------------------------------------------------- */

static vhl_poly_voice_t* allocate_voice(vhl_poly_synth_t* pSynth, vhl_midi_note_t note)
{
    // Priority 1: Check if this exact note is already playing (re-trigger)
    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].activeNote == note && 
            pSynth->voices[i].voiceEngine.state != VHL_VOICE_STATE_STOPPED) {
            return &pSynth->voices[i];
        }
    }

    // Priority 2: Find a stopped/idle voice slot
    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].voiceEngine.state == VHL_VOICE_STATE_STOPPED ||
            pSynth->voices[i].envelope.state == ADSR_STATE_IDLE) {
            return &pSynth->voices[i];
        }
    }

    // Priority 3: Find releasing voice with lowest envelope level
    float minLevel = 1.0f;
    vhl_poly_voice_t* pBestReleaseVoice = NULL;
    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].envelope.state == ADSR_STATE_RELEASE) {
            if (pSynth->voices[i].envelope.currentLevel < minLevel) {
                minLevel = pSynth->voices[i].envelope.currentLevel;
                pBestReleaseVoice = &pSynth->voices[i];
            }
        }
    }
    if (pBestReleaseVoice) {
        return pBestReleaseVoice;
    }

    // Priority 4 (Steal): All 16 voices active -> Steal oldest voice (LRU)
    uint32_t oldestAge = UINT32_MAX;
    vhl_poly_voice_t* pOldestVoice = &pSynth->voices[0];
    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].ageCounter < oldestAge) {
            oldestAge = pSynth->voices[i].ageCounter;
            pOldestVoice = &pSynth->voices[i];
        }
    }

    return pOldestVoice;
}

/* -----------------------------------------------------------------------------
 * Public Polyphonic Synthesizer API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_poly_synth_init(vhl_poly_synth_t* pSynth)
{
    if (!pSynth) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pSynth, 0, sizeof(vhl_poly_synth_t));
    pSynth->masterVolume      = 0.8f;
    pSynth->voiceAllocCounter = 1;

    // Default VHL Instrument Params
    pSynth->currentInstParams.attackMs     = 10.0f;
    pSynth->currentInstParams.decayMs      = 100.0f;
    pSynth->currentInstParams.sustainLevel = 0.8f;
    pSynth->currentInstParams.releaseMs    = 200.0f;
    pSynth->currentInstParams.masterGain   = 1.0f;

    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        vhl_single_voice_init(&pSynth->voices[i].voiceEngine);
        vhl_adsr_init(&pSynth->voices[i].envelope);
        vhl_adsr_set_parameters(&pSynth->voices[i].envelope, &pSynth->currentInstParams);
        pSynth->voices[i].panLeft  = 1.0f;
        pSynth->voices[i].panRight = 1.0f;
    }

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_set_instrument_params(vhl_poly_synth_t* pSynth, const vhl_instrument_params_t* pParams)
{
    if (!pSynth || !pParams) {
        return VHL_ERR_INVALID_ARG;
    }

    pSynth->currentInstParams = *pParams;
    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        vhl_adsr_set_parameters(&pSynth->voices[i].envelope, pParams);
    }
    return VHL_OK;
}

vhl_err_t vhl_poly_synth_bind_sample(vhl_poly_synth_t* pSynth,
                                     const vhl_pcm16_t* pData,
                                     uint32_t totalFrames,
                                     uint32_t loopStart,
                                     uint32_t loopEnd,
                                     bool isLooped,
                                     vhl_midi_note_t rootKey)
{
    if (!pSynth || !pData) {
        return VHL_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        vhl_single_voice_bind_sample(&pSynth->voices[i].voiceEngine, pData, totalFrames, loopStart, loopEnd, isLooped, rootKey);
    }

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_note_on(vhl_poly_synth_t* pSynth, vhl_midi_note_t note, vhl_midi_vel_t velocity)
{
    if (!pSynth) {
        return VHL_ERR_INVALID_ARG;
    }

    vhl_poly_voice_t* pVoice = allocate_voice(pSynth, note);
    pVoice->activeNote     = note;
    pVoice->activeVelocity = velocity;
    pVoice->ageCounter     = ++pSynth->voiceAllocCounter;

    vhl_single_voice_note_on(&pVoice->voiceEngine, note, velocity);
    vhl_adsr_gate(&pVoice->envelope, true);

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_note_off(vhl_poly_synth_t* pSynth, vhl_midi_note_t note)
{
    if (!pSynth) {
        return VHL_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].activeNote == note && 
            pSynth->voices[i].voiceEngine.state != VHL_VOICE_STATE_STOPPED) {
            vhl_adsr_gate(&pSynth->voices[i].envelope, false);
            vhl_single_voice_note_off(&pSynth->voices[i].voiceEngine);
        }
    }

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_render(vhl_poly_synth_t* pSynth, vhl_audio_buffer_t* pOutBuf, size_t frameCount)
{
    if (!pSynth || !pOutBuf) {
        return VHL_ERR_INVALID_ARG;
    }

    // Zero output buffer
    memset(pOutBuf->left, 0, frameCount * sizeof(vhl_sample_t));
    memset(pOutBuf->right, 0, frameCount * sizeof(vhl_sample_t));

    uint32_t activeCount = 0;

    for (size_t v = 0; v < VHL_MAX_POLYPHONY; ++v) {
        vhl_poly_voice_t* pVoice = &pSynth->voices[v];

        if (pVoice->voiceEngine.state == VHL_VOICE_STATE_STOPPED ||
            pVoice->envelope.state == ADSR_STATE_IDLE) {
            continue;
        }

        activeCount++;

        // Render voice samples frame-by-frame applying ADSR envelope
        for (size_t i = 0; i < frameCount; ++i) {
            float envLevel = vhl_adsr_process_sample(&pVoice->envelope);
            if (envLevel <= 0.0f && pVoice->envelope.state == ADSR_STATE_IDLE) {
                pVoice->voiceEngine.state = VHL_VOICE_STATE_STOPPED;
                break;
            }

            // Temp single-frame buffer
            vhl_sample_t lFrame = 0.0f;
            vhl_sample_t rFrame = 0.0f;
            vhl_audio_buffer_t singleBuf = { .left = &lFrame, .right = &rFrame, .frameCount = 1 };

            vhl_single_voice_render(&pVoice->voiceEngine, &singleBuf, 1);

            float masterScale = pSynth->masterVolume * pSynth->currentInstParams.masterGain;
            pOutBuf->left[i]  += lFrame * envLevel * pVoice->panLeft * masterScale;
            pOutBuf->right[i] += rFrame * envLevel * pVoice->panRight * masterScale;
        }
    }

    pSynth->activeVoiceCount = activeCount;
    return VHL_OK;
}

vhl_err_t vhl_poly_synth_set_master_volume(vhl_poly_synth_t* pSynth, float volume)
{
    if (!pSynth) {
        return VHL_ERR_INVALID_ARG;
    }
    pSynth->masterVolume = (volume >= 0.0f && volume <= 1.0f) ? volume : 0.8f;
    return VHL_OK;
}
