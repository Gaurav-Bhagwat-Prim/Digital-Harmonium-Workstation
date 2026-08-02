/* =============================================================================
 * adsr_envelope.h — Per-Voice ADSR Envelope Generator
 * Project: Digital Harmonium Workstation Firmware
 * Stage 8 — 16-Voice Polyphonic Synthesizer Architecture
 * ============================================================================= */

#ifndef ADSR_ENVELOPE_H
#define ADSR_ENVELOPE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADSR_STATE_IDLE = 0,
    ADSR_STATE_ATTACK,
    ADSR_STATE_DECAY,
    ADSR_STATE_SUSTAIN,
    ADSR_STATE_RELEASE
} vhl_adsr_state_t;

/* Dynamic Instrument Parameter Struct */
typedef struct {
    float attackMs;     /* Attack duration in ms (1 to 5000 ms) */
    float decayMs;      /* Decay duration in ms (1 to 5000 ms) */
    float sustainLevel; /* Sustain volume ratio (0.0 to 1.0) */
    float releaseMs;    /* Release duration in ms (1 to 5000 ms) */
    float masterGain;   /* Default instrument gain */
} vhl_instrument_params_t;

typedef struct {
    float attackMs;
    float decayMs;
    float sustainLevel;
    float releaseMs;
    
    vhl_adsr_state_t state;
    float currentLevel;
    float attackIncrement;
    float decayCoeff;
    float releaseCoeff;
} vhl_adsr_t;

/* -----------------------------------------------------------------------------
 * Public ADSR API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize ADSR Envelope Generator */
vhl_err_t vhl_adsr_init(vhl_adsr_t* pAdsr);

/** Configure ADSR Envelope parameters from instrument preset */
vhl_err_t vhl_adsr_set_parameters(vhl_adsr_t* pAdsr, const vhl_instrument_params_t* pParams);

/** Trigger ADSR Gate On (Key Press) or Gate Off (Key Release) */
vhl_err_t vhl_adsr_gate(vhl_adsr_t* pAdsr, bool gateOn);

/** Process single frame step and return current envelope coefficient (0.0 to 1.0) */
float vhl_adsr_process_sample(vhl_adsr_t* pAdsr);

#ifdef __cplusplus
}
#endif

#endif /* ADSR_ENVELOPE_H */
