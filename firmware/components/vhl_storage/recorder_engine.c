/* =============================================================================
 * recorder_engine.c — Real-Time Lock-Free Event Recorder Engine Subsystem Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 11 — Event Recorder Engine & Performance Logging Subsystem
 * ============================================================================= */

#include "recorder_engine.h"
#include <string.h>

/* Helper: Calculate CRC-32 Checksum over byte buffer */
static uint32_t calculate_crc32(const uint8_t* pData, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= pData[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

vhl_err_t vhl_recorder_init(vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;
    memset(pRecorder, 0, sizeof(vhl_recorder_engine_t));
    pRecorder->state = VHR_RECORDER_STATE_IDLE;
    return VHL_OK;
}

vhl_err_t vhl_recorder_arm(vhl_recorder_engine_t* pRecorder, uint8_t instrumentId, const char* title)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    pRecorder->head                = 0;
    pRecorder->tail                = 0;
    pRecorder->totalEventsRecorded = 0;
    pRecorder->overflowCount       = 0;
    pRecorder->peakRingDepth       = 0;
    pRecorder->activeInstrumentId  = instrumentId;

    memset(&pRecorder->fileHeader, 0, sizeof(VHRFileHeader));
    pRecorder->fileHeader.magic[0]     = VHR_MAGIC_0;
    pRecorder->fileHeader.magic[1]     = VHR_MAGIC_1;
    pRecorder->fileHeader.magic[2]     = VHR_MAGIC_2;
    pRecorder->fileHeader.magic[3]     = VHR_MAGIC_3;
    pRecorder->fileHeader.version_major= VHR_VERSION_MAJOR;
    pRecorder->fileHeader.version_minor= VHR_VERSION_MINOR;
    pRecorder->fileHeader.instrument_id= instrumentId;
    pRecorder->fileHeader.timebase_us  = VHR_DEFAULT_TIMEBASE_US;
    pRecorder->fileHeader.ticks_per_quarter = VHR_DEFAULT_TICKS_PER_QN;

    if (title) {
        strncpy(pRecorder->fileHeader.title, title, sizeof(pRecorder->fileHeader.title) - 1);
    } else {
        strncpy(pRecorder->fileHeader.title, "Live Performance", sizeof(pRecorder->fileHeader.title) - 1);
    }

    pRecorder->state = VHR_RECORDER_STATE_ARMED;
    return VHL_OK;
}

vhl_err_t vhl_recorder_start(vhl_recorder_engine_t* pRecorder, uint8_t instrumentId, const char* title)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    if (pRecorder->state != VHR_RECORDER_STATE_ARMED) {
        vhl_recorder_arm(pRecorder, instrumentId, title);
    }

    pRecorder->isFirstEvent              = true;
    pRecorder->recordingStartTimestampUs  = 0;
    pRecorder->lastEventTimestampUs       = 0;
    pRecorder->state                      = VHR_RECORDER_STATE_RECORDING;

    return VHL_OK;
}

