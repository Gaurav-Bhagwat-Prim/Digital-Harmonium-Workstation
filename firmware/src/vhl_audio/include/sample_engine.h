/* =============================================================================
 * sample_engine.h — Single Voice Synthesis Engine & Flash PCM Reader
 * Project: Digital Harmonium Workstation Firmware
 * Stage 7 — Single Voice Audio Synthesis Engine
 * ============================================================================= */

#ifndef SAMPLE_ENGINE_H
#define SAMPLE_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INTERPOLATION_MODE_LINEAR = 0,
    INTERPOLATION_MODE_CUBIC  = 1
} vhl_interpolation_mode_t;

typedef enum {
    VHL_VOICE_STATE_STOPPED = 0,
    VHL_VOICE_STATE_STARTING,
    VHL_VOICE_STATE_PLAYING,
    VHL_VOICE_STATE_RELEASING,
    VHL_VOICE_STATE_FINISHED
} vhl_voice_state_t;

typedef struct {
    const vhl_pcm16_t* pSampleData;      /* Memory-mapped Flash PCM pointer */
    uint32_t           totalFrames;      /* Total sample frame count */
    uint32_t           loopStartFrame;   /* Loop start index */
    uint32_t           loopEndFrame;     /* Loop end index */
    bool               isLooped;         /* True = Loop active, False = One-shot */
    
    vhl_midi_note_t    rootKey;          /* Root MIDI key (e.g. 60 = C4) */
    int8_t             fineTuneCents;    /* Fine tune offset (-50 to +50 cents) */
    
    double             phase;            /* Fractional phase accumulator */
    float              pitchRatio;       /* Sub-cent pitch transposition ratio */
    vhl_voice_state_t  state;            /* Explicit Voice Playback State Machine */
    vhl_midi_vel_t     velocity;         /* Trigger velocity (1 to 127) */
    
    vhl_interpolation_mode_t interpMode; /* Linear or Cubic Hermite */
} vhl_single_voice_t;

/* -----------------------------------------------------------------------------
 * Public Single Voice API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize Single Voice data structure */
vhl_err_t vhl_single_voice_init(vhl_single_voice_t* pVoice);

/** Bind memory-mapped Flash PCM sample data to voice */
vhl_err_t vhl_single_voice_bind_sample(vhl_single_voice_t* pVoice,
                                       const vhl_pcm16_t* pData,
                                       uint32_t totalFrames,
                                       uint32_t loopStart,
                                       uint32_t loopEnd,
                                       bool isLooped,
                                       vhl_midi_note_t rootKey);

/** Trigger Note-On event with target MIDI note and velocity */
vhl_err_t vhl_single_voice_note_on(vhl_single_voice_t* pVoice,
                                   vhl_midi_note_t note,
                                   vhl_midi_vel_t velocity);

/** Trigger Note-Off release */
vhl_err_t vhl_single_voice_note_off(vhl_single_voice_t* pVoice);

/** Render audio frame block into stereo float buffer */
vhl_err_t vhl_single_voice_render(vhl_single_voice_t* pVoice,
                                  vhl_audio_buffer_t* pOutBuf,
                                  size_t frameCount);

/** Query current voice playback state */
vhl_voice_state_t vhl_single_voice_get_state(const vhl_single_voice_t* pVoice);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_ENGINE_H */
