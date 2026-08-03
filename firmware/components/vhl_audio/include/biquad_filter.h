/* =============================================================================
 * biquad_filter.h — Direct Form II 2nd-Order Resonant Low-Pass Biquad Filter
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#ifndef BIQUAD_FILTER_H
#define BIQUAD_FILTER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float w1, w2;       /* Delay line state variables for Direct Form II */
    float b0, b1, b2;   /* Numerator filter coefficients */
    float a1, a2;       /* Denominator filter coefficients (normalized by a0) */
    float lastCutoffHz; /* Cached cutoff for update check */
    float lastResonanceDb;
    bool  active;
} vhl_biquad_filter_t;

/** Initialize biquad filter state */
vhl_err_t vhl_biquad_init(vhl_biquad_filter_t* pFilter);

/** Re-calculate 2nd-order low-pass biquad coefficients if cutoff or resonance changed */
vhl_err_t vhl_biquad_configure(vhl_biquad_filter_t* pFilter, float cutoffHz, float resonanceDb, float sampleRate);

/** Process single audio sample through Direct Form II Biquad Filter */
float vhl_biquad_process_sample(vhl_biquad_filter_t* pFilter, float sampleIn);

/** Reset filter delay lines */
vhl_err_t vhl_biquad_reset(vhl_biquad_filter_t* pFilter);

#ifdef __cplusplus
}
#endif

#endif /* BIQUAD_FILTER_H */
