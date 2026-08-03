/* =============================================================================
 * vhl_dsp_types.h — Runtime DSP State Objects & Parameter Structures
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#ifndef VHL_DSP_TYPES_H
#define VHL_DSP_TYPES_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime Volume & Modulation Envelope Parameters */
typedef struct {
    uint32_t delayMs;
    uint32_t attackMs;
    uint32_t holdMs;
    uint32_t decayMs;
    float    sustainGain;   /* Converted 0.0 to 1.0 linear gain level */
    uint32_t releaseMs;
    int8_t   keyToHold;
    int8_t   keyToDecay;
    float    depthPitchCents;
    float    depthFilterCents;
} VHLRuntimeEnvelope;

/* Runtime LFO Parameters */
typedef struct {
    uint32_t delayMs;
    float    freqHz;        /* Converted mHz -> Hz */
    float    depthPitchCents;
    float    depthFilterCents;
    float    depthVolumeDb;
} VHLRuntimeLFO;

/* Runtime Biquad Filter Configuration */
typedef struct {
    float    cutoffHz;      /* Cutoff frequency in Hz */
    float    resonanceDb;   /* Resonance Q in dB */
    bool     active;
} VHLRuntimeFilterConfig;

/* Complete Runtime Voice State (Per-Voice Parameter Ownership) */
typedef struct {
    uint8_t                voiceId;
    uint8_t                channel;
    uint8_t                key;
    uint8_t                velocity;
    uint8_t                exclusiveClass;
    uint8_t                loopMode;
    bool                   isChoked;

    /* Tuning */
    int8_t                 coarseTune;
    float                  fineTuneCents;
    uint8_t                scaleTuning;

    /* Volume & Spatial */
    float                  attenuationGain; /* Converted centibels -> linear gain */
    float                  panLeft;         /* Converted -500..+500 -> 0.0..1.0 gain */
    float                  panRight;
    uint8_t                reverbSend;
    uint8_t                chorusSend;

    /* Runtime Envelope & LFO Configuration Objects */
    VHLRuntimeEnvelope     volEnvConfig;
    VHLRuntimeEnvelope     modEnvConfig;
    VHLRuntimeLFO          modLfoConfig;
    VHLRuntimeLFO          vibLfoConfig;
    VHLRuntimeFilterConfig filterConfig;

    /* Sample References */
    uint16_t               sampleId;
    const VHLSampleEntry*  pSampleEntry;
    const int16_t*         pPcmFrames;
} VHLRuntimeVoiceConfig;

#ifdef __cplusplus
}
#endif

#endif /* VHL_DSP_TYPES_H */
