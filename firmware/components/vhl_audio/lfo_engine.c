/* =============================================================================
 * lfo_engine.c — Dual Low Frequency Oscillator Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#include "lfo_engine.h"
#include <math.h>
#include <string.h>

#define M_PI_F 3.14159265358979323846f

vhl_err_t vhl_lfo_init(vhl_lfo_t* pLfo)
{
    if (!pLfo) return VHL_ERR_INVALID_ARG;
    memset(pLfo, 0, sizeof(vhl_lfo_t));
    return VHL_OK;
}

vhl_err_t vhl_lfo_configure(vhl_lfo_t* pLfo, uint32_t delayMs, float freqHz, float pitchCents, float filterCents, float volDb)
{
    if (!pLfo) return VHL_ERR_INVALID_ARG;

    pLfo->delayMs          = delayMs;
    pLfo->freqHz           = (freqHz > 0.01f) ? freqHz : 0.01f;
    pLfo->depthPitchCents  = pitchCents;
    pLfo->depthFilterCents = filterCents;
    pLfo->depthVolumeDb    = volDb;
    pLfo->active           = (fabsf(pitchCents) > 0.01f || fabsf(filterCents) > 0.01f || fabsf(volDb) > 0.01f);

    return VHL_OK;
}

float vhl_lfo_process_block(vhl_lfo_t* pLfo, uint32_t blockMs, float sampleRate)
{
    if (!pLfo || !pLfo->active) {
        if (pLfo) pLfo->currentOutput = 0.0f;
        return 0.0f;
    }

    pLfo->elapsedMs += blockMs;

    // If still in delay phase, return 0
    if (pLfo->elapsedMs < pLfo->delayMs) {
        pLfo->currentOutput = 0.0f;
        return 0.0f;
    }

    // Phase increment per block: delta = 2 * pi * freq * (blockMs / 1000)
    float deltaPhase = 2.0f * M_PI_F * pLfo->freqHz * ((float)blockMs / 1000.0f);
    pLfo->phase += deltaPhase;

    // Wrap phase within [0..2pi]
    while (pLfo->phase >= 2.0f * M_PI_F) {
        pLfo->phase -= 2.0f * M_PI_F;
    }

    pLfo->currentOutput = sinf(pLfo->phase);
    return pLfo->currentOutput;
}

vhl_err_t vhl_lfo_reset(vhl_lfo_t* pLfo)
{
    if (!pLfo) return VHL_ERR_INVALID_ARG;
    pLfo->elapsedMs     = 0;
    pLfo->phase         = 0.0f;
    pLfo->currentOutput = 0.0f;
    return VHL_OK;
}
