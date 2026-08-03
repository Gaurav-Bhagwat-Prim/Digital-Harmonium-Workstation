/* =============================================================================
 * poly_synth.h — 16-Voice Polyphonic Synthesizer Runtime Engine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 10 — Complete Real-Time Audio DSP Execution Engine
 * ============================================================================= */

#ifndef POLY_SYNTH_H
#define POLY_SYNTH_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "sample_engine.h"
#include "adsr_envelope.h"
#include "lfo_engine.h"
#include "biquad_filter.h"
#include "vhl_dsp_types.h"
#include "parameter_converter.h"
#include "vhl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extensible Polyphonic Voice Structure */
typedef struct {
    vhl_single_voice_t     voiceEngine;
    vhl_adsr_t             envelope;        /* Volume Envelope */
    vhl_adsr_t             modEnvelope;     /* Modulation Envelope */
    vhl_lfo_t              modLfo;          /* Modulation LFO */
    vhl_lfo_t              vibLfo;          /* Vibrato LFO */
    vhl_biquad_filter_t    filter;          /* Resonant Low-Pass Biquad Filter */

    vhl_midi_note_t        activeNote;
    vhl_midi_vel_t         activeVelocity;
    uint8_t                channel;
    uint8_t                exclusiveClass;
    uint32_t               ageCounter;      /* Monotonic counter for LRU stealing */
    float                  panLeft;         /* Left channel gain (0.0 to 1.0) */
    float                  panRight;        /* Right channel gain (0.0 to 1.0) */

    /* Stage 10 Complete Runtime Parameter Ownership */
    VHLRuntimeVoiceConfig  runtimeConfig;

    /* Reserved Extension Objects & Pointers */
    void*                  pFilterState;     /* Bound to filter */
    void*                  pModulationState; /* Bound to modLfo */
    void*                  pLayerState;      /* Bound to vibLfo */
} vhl_poly_voice_t;

typedef struct {
    vhl_poly_voice_t        voices[VHL_MAX_POLYPHONY]; /* 16 Voice Array */
    vhl_instrument_params_t currentInstParams;          /* Active VHL Instrument Params */
    const vhl_loader_t*     pActiveLoader;              /* Loaded VHL Binary Package */
    uint8_t                 activeInstrumentId;
    float                   masterVolume;               /* Master Volume Gain (0.0 to 1.0) */
    uint32_t                voiceAllocCounter;          /* Monotonic age counter */
    uint32_t                activeVoiceCount;           /* Count of currently playing voices */
} vhl_poly_synth_t;

/* -----------------------------------------------------------------------------
 * Public Polyphonic Synthesizer APIs
 * ----------------------------------------------------------------------------- */

/** Initialize 16-Voice Polyphonic Synthesizer */
vhl_err_t vhl_poly_synth_init(vhl_poly_synth_t* pSynth);

/** Bind active VHL Binary Loader package */
vhl_err_t vhl_poly_synth_bind_vhl_loader(vhl_poly_synth_t* pSynth, const vhl_loader_t* pLoader, uint8_t instrumentId);

/** Configure active VHL Instrument preset parameters */
vhl_err_t vhl_poly_synth_set_instrument_params(vhl_poly_synth_t* pSynth, const vhl_instrument_params_t* pParams);

/** Bind raw PCM sample to all 16 voices */
vhl_err_t vhl_poly_synth_bind_sample(vhl_poly_synth_t* pSynth,
                                     const vhl_pcm16_t* pData,
                                     uint32_t totalFrames,
                                     uint32_t loopStart,
                                     uint32_t loopEnd,
                                     bool isLooped,
                                     vhl_midi_note_t rootKey);

/** Dispatch Note-On: perform O(1) region lookup, exclusive class choke candidate marking, and voice allocation */
vhl_err_t vhl_poly_synth_note_on(vhl_poly_synth_t* pSynth, vhl_midi_note_t note, vhl_midi_vel_t velocity);

/** Dispatch Note-Off to matching voice slots */
vhl_err_t vhl_poly_synth_note_off(vhl_poly_synth_t* pSynth, vhl_midi_note_t note);

/** Perform Exclusive Class Choke Scan */
vhl_err_t vhl_poly_synth_choke_exclusive_class(vhl_poly_synth_t* pSynth, uint8_t exclusiveClass, uint8_t channel, uint8_t* outChokedCount);

/** Render 16 voices into stereo float buffer with complete DSP pipeline and Soft Limiter */
vhl_err_t vhl_poly_synth_render(vhl_poly_synth_t* pSynth, vhl_audio_buffer_t* pOutBuf, size_t frameCount);

/** Set Master Output Volume (0.0 to 1.0) */
vhl_err_t vhl_poly_synth_set_master_volume(vhl_poly_synth_t* pSynth, float volume);

#ifdef __cplusplus
}
#endif

#endif /* POLY_SYNTH_H */
