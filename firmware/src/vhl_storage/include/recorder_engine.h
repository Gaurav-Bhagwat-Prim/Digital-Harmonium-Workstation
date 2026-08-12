/* =============================================================================
 * recorder_engine.h — Real-Time Lock-Free Event Recorder Engine
 * Project: Digital Harmonium Workstation Firmware
 *
 * Architecture:
 *   - Events arrive via vhl_event_bus recorder tap hook (< 1µs, non-blocking).
 *   - Events are pushed into an SPSC lock-free ring buffer on the tap caller's core.
 *   - Storage Task (Core 0) drains the ring buffer and writes to SD card.
 *   - CRC-32 is accumulated incrementally during drain (no full-buffer CRC at end).
 *   - The header is written twice: once as placeholder at file open, and once
 *     finalized (with correct total_events, duration, CRC) at recording stop.
 *
 * Instrument Identity:
 *   - The recorder accepts an instrument name string (e.g. "harmonium") rather
 *     than a numeric ID. This name is written to VHRFileHeader.native_instrument
 *     and enables instrument-independent VHR playback.
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

/* =============================================================================
 * Recorder State Machine — 5 States
 *
 *  IDLE ─── arm() ──► ARMED ─── first NOTE_ON ──► RECORDING
 *                                                      │
 *                                                   stop()
 *                                                      │
 *                                              STOPPING → FLUSHING ─── (drain complete) ──► IDLE
 * ============================================================================= */

typedef enum {
    VHR_RECORDER_STATE_IDLE      = 0, /* Passive; events bypass ring buffer */
    VHR_RECORDER_STATE_ARMED     = 1, /* Waiting for first NOTE_ON to begin timestamping */
    VHR_RECORDER_STATE_RECORDING = 2, /* Active; events pushed to ring buffer */
    VHR_RECORDER_STATE_STOPPING  = 3, /* Stop signal received; ring buffer ingestion closed */
    VHR_RECORDER_STATE_FLUSHING  = 4  /* Draining remaining events; will write finalized header */
} vhr_recorder_state_t;

/* =============================================================================
 * Recorder Engine Struct
 *
 * Memory: Contains the SPSC ring buffer directly (512 × 8 bytes = 4096 bytes at default).
 *         The file header is assembled here and written to SD at finalization.
 *         No heap allocation.
 * ============================================================================= */

typedef struct {
    /* SPSC Ring Buffer */
    VHREventEntry        ringBuffer[VHR_RECORDER_RING_SIZE];
    volatile uint32_t    head;             /* Producer writes here (tap callback, any core) */
    volatile uint32_t    tail;             /* Consumer reads here (Storage Task, Core 0) */

    /* State */
    vhr_recorder_state_t state;

    /* Timestamp Tracking */
    bool                 isFirstEvent;
    uint32_t             lastEventTimestampUs;
    uint32_t             recordingStartTimestampUs;

    /* Statistics */
    uint32_t             totalEventsRecorded;
    uint32_t             overflowCount;         /* Ring buffer overflow (dropped events) */
    uint32_t             peakRingDepth;

    /* Format State */
    uint16_t             activeFormatFlags;      /* VHR_FLAG_* bits accumulated during recording */
    uint32_t             runningEventCrc;        /* Incremental CRC-32; init = 0xFFFFFFFF; finalize = ~crc */

    /* Header Template */
    VHRFileHeader        fileHeader;             /* Assembled during arm(); finalized in finalize_header() */

} vhl_recorder_engine_t;

/* =============================================================================
 * Public API
 * ============================================================================= */

/**
 * Initialize recorder engine struct to IDLE state. Zero all fields.
 * Must be called once before any other API.
 */
vhl_err_t vhl_recorder_init(vhl_recorder_engine_t* pRecorder);

/**
 * Arm recorder. Transitions IDLE → ARMED.
 * Clears ring buffer and prepares the VHR file header template.
 * Recording actually begins (ARMED → RECORDING) on the first NOTE_ON pushed.
 *
 * @param pRecorder       Recorder struct.
 * @param instrumentName  Null-terminated instrument name string (e.g. "harmonium").
 *                        Written to VHRFileHeader.native_instrument.
 *                        Pass NULL or "" for "unknown".
 * @param title           Null-terminated performance title string.
 *                        Written to VHRFileHeader.title.
 *                        Pass NULL for default "Live Performance".
 * @param tempo_bpm_x10   BPM × 10 at recording start (e.g. 800 = 80.0 BPM). 0 = unknown.
 * @param transpose       Semitone transpose at recording start (-12..+12).
 * @param firmwareVersion Firmware version identifier (diagnostic).
 */
