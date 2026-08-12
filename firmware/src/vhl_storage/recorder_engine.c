/* =============================================================================
 * recorder_engine.c — Real-Time Lock-Free Event Recorder Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * VHR Format v2.0
 * ============================================================================= */

#include "recorder_engine.h"
#include <string.h>

/* =============================================================================
 * Internal CRC-32 Helper (IEEE 802.3, poly 0xEDB88320)
 * Incremental: call with successive byte ranges.
 * Initialize crc = 0xFFFFFFFF. Finalize with ~crc.
 * Must match the algorithm used in vhr_player.c for CRC verification.
 * ============================================================================= */

static uint32_t crc32_update_bytes(uint32_t crc, const uint8_t* pData, size_t length)
{
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
    return crc;
}

/* =============================================================================
 * Internal Helper: Map event type to its VHR_FLAG_* bitmask contribution.
 * Returns 0 for event types that do not set a format flag.
 * ============================================================================= */

static uint16_t event_type_to_flag(vhr_event_type_t type)
{
    switch (type) {
        case VHR_EVENT_TEMPO_CHANGE:      return VHR_FLAG_HAS_TEMPO_EVENTS;
        case VHR_EVENT_CONTROL_CHANGE:    return VHR_FLAG_HAS_CONTROL_EVENTS;
        case VHR_EVENT_INSTRUMENT_CHANGE: return VHR_FLAG_HAS_INSTRUMENT_EVENTS;
        case VHR_EVENT_TRANSPOSE_CHANGE:  return VHR_FLAG_HAS_TRANSPOSE_EVENTS;
        case VHR_EVENT_VOLUME_CHANGE:     return VHR_FLAG_HAS_VOLUME_EVENTS;
        case VHR_EVENT_SCALE_CHANGE:      return VHR_FLAG_HAS_SCALE_EVENTS;
        case VHR_EVENT_PEDAL:             return VHR_FLAG_HAS_PEDAL_EVENTS;
        default:                          return 0U;
    }
}

/* =============================================================================
 * Public API Implementation
 * ============================================================================= */

