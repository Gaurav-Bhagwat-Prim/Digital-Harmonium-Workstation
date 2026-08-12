/* =============================================================================
 * vhr_player.c — VHR Performance Playback Replayer Engine Implementation (v2.0)
 * Project: Digital Harmonium Workstation Firmware
 *
 * Design Invariants:
 *   1. The player NEVER calls the DSP engine directly.
 *      NOTE_ON/NOTE_OFF events are re-injected via vhl_event_bus_post_key_event().
 *   2. Zero heap allocation. pHeader/pEventStream are const pointers into a
 *      caller-owned buffer.
 *   3. speedMultiplier is always clamped to [VHR_PLAYER_SPEED_MIN, VHR_PLAYER_SPEED_MAX].
 *   4. Unknown or future vhr_event_type_t values are skipped silently —
 *      eventsSkippedCount++. No fatal errors for forward compatibility.
 *   5. CRC-32 is verified over the event stream bytes (not the header).
 *   6. Instrument resolution: user override → native VHL → current VHL → error.
 *   7. User overrides always have higher priority than recorded events.
 *   8. Storing an event type does NOT require its playback engine to be implemented.
 *      Unimplemented engines produce a graceful skip (eventsSkippedCount++).
 * ============================================================================= */

#include "vhr_player.h"
#include "event_bus.h"
#include <string.h>

/* =============================================================================
 * CRC-32 Helper (IEEE 802.3 polynomial 0xEDB88320)
 *
 * Full-buffer variant: compute CRC-32 over [pData, pData+length).
 * The recorder uses incremental updates; both produce identical results for
 * the same byte sequence when initialized at 0xFFFFFFFF and finalized with ~crc.
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
 * Internal: Apply playback overrides + header values to initialize musical state.
 * Called once during vhr_player_play() from READY state.
 * Priority: override → header value → system default (hardcoded).
 * ============================================================================= */

static void apply_initial_musical_state(vhl_vhr_player_t* pPlayer)
{
    /* Tempo */
    if (pPlayer->overrides.has_tempo) {
        pPlayer->currentTempo_bpm_x10 = pPlayer->overrides.override_tempo_bpm_x10;
    } else if (pPlayer->pHeader->native_tempo_bpm_x10 != 0) {
        pPlayer->currentTempo_bpm_x10 = pPlayer->pHeader->native_tempo_bpm_x10;
    } else {
        pPlayer->currentTempo_bpm_x10 = 800U; /* Default: 80.0 BPM */
    }

    /* Transpose */
    if (pPlayer->overrides.has_transpose) {
        pPlayer->currentTranspose = pPlayer->overrides.override_transpose;
    } else {
        pPlayer->currentTranspose = pPlayer->pHeader->native_transpose;
    }

    /* Volume */
    if (pPlayer->overrides.has_volume) {
        pPlayer->currentVolume = pPlayer->overrides.override_volume;
    } else if (pPlayer->pHeader->native_volume != 0) {
        pPlayer->currentVolume = pPlayer->pHeader->native_volume;
    } else {
        pPlayer->currentVolume = 100U; /* Default: ~78% volume */
    }

    /* Scale */
    pPlayer->currentScale = pPlayer->pHeader->native_scale;

    /* Speed multiplier */
    if (pPlayer->overrides.has_speed) {
        pPlayer->speedMultiplier = clamp_speed(pPlayer->overrides.override_speed);
    }
    /* else: speedMultiplier retains its last set value (default 1.0 from init) */
}

