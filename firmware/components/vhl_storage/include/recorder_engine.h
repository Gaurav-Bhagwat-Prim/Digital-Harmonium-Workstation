/* =============================================================================
 * recorder_engine.h — Real-Time Lock-Free Event Recorder Engine Subsystem
 * Project: Digital Harmonium Workstation Firmware
 * Stage 11 — Event Recorder Engine & Performance Logging Subsystem
 * ============================================================================= */

#ifndef RECORDER_ENGINE_H
#define RECORDER_ENGINE_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhr_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 5-State Recorder State Machine */
typedef enum {
    VHR_RECORDER_STATE_IDLE = 0,
    VHR_RECORDER_STATE_ARMED,
    VHR_RECORDER_STATE_RECORDING,
    VHR_RECORDER_STATE_STOPPING,
    VHR_RECORDER_STATE_FLUSHING
} vhr_recorder_state_t;

/* Atomic Lock-Free Ring Buffer Structure */
typedef struct {
    VHREventEntry        ringBuffer[VHL_RECORDER_RING_SIZE];
    volatile uint32_t    head;
    volatile uint32_t    tail;
    vhr_recorder_state_t state;

    bool                 isFirstEvent;             /* True until first event is pushed */
    uint32_t             lastEventTimestampUs;
    uint32_t             recordingStartTimestampUs;
    uint32_t             totalEventsRecorded;
    uint32_t             overflowCount;
    uint32_t             peakRingDepth;
    uint8_t              activeInstrumentId;

    VHRFileHeader        fileHeader;
} vhl_recorder_engine_t;

/* -----------------------------------------------------------------------------
 * Public Recorder Engine API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize Event Recorder Engine Structure */
vhl_err_t vhl_recorder_init(vhl_recorder_engine_t* pRecorder);

/** Arm Recorder Engine (Transitions IDLE -> ARMED, waiting for first Note-On) */
vhl_err_t vhl_recorder_arm(vhl_recorder_engine_t* pRecorder, uint8_t instrumentId, const char* title);

/** Start Recording immediately (Transitions ARMED/IDLE -> RECORDING) */
vhl_err_t vhl_recorder_start(vhl_recorder_engine_t* pRecorder, uint8_t instrumentId, const char* title);

/** Push new event into SPSC lock-free ring buffer (<1us non-blocking execution) */
vhl_err_t vhl_recorder_push_event(vhl_recorder_engine_t* pRecorder,
                                  vhr_event_type_t type,
                                  uint8_t data1,
                                  uint8_t data2,
                                  uint8_t channel,
                                  uint32_t timestampUs);

/** Stop Recording (Transitions RECORDING -> STOPPING -> FLUSHING) */
vhl_err_t vhl_recorder_stop(vhl_recorder_engine_t* pRecorder);

/** Drain available event entries from ring buffer for Core 0 storage serialization */
vhl_err_t vhl_recorder_flush_block(vhl_recorder_engine_t* pRecorder,
                                   VHREventEntry* outBuffer,
                                   uint32_t maxEvents,
                                   uint32_t* outDrainedCount);

/** Finalize recording and write VHR File Header */
vhl_err_t vhl_recorder_finalize_header(vhl_recorder_engine_t* pRecorder, VHRFileHeader* outHeader);

/** Query current recorder state */
vhr_recorder_state_t vhl_recorder_get_state(const vhl_recorder_engine_t* pRecorder);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_ENGINE_H */
