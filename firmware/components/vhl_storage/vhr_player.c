/* =============================================================================
 * vhr_player.c — VHR Performance Playback Replayer Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 12 — VHR Performance Playback Replayer Engine
 *
 * Design Invariants:
 *   1. The player NEVER calls the DSP engine directly.
 *      All events are re-injected via vhl_event_bus_post_key_event() — the same
 *      path taken by live keyboard input. The synthesizer cannot distinguish
 *      a replayed event from a live one.
 *   2. Zero heap allocation. pHeader / pEventStream are const pointers into
 *      a caller-owned buffer.
 *   3. speedMultiplier is always clamped to [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX].
 *   4. Unknown future vhr_event_type_t values are skipped silently —
 *      eventsSkippedCount increments. No fatal errors for forward compatibility.
 *   5. CRC-32 is verified over the event stream bytes (not the header).
 * ============================================================================= */

#include "vhr_player.h"
#include "event_bus.h"
#include <string.h>

/* =============================================================================
 * CRC-32 Helper (IEEE 802.3 polynomial 0xEDB88320, same as recorder)
 *
 * Full-buffer variant: compute CRC-32 over [pData, pData+length).
 * The recorder uses incremental updates; both produce identical results
 * for the same byte sequence.
 * ============================================================================= */

static uint32_t crc32_compute(const uint8_t* pData, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= pData[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* =============================================================================
 * Speed Clamp Helper
 * ============================================================================= */

static float clamp_speed(float speed)
{
    if (speed < VHR_PLAYER_SPEED_MIN) return VHR_PLAYER_SPEED_MIN;
    if (speed > VHR_PLAYER_SPEED_MAX) return VHR_PLAYER_SPEED_MAX;
    return speed;
}

/* =============================================================================
 * Public API Implementation
 * ============================================================================= */

vhl_err_t vhr_player_init(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    memset(pPlayer, 0, sizeof(vhl_vhr_player_t));
    pPlayer->speedMultiplier = VHR_PLAYER_SPEED_DEFAULT;
    pPlayer->state           = VHR_PLAYER_STATE_IDLE;

    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_load — Validate and bind a .vhr binary buffer
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_load(vhl_vhr_player_t* pPlayer, const void* pVhrBuffer, size_t bufferSize)
{
    if (!pPlayer || !pVhrBuffer) return VHL_ERR_INVALID_ARG;
    if (bufferSize < sizeof(VHRFileHeader)) return VHL_ERR_INVALID_ARG;

    pPlayer->state = VHR_PLAYER_STATE_LOADING;

    const VHRFileHeader* pHdr = (const VHRFileHeader*)pVhrBuffer;

    /* 1. Validate magic bytes */
    if (pHdr->magic[0] != VHR_MAGIC_0 || pHdr->magic[1] != VHR_MAGIC_1 ||
        pHdr->magic[2] != VHR_MAGIC_2 || pHdr->magic[3] != VHR_MAGIC_3) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_VHL_INVALID_MAGIC;
    }

    /* 2. Validate major version for forward compatibility.
     *    Accept any minor version (1.0, 1.1, 1.x …).
     *    Reject different major versions cleanly. */
    if (pHdr->version_major != VHR_VERSION_MAJOR) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_NOT_SUPPORTED;
    }

    /* 3. Validate buffer size covers the declared event stream */
    size_t eventStreamBytes = (size_t)pHdr->total_events * sizeof(VHREventEntry);
    if (sizeof(VHRFileHeader) + eventStreamBytes > bufferSize) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_INVALID_ARG;
    }

    /* 4. CRC-32 computed over the event stream bytes (not the header).
     *    Must match header.crc32 which was written by the recorder's finalize_header(). */
    const VHREventEntry* pEvents = (const VHREventEntry*)(
        (const uint8_t*)pVhrBuffer + sizeof(VHRFileHeader)
    );
    uint32_t computedCrc = crc32_compute((const uint8_t*)pEvents, eventStreamBytes);

    if (computedCrc != pHdr->crc32) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_VHL_CRC_MISMATCH;
    }

    /* 5. All checks passed — bind pointers and reset cursor */
    pPlayer->pHeader      = pHdr;
    pPlayer->pEventStream = pEvents;
    pPlayer->totalEvents  = pHdr->total_events;

    pPlayer->cursorIdx       = 0U;
    pPlayer->accumulatedUs   = 0U;
    pPlayer->cursorTimeUs    = 0U;
    pPlayer->eventsFireCount    = 0U;
    pPlayer->eventsSkippedCount = 0U;
    pPlayer->errorCount      = 0U;

    pPlayer->state = VHR_PLAYER_STATE_READY;
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_play — Start or resume playback
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_play(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state != VHR_PLAYER_STATE_READY &&
        pPlayer->state != VHR_PLAYER_STATE_PAUSED) {
        return VHL_ERR_INVALID_STATE;
    }

    pPlayer->state = VHR_PLAYER_STATE_PLAYING;
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_pause — Freeze timing cursor
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_pause(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state != VHR_PLAYER_STATE_PLAYING) {
        return VHL_ERR_INVALID_STATE;
    }

    pPlayer->state = VHR_PLAYER_STATE_PAUSED;
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_stop — Reset cursor to 0, return to READY
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_stop(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    pPlayer->cursorIdx     = 0U;
    pPlayer->accumulatedUs = 0U;
    pPlayer->cursorTimeUs  = 0U;
    pPlayer->eventsFireCount = 0U;

    /* If a file is bound, return to READY; otherwise stay IDLE */
    if (pPlayer->state != VHR_PLAYER_STATE_IDLE) {
        pPlayer->state = VHR_PLAYER_STATE_READY;
    }

    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_tick — Advance timing cursor and fire due events onto Event Bus
 *
 * Algorithm:
 *   accumulatedUs += elapsedUs * speedMultiplier
 *   while (nextEventTime <= accumulatedUs): fire event, advance cursor
 *   if cursor >= totalEvents: FINISHED
 *
 * Performance: must complete in < 1 ms even when 16 events fire simultaneously.
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_tick(vhl_vhr_player_t* pPlayer, uint32_t elapsedUs)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    /* Only advance during PLAYING state */
    if (pPlayer->state != VHR_PLAYER_STATE_PLAYING) {
        return VHL_OK;
    }

    /* Advance accumulator.
     * Cast to double for the multiply to avoid float precision loss on large elapsedUs,
     * then truncate back to uint64_t. */
    pPlayer->accumulatedUs += (uint64_t)((double)elapsedUs * (double)pPlayer->speedMultiplier);

    /* Fire all events whose absolute time has arrived */
    while (pPlayer->cursorIdx < pPlayer->totalEvents) {
        const VHREventEntry* pEv = &pPlayer->pEventStream[pPlayer->cursorIdx];

        /* Absolute time of this event = time of previous event + this event's delta */
        uint64_t eventAbsTime = pPlayer->cursorTimeUs + (uint64_t)pEv->delta_us;

        if (eventAbsTime > pPlayer->accumulatedUs) {
            break; /* Next event is in the future — stop here */
        }

        /* Advance absolute cursor time BEFORE firing (so timestamp is correct) */
        pPlayer->cursorTimeUs = eventAbsTime;
        pPlayer->cursorIdx++;

        /* Map VHREventEntry to vhl_key_event_t and inject onto Event Bus.
         * Only NOTE_ON and NOTE_OFF are mapped in Stage 12.
         * All other event types are skipped gracefully for forward compatibility.
         * Stage 13/14 will dispatch CONTROL_CHANGE, TEMPO_CHANGE, etc. */
        vhr_event_type_t evType = (vhr_event_type_t)pEv->type;

        if (evType == VHR_EVENT_NOTE_ON || evType == VHR_EVENT_NOTE_OFF) {
            vhl_key_event_t keyEv;
            memset(&keyEv, 0, sizeof(keyEv));
            keyEv.action      = (evType == VHR_EVENT_NOTE_ON) ? KEY_ACTION_PRESS : KEY_ACTION_RELEASE;
            keyEv.note        = pEv->data1;
            keyEv.velocity    = pEv->data2;
            keyEv.timestampMs = (uint32_t)(pPlayer->cursorTimeUs / 1000U);

            vhl_event_bus_post_key_event(&keyEv);
            pPlayer->eventsFireCount++;
        } else {
            /*
             * Unknown or future event type — skip gracefully.
             * This preserves backward/forward compatibility:
             * a Stage 12 firmware can play a Stage 14 .vhr file without crashing.
             */
            pPlayer->eventsSkippedCount++;
        }
    }

    /* Check for end-of-stream */
    if (pPlayer->cursorIdx >= pPlayer->totalEvents) {
        pPlayer->state = VHR_PLAYER_STATE_FINISHED;
    }

    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_seek — Jump to a specific event index
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_seek(vhl_vhr_player_t* pPlayer, uint32_t eventIdx)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state == VHR_PLAYER_STATE_IDLE ||
        pPlayer->state == VHR_PLAYER_STATE_LOADING) {
        return VHL_ERR_INVALID_STATE;
    }

    /* Clamp to valid range */
    if (eventIdx > pPlayer->totalEvents) {
        eventIdx = pPlayer->totalEvents;
    }

    /* Recompute cursorTimeUs by summing delta_us from the beginning */
    uint64_t absTime = 0U;
    for (uint32_t i = 0U; i < eventIdx; ++i) {
        absTime += (uint64_t)pPlayer->pEventStream[i].delta_us;
    }

    pPlayer->cursorIdx    = eventIdx;
    pPlayer->cursorTimeUs = absTime;
    pPlayer->accumulatedUs = absTime; /* Resume from sought position immediately */

    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_set_speed — Set and clamp speed multiplier
 * ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_set_speed(vhl_vhr_player_t* pPlayer, float speed)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;
    pPlayer->speedMultiplier = clamp_speed(speed);
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * vhr_player_get_state / vhr_player_get_progress — Read-only accessors
 * ----------------------------------------------------------------------------- */

vhr_player_state_t vhr_player_get_state(const vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHR_PLAYER_STATE_IDLE;
    return pPlayer->state;
}

void vhr_player_get_progress(const vhl_vhr_player_t* pPlayer,
                              uint32_t* outCurrent,
                              uint32_t* outTotal)
{
    if (!pPlayer) return;
    if (outCurrent) *outCurrent = pPlayer->cursorIdx;
    if (outTotal)   *outTotal   = pPlayer->totalEvents;
}