/* =============================================================================
 * Internal: Instrument resolution.
 *
 * Priority:
 *   1. User override (has_instrument=true) → resolvedInstrument = override
 *   2. Native instrument (header.native_instrument) → resolvedInstrument = native
 *   3. Currently selected system VHL → resolvedInstrument = "current"
 *
 * NOTE: In v2.0 of the player, the actual VHL file loading from SD card is
 * delegated to the caller via the resolvedInstrument string output. The player
 * populates resolvedInstrument; the Storage/Feature task is responsible for
 * actually loading the VHL using vhl_loader_init(). The player does not call
 * vhl_loader directly to preserve component separation and avoid SD I/O inside
 * the player struct.
 *
 * In a future integration, this function will:
 *   - Check SD card for /instruments/<name>.vhl
 *   - If found: record "resolved = native"
 *   - If not found: record "resolved = current" and caller uses active VHL
 *
 * For now: populate resolvedInstrument and return VHL_OK.
 * The caller must check resolvedInstrument and load/switch the VHL.
 * ============================================================================= */

static vhl_err_t resolve_instrument(vhl_vhr_player_t* pPlayer)
{
    if (pPlayer->overrides.has_instrument &&
        pPlayer->overrides.override_instrument[0] != '\0') {
        /* Priority 1: User override */
        strncpy(pPlayer->resolvedInstrument,
                pPlayer->overrides.override_instrument,
                VHR_INSTRUMENT_NAME_LEN - 1);
        pPlayer->resolvedInstrument[VHR_INSTRUMENT_NAME_LEN - 1] = '\0';

    } else if (pPlayer->pHeader->native_instrument[0] != '\0') {
        /* Priority 2: VHR native instrument (metadata, not a hard dependency) */
        strncpy(pPlayer->resolvedInstrument,
                pPlayer->pHeader->native_instrument,
                VHR_INSTRUMENT_NAME_LEN - 1);
        pPlayer->resolvedInstrument[VHR_INSTRUMENT_NAME_LEN - 1] = '\0';

    } else {
        /* Priority 3: Currently selected system VHL — signal with empty string.
         * The caller is responsible for using whatever VHL is currently active. */
        pPlayer->resolvedInstrument[0] = '\0';
    }

    return VHL_OK;
}

/* =============================================================================
 * Public API Implementation
 * ============================================================================= */

