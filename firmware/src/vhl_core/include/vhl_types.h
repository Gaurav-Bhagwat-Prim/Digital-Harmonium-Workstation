/* =============================================================================
 * vhl_types.h — Global System Type Definitions
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef VHL_TYPES_H
#define VHL_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 * Audio & DSP Scalar Types
 * ----------------------------------------------------------------------------- */

/** 32-bit single-precision floating point for internal DSP synthesis accumulation */
typedef float vhl_sample_t;

/** 16-bit signed integer for hardware I²S DMA PCM output */
typedef int16_t vhl_pcm16_t;

/** Interleaved stereo frame (16-bit left/right pair for DMA transmission) */
typedef struct {
    vhl_pcm16_t left;
    vhl_pcm16_t right;
} vhl_stereo_frame_t;

/** De-interleaved floating-point audio buffer structure */
typedef struct {
    vhl_sample_t* left;
    vhl_sample_t* right;
    size_t        frameCount;
} vhl_audio_buffer_t;

/* -----------------------------------------------------------------------------
 * MIDI & Note Scalar Types
 * ----------------------------------------------------------------------------- */

/** MIDI note number (0 to 127, where 60 = Middle C / C4) */
typedef uint8_t vhl_midi_note_t;

/** MIDI velocity value (0 to 127) */
typedef uint8_t vhl_midi_vel_t;

/** Fixed voice channel handle index (0 to VHL_MAX_POLYPHONY - 1) */
typedef uint8_t vhl_voice_id_t;

/** Unique instrument package identifier */
typedef uint16_t vhl_instrument_id_t;

/* -----------------------------------------------------------------------------
 * Key & Input Event Types
 * ----------------------------------------------------------------------------- */

typedef enum {
    KEY_ACTION_RELEASE = 0,
    KEY_ACTION_PRESS   = 1
} vhl_key_action_t;

typedef struct {
    vhl_midi_note_t  note;
    vhl_midi_vel_t   velocity;
    vhl_key_action_t action;
    uint32_t         timestampMs;
} vhl_key_event_t;

#ifdef __cplusplus
}
#endif

#endif /* VHL_TYPES_H */