vhl_err_t vhl_recorder_init(vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;
    memset(pRecorder, 0, sizeof(vhl_recorder_engine_t));
    pRecorder->state = VHR_RECORDER_STATE_IDLE;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_arm(vhl_recorder_engine_t* pRecorder,
                            const char* instrumentName,
                            const char* title,
                            uint16_t    tempo_bpm_x10,
                            int8_t      transpose,
                            uint32_t    firmwareVersion)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    /* Reset ring buffer and statistics */
    pRecorder->head                = 0;
    pRecorder->tail                = 0;
    pRecorder->totalEventsRecorded = 0;
    pRecorder->overflowCount       = 0;
    pRecorder->peakRingDepth       = 0;
    pRecorder->activeFormatFlags   = 0;
    pRecorder->runningEventCrc     = 0xFFFFFFFFU;
    pRecorder->isFirstEvent        = true;
    pRecorder->recordingStartTimestampUs = 0;
    pRecorder->lastEventTimestampUs      = 0;

    /* Build VHR file header template */
    memset(&pRecorder->fileHeader, 0, sizeof(VHRFileHeader));

    pRecorder->fileHeader.magic[0]       = VHR_MAGIC_0;
    pRecorder->fileHeader.magic[1]       = VHR_MAGIC_1;
    pRecorder->fileHeader.magic[2]       = VHR_MAGIC_2;
    pRecorder->fileHeader.magic[3]       = VHR_MAGIC_3;
    pRecorder->fileHeader.version_major  = VHR_VERSION_MAJOR;
    pRecorder->fileHeader.version_minor  = VHR_VERSION_MINOR;
    pRecorder->fileHeader.timebase_us    = VHR_DEFAULT_TIMEBASE_US;
    pRecorder->fileHeader.ticks_per_quarter = VHR_DEFAULT_TICKS_PER_QN;
    pRecorder->fileHeader.firmware_version  = firmwareVersion;

    /* Native recording context */
    if (instrumentName && instrumentName[0] != '\0') {
        strncpy(pRecorder->fileHeader.native_instrument,
                instrumentName,
                VHR_INSTRUMENT_NAME_LEN - 1);
        pRecorder->fileHeader.native_instrument[VHR_INSTRUMENT_NAME_LEN - 1] = '\0';
    } else {
        strncpy(pRecorder->fileHeader.native_instrument, "unknown",
                VHR_INSTRUMENT_NAME_LEN - 1);
    }

    pRecorder->fileHeader.native_tempo_bpm_x10 = tempo_bpm_x10;
    pRecorder->fileHeader.native_transpose     = transpose;

    /* Performance title */
    if (title && title[0] != '\0') {
        strncpy(pRecorder->fileHeader.title, title, sizeof(pRecorder->fileHeader.title) - 1);
        pRecorder->fileHeader.title[sizeof(pRecorder->fileHeader.title) - 1] = '\0';
    } else {
        strncpy(pRecorder->fileHeader.title, "Live Performance",
                sizeof(pRecorder->fileHeader.title) - 1);
    }

    pRecorder->state = VHR_RECORDER_STATE_ARMED;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_start(vhl_recorder_engine_t* pRecorder,
                              const char* instrumentName,
                              const char* title,
                              uint16_t    tempo_bpm_x10,
                              int8_t      transpose,
                              uint32_t    firmwareVersion)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    /* Arm if not already armed (idempotent) */
    if (pRecorder->state != VHR_RECORDER_STATE_ARMED) {
        vhl_err_t err = vhl_recorder_arm(pRecorder, instrumentName, title,
                                          tempo_bpm_x10, transpose, firmwareVersion);
        if (err != VHL_OK) return err;
    }

    /* Transition directly to RECORDING without waiting for NOTE_ON */
    pRecorder->state = VHR_RECORDER_STATE_RECORDING;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_push_event(vhl_recorder_engine_t* pRecorder,
                                   vhr_event_type_t type,
                                   uint8_t data1,
                                   uint8_t data2,
                                   uint8_t channel,
                                   uint32_t timestampUs)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    /* ARMED: transition to RECORDING on first NOTE_ON; discard all other events */
    if (pRecorder->state == VHR_RECORDER_STATE_ARMED) {
        if (type == VHR_EVENT_NOTE_ON) {
            pRecorder->state = VHR_RECORDER_STATE_RECORDING;
            /* Fall through to push this NOTE_ON */
        } else {
            return VHL_OK; /* Silently discard */
        }
    }

    /* Only push while actively RECORDING */
    if (pRecorder->state != VHR_RECORDER_STATE_RECORDING) {
        return VHL_OK; /* Silently discard */
    }

    /* Initialize timestamps on the very first event of this recording */
    if (pRecorder->isFirstEvent) {
        pRecorder->isFirstEvent              = false;
        pRecorder->recordingStartTimestampUs = timestampUs;
        pRecorder->lastEventTimestampUs      = timestampUs;
    }

    /* Lock-Free SPSC Ring Buffer Push */
    uint32_t currentHead = pRecorder->head;
    uint32_t nextHead    = (currentHead + 1U) % VHR_RECORDER_RING_SIZE;

    if (nextHead == pRecorder->tail) {
        /* Ring buffer full — drop event, increment overflow counter */
        pRecorder->overflowCount++;
        return VHL_ERR_QUEUE_FULL;
    }

    /* Compute delta microseconds from previous event */
    uint32_t deltaUs = (timestampUs >= pRecorder->lastEventTimestampUs)
                     ? (timestampUs - pRecorder->lastEventTimestampUs)
                     : 0U;
    pRecorder->lastEventTimestampUs = timestampUs;

    /* Write event entry into ring slot */
    VHREventEntry* pSlot = &pRecorder->ringBuffer[currentHead];
    pSlot->delta_us = deltaUs;
    pSlot->type     = (uint8_t)type;
    pSlot->data1    = data1;
    pSlot->data2    = data2;
    pSlot->channel  = channel;

    /* Update format flags for this event type (bitwise OR — idempotent) */
    pRecorder->activeFormatFlags |= event_type_to_flag(type);

    /* Atomic head advance (single-writer SPSC: no CAS needed) */
    pRecorder->head = nextHead;
    pRecorder->totalEventsRecorded++;

    /* Track peak ring depth */
    uint32_t depth = (pRecorder->head >= pRecorder->tail)
                   ? (pRecorder->head  - pRecorder->tail)
                   : (VHR_RECORDER_RING_SIZE - pRecorder->tail + pRecorder->head);
    if (depth > pRecorder->peakRingDepth) {
        pRecorder->peakRingDepth = depth;
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_stop(vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    if (pRecorder->state == VHR_RECORDER_STATE_RECORDING ||
        pRecorder->state == VHR_RECORDER_STATE_ARMED) {
        pRecorder->state = VHR_RECORDER_STATE_STOPPING;
        pRecorder->state = VHR_RECORDER_STATE_FLUSHING;
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_flush_block(vhl_recorder_engine_t* pRecorder,
                                   VHREventEntry* outBuffer,
                                   uint32_t maxEvents,
                                   uint32_t* outDrainedCount)
{
    if (!pRecorder || !outBuffer || !outDrainedCount || maxEvents == 0) {
        return VHL_ERR_INVALID_ARG;
    }

    uint32_t drained     = 0;
    uint32_t currentTail = pRecorder->tail;

    while (currentTail != pRecorder->head && drained < maxEvents) {
        /* Copy event entry to caller-provided output buffer */
        outBuffer[drained] = pRecorder->ringBuffer[currentTail];

        /* Accumulate CRC-32 over this 8-byte event entry */
        pRecorder->runningEventCrc = crc32_update_bytes(
            pRecorder->runningEventCrc,
            (const uint8_t*)&pRecorder->ringBuffer[currentTail],
            sizeof(VHREventEntry)
        );

        drained++;
        currentTail = (currentTail + 1U) % VHR_RECORDER_RING_SIZE;
    }

    pRecorder->tail   = currentTail;
    *outDrainedCount  = drained;

    /* If FLUSHING and ring is now fully drained → return to IDLE */
    if (pRecorder->state == VHR_RECORDER_STATE_FLUSHING &&
        pRecorder->tail == pRecorder->head) {
        pRecorder->state = VHR_RECORDER_STATE_IDLE;
    }

    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhl_err_t vhl_recorder_finalize_header(vhl_recorder_engine_t* pRecorder,
                                        VHRFileHeader* outHeader)
{
    if (!pRecorder || !outHeader) return VHL_ERR_INVALID_ARG;

    uint32_t durationMs = 0;
    if (pRecorder->lastEventTimestampUs >= pRecorder->recordingStartTimestampUs) {
        durationMs = (pRecorder->lastEventTimestampUs -
                      pRecorder->recordingStartTimestampUs) / 1000U;
    }

    pRecorder->fileHeader.total_events    = pRecorder->totalEventsRecorded;
    pRecorder->fileHeader.duration_ms     = durationMs;
    pRecorder->fileHeader.total_file_size = (uint32_t)sizeof(VHRFileHeader) +
                                            (pRecorder->totalEventsRecorded *
                                             (uint32_t)sizeof(VHREventEntry));
    pRecorder->fileHeader.format_flags    = pRecorder->activeFormatFlags;

    /* CRC-32 finalized by bitwise NOT of accumulated running CRC.
     * The player recomputes the same CRC over the same event stream bytes. */
    pRecorder->fileHeader.crc32 = ~pRecorder->runningEventCrc;

    *outHeader = pRecorder->fileHeader;
    return VHL_OK;
}

/* ----------------------------------------------------------------------------- */

vhr_recorder_state_t vhl_recorder_get_state(const vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHR_RECORDER_STATE_IDLE;
    return pRecorder->state;
}

/* ----------------------------------------------------------------------------- */

void vhl_recorder_get_stats(const vhl_recorder_engine_t* pRecorder,
                             uint32_t* outTotal,
                             uint32_t* outDropped)
{
    if (!pRecorder) return;
    if (outTotal)   *outTotal   = pRecorder->totalEventsRecorded;
    if (outDropped) *outDropped = pRecorder->overflowCount;
}
