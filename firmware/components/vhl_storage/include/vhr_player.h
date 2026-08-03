/* =============================================================================
 * vhr_player.h — VHR Performance Playback Replayer Engine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 12 — VHR Performance Playback Replayer Engine
 *
 * Architecture:
 *   - Reads a .vhr binary buffer (header + event stream) from caller-owned memory.
 *   - Advances a uint64_t timing cursor each time vhr_player_tick() is called.
 *   - Re-injects due events onto vhl_event_bus via vhl_event_bus_post_key_event().
 *   - The synthesizer is identical for live and replayed input — the player
 *     never calls the DSP engine directly.
 *   - Zero heap allocation; all state lives in caller-owned vhl_vhr_player_t.
 *   - Unknown future event types are skipped gracefully (eventsSkippedCount++).
 *   - speedMultiplier is clamped to [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX].
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

#define VHR_PLAYER_SPEED_MIN     (0.25f)   /**< Slowest playback: 1/4 speed */
#define VHR_PLAYER_SPEED_MAX     (4.0f)    /**< Fastest playback: 4× speed  */
#define VHR_PLAYER_SPEED_DEFAULT (1.0f)    /**< Normal playback speed        */

/* =============================================================================
 * Playback State Machine — 7 States
 *
 *  IDLE → (load + valid CRC) → LOADING → READY
 *          magic fail / CRC fail → IDLE         │
 *                                             play()
 *                                               ▼
 *                               PAUSED ←── PLAYING ──→ FINISHED
 *                                play() ──►  │             │
 *                                            │ stop()      │ stop()
 *                                            ▼             ▼
 *                                          READY         READY
 * ============================================================================= */

typedef enum {
    VHR_PLAYER_STATE_IDLE     = 0,  /**< No file loaded; no memory bound */
    VHR_PLAYER_STATE_LOADING  = 1,  /**< Parsing header, validating CRC-32 */
    VHR_PLAYER_STATE_READY    = 2,  /**< File valid; cursor at event[0]; awaiting play() */
    VHR_PLAYER_STATE_PLAYING  = 3,  /**< Actively advancing cursor and firing events */
    VHR_PLAYER_STATE_PAUSED   = 4,  /**< Cursor frozen; accumulatedUs preserved */
    VHR_PLAYER_STATE_STOPPING = 5,  /**< (Reserved for async stop signal) */
    VHR_PLAYER_STATE_FINISHED = 6   /**< All events fired; awaiting stop() or load() */
} vhr_player_state_t;

/* =============================================================================
 * Player Engine Struct (~64 bytes, zero heap allocation)
 *
 * Memory model: pHeader and pEventStream are const pointers into a caller-owned
 * buffer (Flash-mapped or RAM). The player never copies the event stream.
 * ============================================================================= */

typedef struct {
    /* Bound file data (pointer-only, no copy) */
    const VHRFileHeader*  pHeader;         /**< Pointer to validated VHR header */
    const VHREventEntry*  pEventStream;    /**< Pointer to first event entry */
    uint32_t              totalEvents;     /**< Copy of pHeader->total_events */

    /* Timing cursor */
    uint32_t              cursorIdx;       /**< Index of next event to check/fire */
    uint64_t              accumulatedUs;   /**< Total elapsed playback time (µs) */
    uint64_t              cursorTimeUs;    /**< Absolute time of last fired event (µs) */
    float                 speedMultiplier; /**< Playback speed (clamped [0.25, 4.0]) */

    /* State */
    vhr_player_state_t    state;

    /* Diagnostics */
    uint32_t              eventsFireCount;    /**< NOTE_ON/OFF injected onto Event Bus */
    uint32_t              eventsSkippedCount; /**< Unknown/future event types silently skipped */
    uint32_t              errorCount;         /**< Malformed event entries skipped */
} vhl_vhr_player_t;

/* =============================================================================
 * Public API
 * ============================================================================= */

/**
 * Initialize player to IDLE state. Must be called before any other API.
 * @param pPlayer  Caller-owned player struct.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_init(vhl_vhr_player_t* pPlayer);

/**
 * Load and validate a .vhr binary buffer.
 * Validates: magic "VHR1", version_major == VHR_VERSION_MAJOR, and CRC-32
 * computed over the event stream bytes (not the header itself).
 * On success → state = READY, cursor = 0.
 * On failure → state = IDLE, returns error code.
 *
 * @param pPlayer      Player struct.
 * @param pVhrBuffer   Pointer to the full .vhr binary (header + events).
 * @param bufferSize   Total byte length of the buffer.
 * @return VHL_OK, VHL_ERR_INVALID_ARG, VHL_ERR_VHL_INVALID_MAGIC,
 *         VHL_ERR_NOT_SUPPORTED (version mismatch), VHL_ERR_VHL_CRC_MISMATCH.
 */
vhl_err_t vhr_player_load(vhl_vhr_player_t* pPlayer, const void* pVhrBuffer, size_t bufferSize);

/**
 * Start or resume playback. Valid from READY or PAUSED states.
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
 * Resets: cursorIdx=0, accumulatedUs=0, cursorTimeUs=0, eventsFireCount=0.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_stop(vhl_vhr_player_t* pPlayer);

/**
 * Advance the playback cursor by elapsedUs microseconds.
 * Fires all events whose cumulative timestamp <= accumulatedUs via Event Bus.
 * No-op if state != PLAYING. Transitions to FINISHED when all events are fired.
 *
 * Called by StorageTask (Core 0) on each scheduling tick.
 * Must be < 1ms even when 16 events fire simultaneously.
 *
 * @param pPlayer    Player struct.
 * @param elapsedUs  Microseconds elapsed since the last tick() call.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_tick(vhl_vhr_player_t* pPlayer, uint32_t elapsedUs);

/**
 * Seek to a specific event index. Recomputes cursorTimeUs by scanning from 0.
 * Sets accumulatedUs = cursorTimeUs so playback resumes from that point.
 * Valid from READY, PLAYING, PAUSED, or FINISHED states.
 * @return VHL_OK or VHL_ERR_INVALID_STATE.
 */
vhl_err_t vhr_player_seek(vhl_vhr_player_t* pPlayer, uint32_t eventIdx);

/**
 * Set playback speed multiplier. Clamped to [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX].
 * Can be called at any time (including during playback).
 * @param speed  Desired speed (e.g., 0.5f = half speed, 2.0f = double speed).
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhr_player_set_speed(vhl_vhr_player_t* pPlayer, float speed);

/**
 * Query current playback state.
 * @return VHR_PLAYER_STATE_IDLE if pPlayer is NULL.
 */
vhr_player_state_t vhr_player_get_state(const vhl_vhr_player_t* pPlayer);

/**
 * Query playback progress.
 * @param outCurrent  (Optional) Current event index (next to fire).
 * @param outTotal    (Optional) Total events in the loaded file.
 */
void vhr_player_get_progress(const vhl_vhr_player_t* pPlayer,
                              uint32_t* outCurrent,
                              uint32_t* outTotal);

#ifdef __cplusplus
}
#endif

#endif /* VHR_PLAYER_H */
