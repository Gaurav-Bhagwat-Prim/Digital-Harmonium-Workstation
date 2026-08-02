/* =============================================================================
 * poly_synth.h — 16-Voice Polyphonic Synthesizer
 * Project: Digital Harmonium Workstation Firmware
 * Stage 8 — 16-Voice Polyphonic Synthesizer Architecture
 * ============================================================================= */

#ifndef POLY_SYNTH_H
#define POLY_SYNTH_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "sample_engine.h"
#include "adsr_envelope.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extensible Polyphonic Voice Structure */
typedef struct {
    vhl_single_voice_t voiceEngine;
    vhl_adsr_t         envelope;
    vhl_midi_note_t    activeNote;
    vhl_midi_vel_t     activeVelocity;
    uint32_t           ageCounter;   /* Monotonic counter for LRU stealing */
    float              panLeft;      /* Left channel gain (0.0 to 1.0) */
    float              panRight;     /* Right channel gain (0.0 to 1.0) */

    /* Reserved Extensibility Slots for Polyphone SF2 Engine Compatibility */
    void*              pFilterState;     /* Reserved for Stage 9 Resonant Biquad Filter */
    void*              pModulationState; /* Reserved for LFO / Vibrato Modulators */
    void*              pLayerState;      /* Reserved for Multi-Sample Velocity Layers */
} vhl_poly_voice_t;

typedef struct {
    vhl_poly_voice_t        voices[VHL_MAX_POLYPHONY]; /* 16 Voice Array */
    vhl_instrument_params_t currentInstParams;          /* Active VHL Instrument Params */
    float                   masterVolume;               /* Master Volume Gain (0.0 to 1.0) */
    uint32_t                voiceAllocCounter;          /* Monotonic age counter */
    uint32_t                activeVoiceCount;           /* Count of currently playing voices */
} vhl_poly_synth_t;

/* -----------------------------------------------------------------------------
 * Public Polyphonic Synthesizer APIs
 * ----------------------------------------------------------------------------- */

/** Initialize 16-Voice Polyphonic Synthesizer */
vhl_err_t vhl_poly_synth_init(vhl_poly_synth_t* pSynth);

/** Configure active VHL Instrument preset parameters */
vhl_err_t vhl_poly_synth_set_instrument_params(vhl_poly_synth_t* pSynth, const vhl_instrument_params_t* pParams);

/** Bind memory-mapped Flash PCM sample to all 16 voices */
vhl_err_t vhl_poly_synth_bind_sample(vhl_poly_synth_t* pSynth,
                                     const vhl_pcm16_t* pData,
                                     uint32_t totalFrames,
                                     uint32_t loopStart,
                                     uint32_t loopEnd,
                                     bool isLooped,
                                     vhl_midi_note_t rootKey);

/** Dispatch Note-On into active/stolen voice slot */
vhl_err_t vhl_poly_synth_note_on(vhl_poly_synth_t* pSynth, vhl_midi_note_t note, vhl_midi_vel_t velocity);

/** Dispatch Note-Off to matching voice slot */
vhl_err_t vhl_poly_synth_note_off(vhl_poly_synth_t* pSynth, vhl_midi_note_t note);

/** Render 16 voices into stereo float buffer */
vhl_err_t vhl_poly_synth_render(vhl_poly_synth_t* pSynth, vhl_audio_buffer_t* pOutBuf, size_t frameCount);

/** Set Master Output Volume (0.0 to 1.0) */
vhl_err_t vhl_poly_synth_set_master_volume(vhl_poly_synth_t* pSynth, float volume);

#ifdef __cplusplus
}
#endif

#endif /* POLY_SYNTH_H */
