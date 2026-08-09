/* =============================================================================
 * biquad_filter.c — Direct Form II 2nd-Order Resonant Low-Pass Biquad Filter Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#include "biquad_filter.h"
#include <math.h>
#include <string.h>

#define M_PI_F 3.14159265358979323846f

vhl_err_t vhl_biquad_init(vhl_biquad_filter_t* pFilter)
{
    if (!pFilter) return VHL_ERR_INVALID_ARG;
    memset(pFilter, 0, sizeof(vhl_biquad_filter_t));
    pFilter->lastCutoffHz    = -1.0f;
    pFilter->lastResonanceDb = -1.0f;
    return VHL_OK;
}

vhl_err_t vhl_biquad_configure(vhl_biquad_filter_t* pFilter, float cutoffHz, float resonanceDb, float sampleRate)
{
    if (!pFilter || sampleRate <= 0.0f) return VHL_ERR_INVALID_ARG;

    // Clamp cutoff frequency between 20 Hz and Nyquist (0.45 * sampleRate)
    float maxCutoff = sampleRate * 0.45f;
    if (cutoffHz < 20.0f)     cutoffHz = 20.0f;
    if (cutoffHz > maxCutoff) cutoffHz = maxCutoff;

    if (resonanceDb < 0.0f)  resonanceDb = 0.0f;
    if (resonanceDb > 96.0f) resonanceDb = 96.0f;

    // If parameters haven't changed, skip coefficient calculation
    if (fabsf(cutoffHz - pFilter->lastCutoffHz) < 0.1f &&
        fabsf(resonanceDb - pFilter->lastResonanceDb) < 0.1f) {
        return VHL_OK;
    }

    pFilter->lastCutoffHz    = cutoffHz;
    pFilter->lastResonanceDb = resonanceDb;

    // Calculate Q from resonance in dB (Q = 10^(dB/20) * 0.7071)
    float qLinear = powf(10.0f, resonanceDb / 20.0f) * 0.70710678f;
    if (qLinear < 0.5f) qLinear = 0.5f;

    // Angular frequency w0 = 2 * pi * fc / fs
    float w0    = 2.0f * M_PI_F * cutoffHz / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * qLinear);

    float b0 = (1.0f - cosw0) / 2.0f;
    float b1 =  1.0f - cosw0;
    float b2 = (1.0f - cosw0) / 2.0f;
    float a0 =  1.0f + alpha;
    float a1 = -2.0f * cosw0;
    float a2 =  1.0f - alpha;

    // Normalize by a0
    pFilter->b0 = b0 / a0;
    pFilter->b1 = b1 / a0;
    pFilter->b2 = b2 / a0;
    pFilter->a1 = a1 / a0;
    pFilter->a2 = a2 / a0;
    pFilter->active = (cutoffHz < (sampleRate * 0.44f));

    return VHL_OK;
}

float vhl_biquad_process_sample(vhl_biquad_filter_t* pFilter, float sampleIn)
{
    if (!pFilter || !pFilter->active) {
        return sampleIn;
    }

    // Direct Form II Difference Equation:
    // w[n] = x[n] - a1 * w[n-1] - a2 * w[n-2]
    // y[n] = b0 * w[n] + b1 * w[n-1] + b2 * w[n-2]
    float w0 = sampleIn - pFilter->a1 * pFilter->w1 - pFilter->a2 * pFilter->w2;
    float sampleOut = pFilter->b0 * w0 + pFilter->b1 * pFilter->w1 + pFilter->b2 * pFilter->w2;

    pFilter->w2 = pFilter->w1;
    pFilter->w1 = w0;

    return sampleOut;
}

vhl_err_t vhl_biquad_reset(vhl_biquad_filter_t* pFilter)
{
    if (!pFilter) return VHL_ERR_INVALID_ARG;
    pFilter->w1 = 0.0f;
    pFilter->w2 = 0.0f;
    return VHL_OK;
}
