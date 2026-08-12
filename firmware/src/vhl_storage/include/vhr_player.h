/* =============================================================================
 * vhr_player.h — VHR Performance Playback Replayer Engine (v2.0)
 * Project: Digital Harmonium Workstation Firmware
 *
 * Architecture:
 *   - Reads a .vhr v2.0 binary buffer (header + event stream) from caller-owned RAM.
 *   - Advances a uint64_t timing cursor each time vhr_player_tick() is called.
 *   - Re-injects due events onto vhl_event_bus via vhl_event_bus_post_key_event().
 *   - The synthesizer is identical for live and replayed input — the player
 *     never calls the DSP engine directly.
 *   - Zero heap allocation; all state lives in caller-owned vhl_vhr_player_t.
 *
 * Instrument Resolution Priority (strictly in this order):
 *   1. vhr_playback_overrides_t.override_instrument  (if has_instrument=true)
 *   2. VHRFileHeader.native_instrument (if the .vhl exists on SD card)
 *   3. Currently selected system VHL   (if native is unavailable)
 *   4. VHL_ERR_NOT_FOUND               (if no instrument can be resolved)
 *
 * Playback Parameter Priority:
 *   User override → Recorded VHR event/header value → System default
 *
 * Forward Compatibility:
 *   Unknown vhr_event_type_t values are silently skipped (eventsSkippedCount++).
 *   A v2.0 player can safely play a VHR file from a future firmware version.
 *
 * Event Dispatch:
 *   VHR defines a full event vocabulary (NOTE_ON, NOTE_OFF, TEMPO_CHANGE, etc.).
 *   Events whose corresponding runtime engine is not yet implemented are skipped
 *   gracefully — they do NOT cause an error. This allows the file format to be
 *   structurally complete while engines are implemented incrementally.
 * ============================================================================= */

#ifndef VHR_PLAYER_H
#define VHR_PLAYER_H

#include "vhr_format.h"
#include "vhl_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Speed Multiplier Constants
 * ============================================================================= */

#define VHR_PLAYER_SPEED_MIN     (0.25f)
#define VHR_PLAYER_SPEED_MAX     (4.0f)
#define VHR_PLAYER_SPEED_DEFAULT (1.0f)

/* =============================================================================
 * Playback State Machine — 7 States
 *
 *  IDLE → (load + valid CRC) → LOADING → READY
 *          magic fail / CRC fail → IDLE         │
 *                                             play()
 *                                               ▼
 *                               PAUSED ←─── PLAYING ──→ FINISHED
 *                                play() ──►  │              │
 *                                            │ stop()       │ stop()
 *                                            ▼              ▼
 *                                          READY          READY
 * ============================================================================= */

typedef enum {
    VHR_PLAYER_STATE_IDLE     = 0,
    VHR_PLAYER_STATE_LOADING  = 1,
    VHR_PLAYER_STATE_READY    = 2,
    VHR_PLAYER_STATE_PLAYING  = 3,
    VHR_PLAYER_STATE_PAUSED   = 4,
    VHR_PLAYER_STATE_STOPPING = 5,
    VHR_PLAYER_STATE_FINISHED = 6
} vhr_player_state_t;

/* =============================================================================
 * Player Engine Struct — Zero heap allocation
 *
 * pHeader and pEventStream point into a caller-owned buffer.
 * The player never copies the event stream.
 * ============================================================================= */

typedef struct {
    /* Bound file data (pointer-only, no copy) */
    const VHRFileHeader*  pHeader;
    const VHREventEntry*  pEventStream;
    uint32_t              totalEvents;

    /* Active overrides (shallow copy of caller-supplied struct) */
    vhr_playback_overrides_t overrides;   /* Zero-initialized = no overrides */

    /* Resolved instrument name (result of resolution logic applied during play()) */
    char resolvedInstrument[VHR_INSTRUMENT_NAME_LEN];

    /* Timing cursor */
    uint32_t              cursorIdx;       /* Index of next event to check/fire */
    uint64_t              accumulatedUs;   /* Total elapsed playback time (µs, adjusted for speed) */
    uint64_t              cursorTimeUs;    /* Absolute time of last fired event (µs) */
    float                 speedMultiplier; /* Clamped [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX] */

    /* Current musical state (updated from events and overrides during playback) */
    uint16_t              currentTempo_bpm_x10; /* Active BPM × 10; from override or events */
    int8_t                currentTranspose;      /* Active semitone offset */
    uint8_t               currentVolume;         /* Active master volume 0-127 */
    uint8_t               currentScale;          /* Active scale index */

    /* State */
    vhr_player_state_t    state;

    /* Diagnostics */
    uint32_t              eventsFireCount;      /* NOTE_ON/OFF injected onto Event Bus */
    uint32_t              eventsSkippedCount;   /* Unknown/unimplemented types skipped */
    uint32_t              eventsAppliedCount;   /* Musical param events applied (tempo etc.) */
    uint32_t              errorCount;           /* Malformed event entries skipped */

} vhl_vhr_player_t;

/* =============================================================================
 * Public API
 * ============================================================================= */

/**
 * Initialize player to IDLE state. Must be called before any other API.
 */
vhl_err_t vhr_player_init(vhl_vhr_player_t* pPlayer);

