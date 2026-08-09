/* =============================================================================
 * adsr_envelope.c — Per-Voice ADSR Envelope Generator Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 8 — 16-Voice Polyphonic Synthesizer Architecture
 * ============================================================================= */

#include "adsr_envelope.h"
#include <math.h>
#include <string.h>

vhl_err_t vhl_adsr_init(vhl_adsr_t* pAdsr)
{
    if (!pAdsr) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pAdsr, 0, sizeof(vhl_adsr_t));
    pAdsr->state            = ADSR_STATE_IDLE;
    pAdsr->currentLevel     = 0.0f;
    pAdsr->attackMs         = 10.0f;  // 10 ms default attack
    pAdsr->decayMs          = 100.0f; // 100 ms default decay
    pAdsr->sustainLevel     = 0.8f;   // 80% sustain level
    pAdsr->releaseMs        = 200.0f; // 200 ms default release
    pAdsr->attackIncrement  = 1.0f / (10.0f * 44.1f);
    pAdsr->decayCoeff       = 0.999f;
    pAdsr->releaseCoeff     = 0.999f;

    return VHL_OK;
}

vhl_err_t vhl_adsr_set_parameters(vhl_adsr_t* pAdsr, const vhl_instrument_params_t* pParams)
{
    if (!pAdsr || !pParams) {
        return VHL_ERR_INVALID_ARG;
    }

    pAdsr->attackMs     = (pParams->attackMs > 1.0f) ? pParams->attackMs : 1.0f;
    pAdsr->decayMs      = (pParams->decayMs > 1.0f) ? pParams->decayMs : 1.0f;
    pAdsr->sustainLevel = (pParams->sustainLevel >= 0.0f && pParams->sustainLevel <= 1.0f) ? pParams->sustainLevel : 0.8f;
    pAdsr->releaseMs    = (pParams->releaseMs > 1.0f) ? pParams->releaseMs : 1.0f;

    pAdsr->attackIncrement = 1.0f / (pAdsr->attackMs * 44.1f);
    pAdsr->decayCoeff      = expf(-1.0f / (pAdsr->decayMs * 44.1f));
    pAdsr->releaseCoeff    = expf(-1.0f / (pAdsr->releaseMs * 44.1f));

    return VHL_OK;
}

vhl_err_t vhl_adsr_gate(vhl_adsr_t* pAdsr, bool gateOn)
{
    if (!pAdsr) {
        return VHL_ERR_INVALID_ARG;
    }

    if (gateOn) {
        pAdsr->state = ADSR_STATE_ATTACK;
    } else {
        if (pAdsr->state != ADSR_STATE_IDLE) {
            pAdsr->state = ADSR_STATE_RELEASE;
        }
    }

    return VHL_OK;
}

float vhl_adsr_process_sample(vhl_adsr_t* pAdsr)
{
    if (!pAdsr) {
        return 0.0f;
    }

    switch (pAdsr->state) {
        case ADSR_STATE_IDLE:
            pAdsr->currentLevel = 0.0f;
            break;

        case ADSR_STATE_ATTACK:
            pAdsr->currentLevel += pAdsr->attackIncrement;
            if (pAdsr->currentLevel >= 1.0f) {
                pAdsr->currentLevel = 1.0f;
                pAdsr->state = ADSR_STATE_DECAY;
            }
            break;

        case ADSR_STATE_DECAY:
            pAdsr->currentLevel = (pAdsr->currentLevel * pAdsr->decayCoeff) + (pAdsr->sustainLevel * (1.0f - pAdsr->decayCoeff));
            if (fabsf(pAdsr->currentLevel - pAdsr->sustainLevel) < 0.001f) {
                pAdsr->currentLevel = pAdsr->sustainLevel;
                pAdsr->state = ADSR_STATE_SUSTAIN;
            }
            break;

        case ADSR_STATE_SUSTAIN:
            pAdsr->currentLevel = pAdsr->sustainLevel;
            break;

        case ADSR_STATE_RELEASE:
            pAdsr->currentLevel *= pAdsr->releaseCoeff;
            if (pAdsr->currentLevel < 0.001f) {
                pAdsr->currentLevel = 0.0f;
                pAdsr->state = ADSR_STATE_IDLE;
            }
            break;
    }

    return pAdsr->currentLevel;
}
