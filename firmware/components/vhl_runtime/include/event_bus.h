/* =============================================================================
 * event_bus.h — Inter-Task Queues & Event Bus Engine
 * Project: Digital Harmonium Workstation Firmware
 * Stage 4 — Event Bus Architecture
 * Stage 11 — Recorder Tap Hook (Event Bus-Driven Recording Architecture)
 * ============================================================================= */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VHL_QUEUE_ID_KEY_EVENTS = 0,
    VHL_QUEUE_ID_UI_CMDS,
    VHL_QUEUE_ID_STORAGE_MSGS,
    VHL_QUEUE_MAX_QUEUES
} vhl_queue_id_t;

/* UI Command Payload (Copy-by-Value) */
typedef struct {
    uint8_t  commandId;
    uint8_t  screenId;
    uint16_t parameter;
    uint32_t timestampMs;
} vhl_ui_cmd_t;

/* Storage Message Payload (Pointer-Based) */
typedef struct {
    uint8_t   opCode;       /* 1 = Flush Recorder, 2 = Write Setting, 3 = Load VHL */
    uint8_t   reserved;
    uint16_t  dataLength;
    void*     pBuffer;
    uint32_t  timestampMs;
} vhl_storage_msg_t;

/* Diagnostic Queue Instrumentation Struct */
typedef struct {
    vhl_queue_id_t queueId;
    uint32_t       totalSent;
    uint32_t       totalReceived;
    uint32_t       overflowCount;
    uint32_t       droppedMessageCount;
    uint16_t       peakQueueDepth;
    uint16_t       currentQueueDepth;
} vhl_queue_stats_t;

/* System Event Bitmasks */
#define VHL_EVENT_BIT_AUDIO_READY     (1U << 0)
#define VHL_EVENT_BIT_INST_LOADED     (1U << 1)
#define VHL_EVENT_BIT_REC_STARTED     (1U << 2)
#define VHL_EVENT_BIT_REC_STOPPED     (1U << 3)
#define VHL_EVENT_BIT_SD_CARD_MOUNTED (1U << 4)
#define VHL_EVENT_BIT_SD_CARD_REMOVED (1U << 5)
#define VHL_EVENT_BIT_SYSTEM_ERROR    (1U << 6)

/* =============================================================================
 * Stage 11: Recorder Tap Hook
 * The recorder is the sole consumer of the Event Bus tap.
 * It never reads hardware inputs directly — all musical events flow through here.
 * Playback (Stage 12) will replay events back onto the Event Bus via the same path.
 * ============================================================================= */

/**
 * Recorder tap callback signature.
 * Called inside vhl_event_bus_post_key_event() immediately after the event is
 * enqueued. Must be lock-free and non-blocking (< 1 us execution time).
 * @param pEvent  Pointer to the key event that was just posted.
 * @param pCtx    Opaque context pointer supplied during registration (e.g. recorder engine instance).
 */
typedef void (*vhl_recorder_tap_fn_t)(const vhl_key_event_t* pEvent, void* pCtx);

/**
 * Register a recorder tap callback on the Event Bus.
 * Pass NULL for both arguments to deregister (disable recording tap).
 */
vhl_err_t vhl_event_bus_set_recorder_hook(vhl_recorder_tap_fn_t tapFn, void* pCtx);

/* -----------------------------------------------------------------------------
 * Task-Context APIs (Forbidden inside ISRs)
 * ----------------------------------------------------------------------------- */

/** Initialize static memory buffers for all queues and event groups */
vhl_err_t vhl_event_bus_init(void);

/** Key Event Queue Push/Pop (Copy-by-Value) */
vhl_err_t vhl_event_bus_post_key_event(const vhl_key_event_t* pEvent);
vhl_err_t vhl_event_bus_pop_key_event(vhl_key_event_t* pOutEvent, uint32_t timeoutMs);

/** UI Command Queue Push/Pop (Copy-by-Value) */
vhl_err_t vhl_event_bus_post_ui_cmd(const vhl_ui_cmd_t* pCmd);
vhl_err_t vhl_event_bus_pop_ui_cmd(vhl_ui_cmd_t* pOutCmd, uint32_t timeoutMs);

/** Storage Message Queue Push/Pop (Pointer-Based Payload) */
vhl_err_t vhl_event_bus_post_storage_msg(const vhl_storage_msg_t* pMsg);
vhl_err_t vhl_event_bus_pop_storage_msg(vhl_storage_msg_t* pOutMsg, uint32_t timeoutMs);

/** System Event Group Bitmask Operations */
vhl_err_t vhl_event_bus_set_bits(uint32_t bits);
vhl_err_t vhl_event_bus_clear_bits(uint32_t bits);
uint32_t  vhl_event_bus_get_bits(void);

/* -----------------------------------------------------------------------------
 * Dedicated ISR-Safe APIs (Mandatory for Interrupt Handlers)
 * ----------------------------------------------------------------------------- */
vhl_err_t vhl_event_bus_post_key_event_from_isr(const vhl_key_event_t* pEvent, bool* pxHigherPriorityTaskWoken);
vhl_err_t vhl_event_bus_post_ui_cmd_from_isr(const vhl_ui_cmd_t* pCmd, bool* pxHigherPriorityTaskWoken);
vhl_err_t vhl_event_bus_set_bits_from_isr(uint32_t bits, bool* pxHigherPriorityTaskWoken);

/* -----------------------------------------------------------------------------
 * Event Bus Diagnostics Instrumentation API
 * ----------------------------------------------------------------------------- */
vhl_err_t vhl_event_bus_get_queue_stats(vhl_queue_id_t queueId, vhl_queue_stats_t* outStats);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */
