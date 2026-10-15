/* =============================================================================
 * poly_synth.c — 16-Voice Polyphonic Synthesizer Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#include "poly_synth.h"
#include <math.h>
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
    pSynth->masterVolume      = 0.6f;
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
        vhl_adsr_init(&pSynth->voices[i].modEnvelope);
        vhl_lfo_init(&pSynth->voices[i].modLfo);
        vhl_lfo_init(&pSynth->voices[i].vibLfo);
        vhl_biquad_init(&pSynth->voices[i].filter);

        vhl_adsr_set_parameters(&pSynth->voices[i].envelope, &pSynth->currentInstParams);
        pSynth->voices[i].panLeft  = 0.7071f; // Constant-power center
        pSynth->voices[i].panRight = 0.7071f;

        // Bind Extension Objects & Pointers
        pSynth->voices[i].pFilterState     = &pSynth->voices[i].filter;
        pSynth->voices[i].pModulationState = &pSynth->voices[i].modLfo;
        pSynth->voices[i].pLayerState      = &pSynth->voices[i].vibLfo;
    }

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_bind_vhl_loader(vhl_poly_synth_t* pSynth, const vhl_loader_t* pLoader, uint8_t instrumentId)
{
    if (!pSynth || !pLoader) {
        return VHL_ERR_INVALID_ARG;
    }

    pSynth->pActiveLoader      = pLoader;
    pSynth->activeInstrumentId = instrumentId;
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

vhl_err_t vhl_poly_synth_choke_exclusive_class(vhl_poly_synth_t* pSynth, uint8_t exclusiveClass, uint8_t channel, uint8_t* outChokedCount)
{
    if (!pSynth || !outChokedCount) {
        return VHL_ERR_INVALID_ARG;
    }

    *outChokedCount = 0;
    if (exclusiveClass == 0) {
        return VHL_OK;
    }

    for (size_t i = 0; i < VHL_MAX_POLYPHONY; ++i) {
        if (pSynth->voices[i].channel == channel &&
            pSynth->voices[i].exclusiveClass == exclusiveClass &&
            pSynth->voices[i].voiceEngine.state != VHL_VOICE_STATE_STOPPED) {
            
            pSynth->voices[i].runtimeConfig.isChoked = true;
            vhl_adsr_gate(&pSynth->voices[i].envelope, false);
            vhl_adsr_gate(&pSynth->voices[i].modEnvelope, false);
            (*outChokedCount)++;
        }
    }

    return VHL_OK;
}

vhl_err_t vhl_poly_synth_note_on(vhl_poly_synth_t* pSynth, vhl_midi_note_t note, vhl_midi_vel_t velocity)
{
    if (!pSynth) {
        return VHL_ERR_INVALID_ARG;
    }

    // O(1) Accelerated VHL Region Lookup (if loader active)
    if (pSynth->pActiveLoader && pSynth->pActiveLoader->isLoaded) {
        const VHLRegion* matchedRegions[VHL_MAX_MATCHED_REGIONS];
        uint8_t regionCount = 0;

        vhl_loader_get_regions_for_key(pSynth->pActiveLoader,
                                      pSynth->activeInstrumentId,
                                      note,
                                      velocity,
                                      matchedRegions,
                                      &regionCount);

        for (uint8_t r = 0; r < regionCount; ++r) {
            const VHLRegion* pReg = matchedRegions[r];

            // Exclusive class choke scan
            if (pReg->exclusive_class > 0) {
                uint8_t choked = 0;
                vhl_poly_synth_choke_exclusive_class(pSynth, pReg->exclusive_class, 0, &choked);
            }

            vhl_poly_voice_t* pVoice = allocate_voice(pSynth, note);
            pVoice->activeNote     = note;
            pVoice->activeVelocity = velocity;
            pVoice->channel        = 0;
            pVoice->exclusiveClass = pReg->exclusive_class;
            pVoice->ageCounter     = ++pSynth->voiceAllocCounter;

            const VHLSampleEntry* pSmpl = NULL;
            vhl_loader_get_sample(pSynth->pActiveLoader, pReg->sample_id, &pSmpl);

            // Complete Parameter Ownership Decoding
            vhl_decode_region_to_runtime(pReg, pSmpl, &pVoice->runtimeConfig);

            // Configure ADSR Envelopes
            vhl_instrument_params_t volParams = {
                .attackMs     = (float)pVoice->runtimeConfig.volEnvConfig.attackMs,
                .decayMs      = (float)pVoice->runtimeConfig.volEnvConfig.decayMs,
                .sustainLevel = pVoice->runtimeConfig.volEnvConfig.sustainGain,
                .releaseMs    = (float)pVoice->runtimeConfig.volEnvConfig.releaseMs,
                .masterGain   = 1.0f
            };
            vhl_adsr_set_parameters(&pVoice->envelope, &volParams);

            vhl_instrument_params_t modParams = {
                .attackMs     = (float)pVoice->runtimeConfig.modEnvConfig.attackMs,
                .decayMs      = (float)pVoice->runtimeConfig.modEnvConfig.decayMs,
                .sustainLevel = pVoice->runtimeConfig.modEnvConfig.sustainGain,
                .releaseMs    = (float)pVoice->runtimeConfig.modEnvConfig.releaseMs,
                .masterGain   = 1.0f
            };
            vhl_adsr_set_parameters(&pVoice->modEnvelope, &modParams);

            // Configure Dual LFO Engine
            vhl_lfo_configure(&pVoice->modLfo,
                              pVoice->runtimeConfig.modLfoConfig.delayMs,
                              pVoice->runtimeConfig.modLfoConfig.freqHz,
                              pVoice->runtimeConfig.modLfoConfig.depthPitchCents,
                              pVoice->runtimeConfig.modLfoConfig.depthFilterCents,
                              pVoice->runtimeConfig.modLfoConfig.depthVolumeDb);

            vhl_lfo_configure(&pVoice->vibLfo,
                              pVoice->runtimeConfig.vibLfoConfig.delayMs,
                              pVoice->runtimeConfig.vibLfoConfig.freqHz,
                              pVoice->runtimeConfig.vibLfoConfig.depthPitchCents,
                              0.0f, 0.0f);

            // Configure Biquad Filter Engine
            vhl_biquad_reset(&pVoice->filter);
            vhl_biquad_configure(&pVoice->filter,
                                 pVoice->runtimeConfig.filterConfig.cutoffHz,
                                 pVoice->runtimeConfig.filterConfig.resonanceDb,
                                 44100.0f);

            pVoice->panLeft  = pVoice->runtimeConfig.panLeft;
            pVoice->panRight = pVoice->runtimeConfig.panRight;

            if (pSmpl) {
                const int16_t* pPcm = (const int16_t*)(pSynth->pActiveLoader->pPcmBase + pSmpl->data_offset);
                vhl_single_voice_bind_sample(&pVoice->voiceEngine,
                                             pPcm,
                                             pSmpl->total_frames,
                                             pSmpl->loop_start_frame,
                                             pSmpl->loop_end_frame,
                                             pReg->loop_mode != 0,
                                             pSmpl->root_key);
            }

            vhl_single_voice_note_on(&pVoice->voiceEngine, note, velocity);
            vhl_adsr_gate(&pVoice->envelope, true);
            vhl_adsr_gate(&pVoice->modEnvelope, true);
            vhl_lfo_reset(&pVoice->modLfo);
            vhl_lfo_reset(&pVoice->vibLfo);
        }
        return VHL_OK;
    }

    // Default Fallback Note-On
    vhl_poly_voice_t* pVoice = allocate_voice(pSynth, note);
    pVoice->activeNote     = note;
    pVoice->activeVelocity = velocity;
    pVoice->ageCounter     = ++pSynth->voiceAllocCounter;

    vhl_single_voice_note_on(&pVoice->voiceEngine, note, velocity);
    vhl_adsr_gate(&pVoice->envelope, true);
    vhl_adsr_gate(&pVoice->modEnvelope, true);

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
            vhl_adsr_gate(&pSynth->voices[i].modEnvelope, false);
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

    // 1. Zero stereo output buffer
    memset(pOutBuf->left, 0, frameCount * sizeof(vhl_sample_t));
    memset(pOutBuf->right, 0, frameCount * sizeof(vhl_sample_t));

    uint32_t activeCount = 0;
    uint32_t blockMs = (uint32_t)(((float)frameCount / 44100.0f) * 1000.0f);
    if (blockMs == 0) blockMs = 5; // Default ~5.8ms block duration

    for (size_t v = 0; v < VHL_MAX_POLYPHONY; ++v) {
        vhl_poly_voice_t* pVoice = &pSynth->voices[v];

        if (pVoice->voiceEngine.state == VHL_VOICE_STATE_STOPPED ||
            pVoice->envelope.state == ADSR_STATE_IDLE) {
            continue;
        }

        activeCount++;

        // ---------------------------------------------------------------------
        // MANDATORY STAGE 10 DSP PIPELINE EXECUTION (Per Render Block)
        // ---------------------------------------------------------------------

        // 1. Step Envelope Generators
        float envVol = vhl_adsr_process_sample(&pVoice->envelope);
        float envMod = vhl_adsr_process_sample(&pVoice->modEnvelope);

        // 2. Step Dual LFO Engine
        float lfoMod = vhl_lfo_process_block(&pVoice->modLfo, blockMs, 44100.0f);
        float lfoVib = vhl_lfo_process_block(&pVoice->vibLfo, blockMs, 44100.0f);

        // 3. Dynamic Pitch Engine (Recalculate Pitch Ratio per block)
        float deltaPitchCents = (float)((int16_t)pVoice->activeNote - (int16_t)pVoice->voiceEngine.rootKey) * 100.0f;
        if (pVoice->runtimeConfig.scaleTuning > 0) {
            deltaPitchCents = (float)((int16_t)pVoice->activeNote - (int16_t)pVoice->voiceEngine.rootKey) * (float)pVoice->runtimeConfig.scaleTuning;
        }
        deltaPitchCents += (float)pVoice->runtimeConfig.coarseTune * 100.0f;
        deltaPitchCents += pVoice->runtimeConfig.fineTuneCents;

        // Add Mod Env & LFO Pitch Modulations
        deltaPitchCents += pVoice->runtimeConfig.modEnvConfig.depthPitchCents * envMod;
        deltaPitchCents += pVoice->runtimeConfig.modLfoConfig.depthPitchCents * lfoMod;
        deltaPitchCents += pVoice->runtimeConfig.vibLfoConfig.depthPitchCents * lfoVib;

        pVoice->voiceEngine.pitchRatio = powf(2.0f, deltaPitchCents / 1200.0f);

        // 4. Dynamic Resonant Biquad Filter Engine (Recalculate Cutoff per block)
        float filterCutoffHz = pVoice->runtimeConfig.filterConfig.cutoffHz;
        if (filterCutoffHz > 0.0f) {
            float deltaFilterCents = pVoice->runtimeConfig.modEnvConfig.depthFilterCents * envMod +
                                     pVoice->runtimeConfig.modLfoConfig.depthFilterCents * lfoMod;
            if (fabsf(deltaFilterCents) > 0.01f) {
                filterCutoffHz *= powf(2.0f, deltaFilterCents / 1200.0f);
            }
            vhl_biquad_configure(&pVoice->filter, filterCutoffHz, pVoice->runtimeConfig.filterConfig.resonanceDb, 44100.0f);
        }

        // 5. Dynamic Gain Scaling Engine
        float velGain = (float)pVoice->activeVelocity / 127.0f;
        float baseGain = (pVoice->runtimeConfig.attenuationGain > 0.0f) ? pVoice->runtimeConfig.attenuationGain : 1.0f;
        
        // Add Mod LFO Volume Attenuation
        if (pVoice->runtimeConfig.modLfoConfig.depthVolumeDb != 0.0f) {
            float volModDb = pVoice->runtimeConfig.modLfoConfig.depthVolumeDb * lfoMod;
            baseGain *= powf(10.0f, -volModDb / 20.0f);
        }

        float voiceTotalGain = envVol * velGain * baseGain * pSynth->masterVolume * pSynth->currentInstParams.masterGain;

        // 6. Frame-by-Frame Sample Resampling, Filtering, Panning & Accumulation
        vhl_sample_t singleL[VHL_DMA_BUFFER_FRAMES];
        vhl_sample_t singleR[VHL_DMA_BUFFER_FRAMES];
        vhl_audio_buffer_t singleBuf = { .left = singleL, .right = singleR, .frameCount = frameCount };
        memset(singleL, 0, frameCount * sizeof(vhl_sample_t));
        memset(singleR, 0, frameCount * sizeof(vhl_sample_t));

        vhl_single_voice_render(&pVoice->voiceEngine, &singleBuf, frameCount);

        for (size_t i = 0; i < frameCount; ++i) {
            float sampleResampled = singleL[i];
            float sampleFiltered  = vhl_biquad_process_sample(&pVoice->filter, sampleResampled);

            pOutBuf->left[i]  += sampleFiltered * voiceTotalGain * pVoice->panLeft;
            pOutBuf->right[i] += sampleFiltered * voiceTotalGain * pVoice->panRight;
        }

        if (envVol <= 0.0001f && pVoice->envelope.state == ADSR_STATE_IDLE) {
            pVoice->voiceEngine.state = VHL_VOICE_STATE_STOPPED;
        }
    }

    pSynth->activeVoiceCount = activeCount;

    // -------------------------------------------------------------------------
    // MILESTONE 10.9: LIGHTWEIGHT SOFT LIMITER
    // Prevents digital clipping while preserving musical dynamics
    // Output formula: y = x / (1.0 + |x|)
    // -------------------------------------------------------------------------
    for (size_t i = 0; i < frameCount; ++i) {
        float inL = pOutBuf->left[i];
        float inR = pOutBuf->right[i];

        pOutBuf->left[i]  = inL / (1.0f + fabsf(inL));
        pOutBuf->right[i] = inR / (1.0f + fabsf(inR));
    }

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