vhl_err_t vhl_recorder_push_event(vhl_recorder_engine_t* pRecorder,
                                  vhr_event_type_t type,
                                  uint8_t data1,
                                  uint8_t data2,
                                  uint8_t channel,
                                  uint32_t timestampUs)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    // State Transition: ARMED -> RECORDING on first NOTE_ON event
    if (pRecorder->state == VHR_RECORDER_STATE_ARMED) {
        if (type == VHR_EVENT_NOTE_ON) {
            pRecorder->state = VHR_RECORDER_STATE_RECORDING;
        } else {
            return VHL_OK; // Ignore non-note events while ARMED
        }
    }

    if (pRecorder->state != VHR_RECORDER_STATE_RECORDING) {
        return VHL_OK; // Passive mode: bypass ring buffer
    }

    /* Initialize timestamps on first event */
    if (pRecorder->isFirstEvent) {
        pRecorder->isFirstEvent              = false;
        pRecorder->recordingStartTimestampUs = timestampUs;
        pRecorder->lastEventTimestampUs      = timestampUs;
    }

    // Lock-Free SPSC Ring Buffer Push
    uint32_t currentHead = pRecorder->head;
    uint32_t nextHead     = (currentHead + 1U) % VHL_RECORDER_RING_SIZE;

    if (nextHead == pRecorder->tail) {
        pRecorder->overflowCount++;
        return VHL_ERR_QUEUE_FULL; // Buffer overflow (dropped event)
    }

    uint32_t deltaUs = (timestampUs >= pRecorder->lastEventTimestampUs)
                     ? (timestampUs - pRecorder->lastEventTimestampUs)
                     : 0U;
    pRecorder->lastEventTimestampUs = timestampUs;

    VHREventEntry* pSlot = &pRecorder->ringBuffer[currentHead];
    pSlot->delta_us = deltaUs;
    pSlot->type     = (uint8_t)type;
    pSlot->data1    = data1;
    pSlot->data2    = data2;
    pSlot->channel  = channel;

    // Atomic Head Increment
    pRecorder->head = nextHead;
    pRecorder->totalEventsRecorded++;

    // Track peak depth
    uint32_t depth = (pRecorder->head >= pRecorder->tail)
                   ? (pRecorder->head - pRecorder->tail)
                   : (VHL_RECORDER_RING_SIZE - pRecorder->tail + pRecorder->head);
    if (depth > pRecorder->peakRingDepth) {
        pRecorder->peakRingDepth = depth;
    }

    return VHL_OK;
}

vhl_err_t vhl_recorder_stop(vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHL_ERR_INVALID_ARG;

    if (pRecorder->state == VHR_RECORDER_STATE_RECORDING || pRecorder->state == VHR_RECORDER_STATE_ARMED) {
        pRecorder->state = VHR_RECORDER_STATE_STOPPING;
        pRecorder->state = VHR_RECORDER_STATE_FLUSHING;
    }

    return VHL_OK;
}

vhl_err_t vhl_recorder_flush_block(vhl_recorder_engine_t* pRecorder,
                                   VHREventEntry* outBuffer,
                                   uint32_t maxEvents,
                                   uint32_t* outDrainedCount)
{
    if (!pRecorder || !outBuffer || !outDrainedCount || maxEvents == 0) {
        return VHL_ERR_INVALID_ARG;
    }

    uint32_t drained = 0;
    uint32_t currentTail = pRecorder->tail;

    while (currentTail != pRecorder->head && drained < maxEvents) {
        outBuffer[drained++] = pRecorder->ringBuffer[currentTail];
        currentTail = (currentTail + 1U) % VHL_RECORDER_RING_SIZE;
    }

    pRecorder->tail = currentTail;
    *outDrainedCount = drained;

    // If STOPPING/FLUSHING and ring buffer is empty, return to IDLE
    if (pRecorder->state == VHR_RECORDER_STATE_FLUSHING && pRecorder->tail == pRecorder->head) {
        pRecorder->state = VHR_RECORDER_STATE_IDLE;
    }

    return VHL_OK;
}

vhl_err_t vhl_recorder_finalize_header(vhl_recorder_engine_t* pRecorder, VHRFileHeader* outHeader)
{
    if (!pRecorder || !outHeader) {
        return VHL_ERR_INVALID_ARG;
    }

    pRecorder->fileHeader.total_events    = pRecorder->totalEventsRecorded;
    pRecorder->fileHeader.duration_ms     = (pRecorder->lastEventTimestampUs - pRecorder->recordingStartTimestampUs) / 1000U;
    pRecorder->fileHeader.total_file_size = (uint32_t)sizeof(VHRFileHeader) + (pRecorder->totalEventsRecorded * (uint32_t)sizeof(VHREventEntry));

    // Calculate CRC-32 of header (with crc32 field zeroed)
    pRecorder->fileHeader.crc32 = calculate_crc32((const uint8_t*)&pRecorder->fileHeader, sizeof(VHRFileHeader));

    *outHeader = pRecorder->fileHeader;
    return VHL_OK;
}

vhr_recorder_state_t vhl_recorder_get_state(const vhl_recorder_engine_t* pRecorder)
{
    if (!pRecorder) return VHR_RECORDER_STATE_IDLE;
    return pRecorder->state;
}