vhl_err_t vhr_player_init(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    memset(pPlayer, 0, sizeof(vhl_vhr_player_t));
    pPlayer->speedMultiplier      = VHR_PLAYER_SPEED_DEFAULT;
    pPlayer->state                = VHR_PLAYER_STATE_IDLE;

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_load(vhl_vhr_player_t* pPlayer,
                           const void* pVhrBuffer,
                           size_t bufferSize)
{
    if (!pPlayer || !pVhrBuffer) return VHL_ERR_INVALID_ARG;
    if (bufferSize < sizeof(VHRFileHeader))  return VHL_ERR_INVALID_ARG;

    pPlayer->state = VHR_PLAYER_STATE_LOADING;

    const VHRFileHeader* pHdr = (const VHRFileHeader*)pVhrBuffer;

    /* 1. Validate magic bytes ('V','H','R','2') */
    if (pHdr->magic[0] != VHR_MAGIC_0 || pHdr->magic[1] != VHR_MAGIC_1 ||
        pHdr->magic[2] != VHR_MAGIC_2 || pHdr->magic[3] != VHR_MAGIC_3) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_VHL_INVALID_MAGIC;
    }

    /* 2. Validate version_major. Accept any version_minor (additive changes only).
     *    v1.0 files ('VHR1') are rejected by the magic check above. */
    if (pHdr->version_major != VHR_VERSION_MAJOR) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_NOT_SUPPORTED;
    }

    /* 3. Validate buffer covers declared event stream */
    size_t eventStreamBytes = (size_t)pHdr->total_events * sizeof(VHREventEntry);
    if (sizeof(VHRFileHeader) + eventStreamBytes > bufferSize) {
        pPlayer->state = VHR_PLAYER_STATE_IDLE;
        return VHL_ERR_INVALID_ARG;
    }

    /* 4. CRC-32 over event stream bytes (NOT the header) */
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

    pPlayer->cursorIdx          = 0U;
    pPlayer->accumulatedUs      = 0U;
    pPlayer->cursorTimeUs       = 0U;
    pPlayer->eventsFireCount    = 0U;
    pPlayer->eventsSkippedCount = 0U;
    pPlayer->eventsAppliedCount = 0U;
    pPlayer->errorCount         = 0U;

    /* Clear resolved instrument until play() runs resolution */
    memset(pPlayer->resolvedInstrument, 0, VHR_INSTRUMENT_NAME_LEN);

    pPlayer->state = VHR_PLAYER_STATE_READY;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_set_overrides(vhl_vhr_player_t* pPlayer,
                                    const vhr_playback_overrides_t* pOverrides)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pOverrides) {
        pPlayer->overrides = *pOverrides; /* shallow copy */
    } else {
        /* Clear all overrides */
        memset(&pPlayer->overrides, 0, sizeof(vhr_playback_overrides_t));
    }

    /* If a speed override is active, apply it immediately */
    if (pPlayer->overrides.has_speed) {
        pPlayer->speedMultiplier = clamp_speed(pPlayer->overrides.override_speed);
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_play(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state != VHR_PLAYER_STATE_READY &&
        pPlayer->state != VHR_PLAYER_STATE_PAUSED) {
        return VHL_ERR_INVALID_STATE;
    }

    /* On first play from READY: resolve instrument and initialize musical state */
    if (pPlayer->state == VHR_PLAYER_STATE_READY) {
        resolve_instrument(pPlayer);
        apply_initial_musical_state(pPlayer);
    }
    /* On resume from PAUSED: retain current musical state — don't reinitialize */

    pPlayer->state = VHR_PLAYER_STATE_PLAYING;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_pause(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state != VHR_PLAYER_STATE_PLAYING) {
        return VHL_ERR_INVALID_STATE;
    }

    pPlayer->state = VHR_PLAYER_STATE_PAUSED;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_stop(vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    pPlayer->cursorIdx          = 0U;
    pPlayer->accumulatedUs      = 0U;
    pPlayer->cursorTimeUs       = 0U;
    pPlayer->eventsFireCount    = 0U;
    pPlayer->eventsAppliedCount = 0U;
    pPlayer->eventsSkippedCount = 0U;

    /* Reset musical state to initial (will be re-resolved on next play()) */
    pPlayer->currentTempo_bpm_x10 = 0;
    pPlayer->currentTranspose     = 0;
    pPlayer->currentVolume        = 0;
    pPlayer->currentScale         = 0;
    memset(pPlayer->resolvedInstrument, 0, VHR_INSTRUMENT_NAME_LEN);

    if (pPlayer->state != VHR_PLAYER_STATE_IDLE) {
        pPlayer->state = VHR_PLAYER_STATE_READY;
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_tick(vhl_vhr_player_t* pPlayer, uint32_t elapsedUs)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;

    if (pPlayer->state != VHR_PLAYER_STATE_PLAYING) {
        return VHL_OK; /* No-op outside PLAYING state */
    }

    /* Advance time accumulator.
     * Cast to double for multiply to prevent float32 precision loss on large us values. */
    pPlayer->accumulatedUs += (uint64_t)((double)elapsedUs *
                                          (double)pPlayer->speedMultiplier);

    /* Fire all events whose absolute time has arrived */
    while (pPlayer->cursorIdx < pPlayer->totalEvents) {

        const VHREventEntry* pEv = &pPlayer->pEventStream[pPlayer->cursorIdx];

        /* Absolute time of this event = previous cursor + this event's delta */
        uint64_t eventAbsTime = pPlayer->cursorTimeUs + (uint64_t)pEv->delta_us;

        if (eventAbsTime > pPlayer->accumulatedUs) {
            break; /* Next event is in the future — stop here */
        }

        /* Advance absolute cursor time BEFORE processing (so timestamp is accurate) */
        pPlayer->cursorTimeUs = eventAbsTime;
        pPlayer->cursorIdx++;

        /* ── Event Dispatch ─────────────────────────────────────────────────────
         *
         * Policy (strictly ordered):
         *   1. Lifecycle events (START_RECORD, STOP_RECORD): skip, informational only.
         *   2. NOTE_ON / NOTE_OFF: always injected onto Event Bus.
         *   3. Musical param events (TEMPO, TRANSPOSE, VOLUME, SCALE, TUNING):
         *      - Record value into current musical state.
         *      - If user has an override for this parameter, the override value is
         *        what was applied at play() start; the recorded event still updates
         *        the "background" state but does NOT override the user override.
         *   4. Other event types (PEDAL, CC, PROGRAM, INSTRUMENT_CHANGE, etc.):
         *      - If a runtime engine is ready to handle them, dispatch.
         *      - If no engine is ready: skip gracefully (eventsSkippedCount++).
         *   5. Unknown event types (future format extensions): skip always.
         *
         * ────────────────────────────────────────────────────────────────────── */

        vhr_event_type_t evType = (vhr_event_type_t)pEv->type;

        switch (evType) {

            /* ── Lifecycle markers — informational, never re-injected ── */
            case VHR_EVENT_START_RECORD:
            case VHR_EVENT_STOP_RECORD:
                /* Intentionally skip; these are stream markers, not playback commands */
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Note Events — re-inject onto Event Bus ── */
            case VHR_EVENT_NOTE_ON:
            case VHR_EVENT_NOTE_OFF: {
                vhl_key_event_t keyEv;
                memset(&keyEv, 0, sizeof(keyEv));
                keyEv.action      = (evType == VHR_EVENT_NOTE_ON) ? KEY_ACTION_PRESS
                                                                   : KEY_ACTION_RELEASE;
                keyEv.note        = pEv->data1;
                keyEv.velocity    = pEv->data2;
                keyEv.timestampMs = (uint32_t)(pPlayer->cursorTimeUs / 1000U);

                vhl_event_bus_post_key_event(&keyEv);
                pPlayer->eventsFireCount++;
                break;
            }

            /* ── Tempo Change — update musical state; respect user override ── */
            case VHR_EVENT_TEMPO_CHANGE: {
                /* Decode: bpm_x10 = (data1 << 8) | data2 */
                uint16_t bpm_x10 = ((uint16_t)pEv->data1 << 8) | (uint16_t)pEv->data2;

                /* Always record the "native stream" value */
                if (!pPlayer->overrides.has_tempo) {
                    /* No override — apply recorded value as active tempo */
                    pPlayer->currentTempo_bpm_x10 = bpm_x10;
                }
                /* If has_tempo override is active: the override tempo remains current,
                 * but we still update a "native stream" tracker if needed in future.
                 * For now: override silently wins. */
                pPlayer->eventsAppliedCount++;
                break;
            }

            /* ── Transpose Change — update musical state; respect user override ── */
            case VHR_EVENT_TRANSPOSE_CHANGE: {
                int8_t semitones = (int8_t)pEv->data1;
                if (!pPlayer->overrides.has_transpose) {
                    pPlayer->currentTranspose = semitones;
                }
                pPlayer->eventsAppliedCount++;
                break;
            }

            /* ── Volume Change — update musical state; respect user override ── */
            case VHR_EVENT_VOLUME_CHANGE: {
                uint8_t vol = pEv->data1;
                if (!pPlayer->overrides.has_volume) {
                    pPlayer->currentVolume = vol;
                }
                pPlayer->eventsAppliedCount++;
                break;
            }

            /* ── Scale Change — always apply (no scale override in v2.0) ── */
            case VHR_EVENT_SCALE_CHANGE: {
                pPlayer->currentScale = pEv->data1;
                pPlayer->eventsAppliedCount++;
                /* Scale engine dispatch will be wired here when implemented */
                break;
            }

            /* ── Tuning Change — skipped until tuning engine is implemented ── */
            case VHR_EVENT_TUNING_CHANGE:
                /* Future: dispatch to pitch/tuning engine.
                 * data1 = coarse semitone offset (int8_t cast),
                 * data2 = fine tuning cents (int8_t cast). */
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Control Change — skipped until CC engine is implemented ── */
            case VHR_EVENT_CONTROL_CHANGE:
                /* Future: dispatch to CC/modulation engine.
                 * data1 = controller ID (MIDI CC), data2 = value (0-127). */
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Program Change — skipped until program routing is implemented ── */
            case VHR_EVENT_PROGRAM_CHANGE:
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Instrument Change (mid-stream) — skipped until routing implemented ── */
            case VHR_EVENT_INSTRUMENT_CHANGE:
                /* Future: trigger instrument resolution and VHL switch mid-performance.
                 * For now, the native instrument was resolved at play() start. */
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Pedal — skipped until pedal/sustain engine is implemented ── */
            case VHR_EVENT_PEDAL:
                /* Future: dispatch to sustain/pedal engine.
                 * data1 = pedal ID, data2 = value (0=off, 127=on). */
                pPlayer->eventsSkippedCount++;
                break;

            /* ── VHR_EVENT_NONE — always skip ── */
            case VHR_EVENT_NONE:
                pPlayer->eventsSkippedCount++;
                break;

            /* ── Unknown / Future event types — always skip gracefully ── */
            default:
                pPlayer->eventsSkippedCount++;
                break;
        }
    }

    /* Check end-of-stream */
    if (pPlayer->cursorIdx >= pPlayer->totalEvents) {
        pPlayer->state = VHR_PLAYER_STATE_FINISHED;
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

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

    /* Recompute absolute time at target by summing deltas from the start */
    uint64_t absTime = 0U;
    for (uint32_t i = 0U; i < eventIdx; ++i) {
        absTime += (uint64_t)pPlayer->pEventStream[i].delta_us;
    }

    pPlayer->cursorIdx     = eventIdx;
    pPlayer->cursorTimeUs  = absTime;
    pPlayer->accumulatedUs = absTime; /* Resume from sought position */

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhr_player_set_speed(vhl_vhr_player_t* pPlayer, float speed)
{
    if (!pPlayer) return VHL_ERR_INVALID_ARG;
    pPlayer->speedMultiplier          = clamp_speed(speed);
    pPlayer->overrides.has_speed      = true;
    pPlayer->overrides.override_speed = speed;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

const char* vhr_player_get_resolved_instrument(const vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return "";
    return pPlayer->resolvedInstrument;
}

/* ----------------------------------------------------------------------------- */

vhr_player_state_t vhr_player_get_state(const vhl_vhr_player_t* pPlayer)
{
    if (!pPlayer) return VHR_PLAYER_STATE_IDLE;
    return pPlayer->state;
}

/* ----------------------------------------------------------------------------- */

void vhr_player_get_progress(const vhl_vhr_player_t* pPlayer,
                              uint32_t* outCurrent,
                              uint32_t* outTotal)
{
    if (!pPlayer) return;
    if (outCurrent) *outCurrent = pPlayer->cursorIdx;
    if (outTotal)   *outTotal   = pPlayer->totalEvents;
}

/* ----------------------------------------------------------------------------- */

void vhr_player_get_musical_state(const vhl_vhr_player_t* pPlayer,
                                   uint16_t* outTempo_bpm_x10,
                                   int8_t*   outTranspose,
                                   uint8_t*  outVolume,
                                   uint8_t*  outScale)
{
    if (!pPlayer) return;
    if (outTempo_bpm_x10) *outTempo_bpm_x10 = pPlayer->currentTempo_bpm_x10;
    if (outTranspose)     *outTranspose      = pPlayer->currentTranspose;
    if (outVolume)        *outVolume         = pPlayer->currentVolume;
    if (outScale)         *outScale          = pPlayer->currentScale;
}