/**
 * Load and validate a .vhr v2.0 binary buffer.
 *
 * Validates: magic "VHR2", version_major == 2, buffer size covers declared events,
 * and CRC-32 computed over the event stream bytes.
 *
 * Does NOT yet resolve or load the VHL instrument — instrument resolution happens
 * in vhr_player_play() so that overrides can be set after load but before play.
 *
 * On success → state = READY, cursor = 0.
 * On failure → state = IDLE, returns error code.
 *
 * @param pPlayer     Player struct.
 * @param pVhrBuffer  Pointer to the full .vhr binary (header + events). Caller-owned.
 * @param bufferSize  Total byte length of the buffer.
 * @return VHL_OK, VHL_ERR_INVALID_ARG, VHL_ERR_VHL_INVALID_MAGIC,
 *         VHL_ERR_NOT_SUPPORTED (version_major mismatch), VHL_ERR_VHL_CRC_MISMATCH.
 */
vhl_err_t vhr_player_load(vhl_vhr_player_t* pPlayer,
                           const void* pVhrBuffer,
                           size_t bufferSize);

/**
 * Set playback overrides. May be called at any time after init, including
 * after load() and during playback.
 *
 * The override struct is shallow-copied into the player. The original struct
 * does not need to remain valid after this call.
 *
 * @param pPlayer     Player struct.
 * @param pOverrides  Overrides to apply. Pass NULL to clear all overrides.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_set_overrides(vhl_vhr_player_t* pPlayer,
                                    const vhr_playback_overrides_t* pOverrides);

/**
 * Start or resume playback. Valid from READY or PAUSED states.
 *
 * On first play() from READY: performs instrument resolution (override → native → current).
 * Initializes currentTempo, currentTranspose, currentVolume from overrides or header.
 *
 * @return VHL_OK or VHL_ERR_INVALID_STATE.
 */
vhl_err_t vhr_player_play(vhl_vhr_player_t* pPlayer);

/**
 * Pause playback. Freezes accumulatedUs; timing resumes seamlessly on play().
 * Valid from PLAYING state only.
 * @return VHL_OK or VHL_ERR_INVALID_STATE.
 */
vhl_err_t vhr_player_pause(vhl_vhr_player_t* pPlayer);

/**
 * Stop playback and reset cursor to 0.
 * From any non-IDLE state → READY. From IDLE → IDLE (no-op).
 * Resets cursor, accumulatedUs, eventsFireCount, and musical state.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_stop(vhl_vhr_player_t* pPlayer);

/**
 * Advance the playback cursor by elapsedUs microseconds.
 *
 * Fires all events whose cumulative timestamp <= accumulatedUs via Event Bus.
 * No-op if state != PLAYING. Transitions to FINISHED when all events are fired.
 *
 * Event dispatch policy:
 *   NOTE_ON/NOTE_OFF → injected onto Event Bus unconditionally.
 *   TEMPO_CHANGE     → applied to currentTempo_bpm_x10 (if no tempo override).
 *   TRANSPOSE_CHANGE → applied to currentTranspose (if no transpose override).
 *   VOLUME_CHANGE    → applied to currentVolume (if no volume override).
 *   SCALE_CHANGE     → applied to currentScale.
 *   All others       → skipped gracefully (eventsSkippedCount++).
 *
 * User overrides always take priority: if has_tempo=true, recorded TEMPO_CHANGE
 * events are tracked in currentTempo but the override value is used for synthesis.
 *
 * @param pPlayer    Player struct.
 * @param elapsedUs  Microseconds elapsed since the last tick() call.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_tick(vhl_vhr_player_t* pPlayer, uint32_t elapsedUs);

/**
 * Seek to a specific event index. Recomputes cursorTimeUs by scanning from 0.
 * Valid from READY, PLAYING, PAUSED, or FINISHED states.
 * @return VHL_OK or VHL_ERR_INVALID_STATE.
 */
vhl_err_t vhr_player_seek(vhl_vhr_player_t* pPlayer, uint32_t eventIdx);

/**
 * Set playback speed multiplier. Clamped to [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX].
 * May be called at any time including during playback.
 * Equivalent to setting has_speed=true in the overrides struct.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_set_speed(vhl_vhr_player_t* pPlayer, float speed);

/**
 * Query the resolved instrument name after play() has been called.
 * Returns an empty string if instrument resolution has not yet occurred.
 * @return Pointer to the internal resolvedInstrument buffer (read-only).
 */
const char* vhr_player_get_resolved_instrument(const vhl_vhr_player_t* pPlayer);

/**
 * Query current playback state.
 * @return VHR_PLAYER_STATE_IDLE if pPlayer is NULL.
 */
vhr_player_state_t vhr_player_get_state(const vhl_vhr_player_t* pPlayer);

/**
 * Query playback progress.
 * @param outCurrent  (Optional) Next event index to be fired.
 * @param outTotal    (Optional) Total events in the loaded file.
 */
void vhr_player_get_progress(const vhl_vhr_player_t* pPlayer,
                              uint32_t* outCurrent,
                              uint32_t* outTotal);

/**
 * Query current musical state as resolved from overrides + recorded events.
 * Valid at any time after load(). Values updated live during playback by tick().
 * @param outTempo_bpm_x10  (Optional) Current BPM × 10.
 * @param outTranspose      (Optional) Current semitone transpose.
 * @param outVolume         (Optional) Current master volume (0-127).
 * @param outScale          (Optional) Current scale index.
 */
void vhr_player_get_musical_state(const vhl_vhr_player_t* pPlayer,
                                   uint16_t* outTempo_bpm_x10,
                                   int8_t*   outTranspose,
                                   uint8_t*  outVolume,
                                   uint8_t*  outScale);

#ifdef __cplusplus
}
#endif

#endif /* VHR_PLAYER_H */