vhl_err_t vhl_recorder_arm(vhl_recorder_engine_t* pRecorder,
                            const char* instrumentName,
                            const char* title,
                            uint16_t    tempo_bpm_x10,
                            int8_t      transpose,
                            uint32_t    firmwareVersion);

/**
 * Start recording immediately (bypasses ARMED wait-for-NOTE_ON).
 * Transitions IDLE or ARMED → RECORDING immediately.
 * Internally calls arm() if not already armed.
 */
vhl_err_t vhl_recorder_start(vhl_recorder_engine_t* pRecorder,
                              const char* instrumentName,
                              const char* title,
                              uint16_t    tempo_bpm_x10,
                              int8_t      transpose,
                              uint32_t    firmwareVersion);

/**
 * Push a new event into the SPSC ring buffer.
 * Must complete in < 1µs. Non-blocking. Lock-free.
 *
 * Behavior:
 *   - While ARMED: only transitions to RECORDING on VHR_EVENT_NOTE_ON; other events discarded.
 *   - While RECORDING: all events accepted (if ring buffer has space).
 *   - While IDLE/STOPPING/FLUSHING: returns VHL_OK (event silently discarded).
 *
 * The format_flags field in the header is updated atomically when special event
 * types are first seen (TEMPO_CHANGE, CONTROL_CHANGE, etc.).
 *
 * @param pRecorder    Recorder struct.
 * @param type         vhr_event_type_t value.
 * @param data1        Event data byte 1 (see vhr_event_type_t comments).
 * @param data2        Event data byte 2.
 * @param channel      Logical channel (0-15; use 0 currently).
 * @param timestampUs  Absolute microsecond timestamp from esp_timer_get_time() or equivalent.
 * @return VHL_OK, VHL_ERR_INVALID_ARG, or VHL_ERR_QUEUE_FULL (ring overflow; event dropped).
 */
vhl_err_t vhl_recorder_push_event(vhl_recorder_engine_t* pRecorder,
                                   vhr_event_type_t type,
                                   uint8_t data1,
                                   uint8_t data2,
                                   uint8_t channel,
                                   uint32_t timestampUs);

/**
 * Stop recording. Transitions RECORDING or ARMED → STOPPING → FLUSHING.
 * After this call, push_event() will silently discard further events.
 * The Storage Task must then call flush_block() until all remaining events
 * are drained, then call finalize_header().
 */
vhl_err_t vhl_recorder_stop(vhl_recorder_engine_t* pRecorder);

/**
 * Drain up to maxEvents entries from the ring buffer into outBuffer.
 * Called by the Storage Task on Core 0 to write events to the SD card.
 * Accumulates CRC-32 incrementally over drained bytes.
 * Transitions FLUSHING → IDLE when the ring buffer is fully drained.
 *
 * @param pRecorder       Recorder struct.
 * @param outBuffer       Caller-provided buffer of size >= maxEvents × sizeof(VHREventEntry).
 * @param maxEvents       Maximum number of events to drain per call.
 * @param outDrainedCount Set to the number of events actually drained.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhl_recorder_flush_block(vhl_recorder_engine_t* pRecorder,
                                   VHREventEntry* outBuffer,
                                   uint32_t maxEvents,
                                   uint32_t* outDrainedCount);

/**
 * Finalize the VHR file header after all events have been flushed.
 * Fills total_events, duration_ms, total_file_size, crc32, and format_flags.
 * The header returned in outHeader is ready to be written to offset 0 of the SD file.
 *
 * @param pRecorder  Recorder struct.
 * @param outHeader  Destination for the finalized 96-byte VHRFileHeader.
 * @return VHL_OK or VHL_ERR_INVALID_ARG.
 */
vhl_err_t vhl_recorder_finalize_header(vhl_recorder_engine_t* pRecorder,
                                        VHRFileHeader* outHeader);

/**
 * Query current recorder state.
 * @return VHR_RECORDER_STATE_IDLE if pRecorder is NULL.
 */
vhr_recorder_state_t vhl_recorder_get_state(const vhl_recorder_engine_t* pRecorder);

/**
 * Query recording statistics.
 * @param outTotal    (Optional) Total events recorded so far.
 * @param outDropped  (Optional) Total events dropped due to ring overflow.
 */
void vhl_recorder_get_stats(const vhl_recorder_engine_t* pRecorder,
                             uint32_t* outTotal,
                             uint32_t* outDropped);

#ifdef __cplusplus
}
#endif

#endif /* RECORDER_ENGINE_H */
