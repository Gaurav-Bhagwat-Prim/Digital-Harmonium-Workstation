/* =============================================================================
 * lfo_engine.h — Dual Low Frequency Oscillator (Mod LFO & Vib LFO) Engine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#ifndef LFO_ENGINE_H
#define LFO_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t delayMs;
    float    freqHz;
    float    depthPitchCents;
    float    depthFilterCents;
    float    depthVolumeDb;

    uint32_t elapsedMs;
    float    phase;          /* Current phase angle in radians [0..2pi] */
    float    currentOutput;  /* Current LFO sine output [-1.0..+1.0] */
    bool     active;
} vhl_lfo_t;

/** Initialize LFO structure */
vhl_err_t vhl_lfo_init(vhl_lfo_t* pLfo);

/** Configure LFO parameters */
vhl_err_t vhl_lfo_configure(vhl_lfo_t* pLfo, uint32_t delayMs, float freqHz, float pitchCents, float filterCents, float volDb);

/** Advance LFO phase by block time delta and compute sine output */
float vhl_lfo_process_block(vhl_lfo_t* pLfo, uint32_t blockMs, float sampleRate);

/** Reset LFO phase and delay timers */
vhl_err_t vhl_lfo_reset(vhl_lfo_t* pLfo);

#ifdef __cplusplus
}
#endif

#endif /* LFO_ENGINE_H */
