/* =============================================================================
 * sample_engine.c — Single Voice Synthesis Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 7 — Single Voice Audio Synthesis Engine
 * ============================================================================= */

#include "sample_engine.h"
#include <math.h>
#include <string.h>

/* -----------------------------------------------------------------------------
 * Interpolation Math Helper Functions
 * ----------------------------------------------------------------------------- */

static inline float interpolate_linear(float y0, float y1, float frac)
{
    return y0 + frac * (y1 - y0);
}

static inline float interpolate_cubic(float y0, float y1, float y2, float y3, float frac)
{
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

/* Helper to safely fetch Flash PCM sample at given frame index */
static inline float fetch_sample(const vhl_pcm16_t* pData, int32_t idx, uint32_t totalFrames)
{
    if (!pData || totalFrames == 0) return 0.0f;
    if (idx < 0) idx = 0;
    if ((uint32_t)idx >= totalFrames) idx = (int32_t)totalFrames - 1;

    // Convert 16-bit PCM integer (-32768..+32767) to Float (-1.0f..+1.0f)
    return (float)pData[idx] / 32767.0f;
}

/* -----------------------------------------------------------------------------
 * Public Single Voice API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_single_voice_init(vhl_single_voice_t* pVoice)
{
    if (!pVoice) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pVoice, 0, sizeof(vhl_single_voice_t));
    pVoice->state      = VHL_VOICE_STATE_STOPPED;
    pVoice->interpMode = INTERPOLATION_MODE_CUBIC;
    pVoice->pitchRatio = 1.0f;
    pVoice->rootKey    = 62; // D4

    return VHL_OK;
}

vhl_err_t vhl_single_voice_bind_sample(vhl_single_voice_t* pVoice,
                                       const vhl_pcm16_t* pData,
                                       uint32_t totalFrames,
                                       uint32_t loopStart,
                                       uint32_t loopEnd,
                                       bool isLooped,
                                       vhl_midi_note_t rootKey)
{
    if (!pVoice || !pData || totalFrames == 0) {
        return VHL_ERR_INVALID_ARG;
    }

    pVoice->pSampleData     = pData;
    pVoice->totalFrames     = totalFrames;
    pVoice->loopStartFrame  = loopStart;
    pVoice->loopEndFrame    = (loopEnd > 0 && loopEnd <= totalFrames) ? loopEnd : totalFrames;
    pVoice->isLooped        = isLooped;
    pVoice->rootKey         = (rootKey > 0) ? rootKey : 62; // Default to D4
    pVoice->phase           = 0.0;
    pVoice->state           = VHL_VOICE_STATE_STOPPED;

    return VHL_OK;
}

vhl_err_t vhl_single_voice_note_on(vhl_single_voice_t* pVoice,
                                   vhl_midi_note_t note,
                                   vhl_midi_vel_t velocity)
{
    if (!pVoice || !pVoice->pSampleData) {
        return VHL_ERR_INVALID_STATE;
    }

    // State Transition: STOPPED/FINISHED -> STARTING -> PLAYING
    pVoice->state = VHL_VOICE_STATE_STARTING;
    pVoice->velocity = velocity;
    pVoice->phase = 0.0;

    // Calculate Pitch Ratio = 2^((note - rootKey + fineTune/100) / 12)
    float noteDiff = (float)((int16_t)note - (int16_t)pVoice->rootKey) + ((float)pVoice->fineTuneCents / 100.0f);
    pVoice->pitchRatio = powf(2.0f, noteDiff / 12.0f);

    pVoice->state = VHL_VOICE_STATE_PLAYING;
    return VHL_OK;
}

vhl_err_t vhl_single_voice_note_off(vhl_single_voice_t* pVoice)
{
    if (!pVoice) {
        return VHL_ERR_INVALID_ARG;
    }

    if (pVoice->state == VHL_VOICE_STATE_PLAYING) {
        // State Transition: PLAYING -> RELEASING
        pVoice->state = VHL_VOICE_STATE_RELEASING;
    }

    return VHL_OK;
}

vhl_err_t vhl_single_voice_render(vhl_single_voice_t* pVoice,
                                  vhl_audio_buffer_t* pOutBuf,
                                  size_t frameCount)
{
    if (!pVoice || !pOutBuf || !pVoice->pSampleData) {
        return VHL_ERR_INVALID_ARG;
    }

    if (pVoice->state != VHL_VOICE_STATE_PLAYING && pVoice->state != VHL_VOICE_STATE_RELEASING) {
        return VHL_OK; // Voice inactive -> Output remains zeroed
    }

    float velGain = (float)pVoice->velocity / 127.0f;

    for (size_t i = 0; i < frameCount; ++i) {
        uint32_t idx1 = (uint32_t)pVoice->phase;
        float frac = (float)(pVoice->phase - (double)idx1);

        // Check bounds/looping
        if (pVoice->isLooped && pVoice->phase >= (double)pVoice->loopEndFrame) {
            double loopLen = (double)(pVoice->loopEndFrame - pVoice->loopStartFrame);
            if (loopLen > 0.0) {
                pVoice->phase -= loopLen;
                idx1 = (uint32_t)pVoice->phase;
                frac = (float)(pVoice->phase - (double)idx1);
            }
        } else if (!pVoice->isLooped && idx1 >= pVoice->totalFrames) {
            pVoice->state = VHL_VOICE_STATE_FINISHED;
            pVoice->state = VHL_VOICE_STATE_STOPPED;
            break;
        }

        float outSample = 0.0f;

        if (pVoice->interpMode == INTERPOLATION_MODE_LINEAR) {
            int32_t idx_next = (int32_t)idx1 + 1;
            if (pVoice->isLooped && pVoice->loopEndFrame > pVoice->loopStartFrame && (uint32_t)idx_next >= pVoice->loopEndFrame) {
                idx_next = (int32_t)pVoice->loopStartFrame;
            }
            float s0 = fetch_sample(pVoice->pSampleData, (int32_t)idx1, pVoice->totalFrames);
            float s1 = fetch_sample(pVoice->pSampleData, idx_next, pVoice->totalFrames);
            outSample = interpolate_linear(s0, s1, frac);
        } else { // CUBIC
            float s0 = fetch_sample(pVoice->pSampleData, (int32_t)idx1 - 1, pVoice->totalFrames);
            float s1 = fetch_sample(pVoice->pSampleData, (int32_t)idx1,     pVoice->totalFrames);
            float s2 = fetch_sample(pVoice->pSampleData, (int32_t)idx1 + 1, pVoice->totalFrames);
            float s3 = fetch_sample(pVoice->pSampleData, (int32_t)idx1 + 2, pVoice->totalFrames);
            outSample = interpolate_cubic(s0, s1, s2, s3, frac);
        }

        float finalSample = outSample * velGain;
        pOutBuf->left[i]  += finalSample;
        pOutBuf->right[i] += finalSample;

        // Advance fractional phase accumulator
        pVoice->phase += (double)pVoice->pitchRatio;
    }

    return VHL_OK;
}

vhl_voice_state_t vhl_single_voice_get_state(const vhl_single_voice_t* pVoice)
{
    if (!pVoice) {
        return VHL_VOICE_STATE_STOPPED;
    }
    return pVoice->state;
}
