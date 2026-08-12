/* =============================================================================
 * event_bus.c — Inter-Task Queues & Event Bus Engine Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 4  — Event Bus Architecture
 * Stage 11 — Recorder Tap Hook Integration
 * Stage 13 — LED Tap Hook Integration
 * ============================================================================= */

#include "event_bus.h"
#include <string.h>

/* -----------------------------------------------------------------------------
 * Static Queue Diagnostics Instrumentation Table
 * ----------------------------------------------------------------------------- */

static vhl_queue_stats_t s_queue_stats[VHL_QUEUE_MAX_QUEUES];
static uint32_t          s_system_event_bits = 0;

/* Stage 11: Recorder Tap Hook State */
static vhl_recorder_tap_fn_t s_recorder_tap_fn  = NULL;
static void*                 s_recorder_tap_ctx  = NULL;

/* Stage 13: LED Tap Hook State
 * Fires on every key event (live or VHR playback re-injection) — same path. */
static vhl_led_tap_fn_t      s_led_tap_fn        = NULL;
static void*                 s_led_tap_ctx       = NULL;

/* -----------------------------------------------------------------------------
 * Ring Buffer Struct for Standalone Host Verification Mode
 * ----------------------------------------------------------------------------- */

typedef struct {
    uint8_t  buffer[VHL_QUEUE_CAP_KEY_EVENTS * sizeof(vhl_key_event_t)];
    size_t   head;
    size_t   tail;
    size_t   count;
    size_t   capacity;
    size_t   elemSize;
} vhl_static_ring_queue_t;

static vhl_static_ring_queue_t s_key_queue;
static vhl_static_ring_queue_t s_ui_queue;
static vhl_static_ring_queue_t s_storage_queue;

static void queue_init(vhl_static_ring_queue_t* q, size_t capacity, size_t elemSize)
{
    q->head     = 0;
    q->tail     = 0;
    q->count    = 0;
    q->capacity = capacity;
    q->elemSize = elemSize;
    memset(q->buffer, 0, sizeof(q->buffer));
}

static bool queue_push(vhl_static_ring_queue_t* q, const void* pElem, vhl_queue_id_t qId, bool dropOldestOnFull)
{
    if (q->count >= q->capacity) {
        s_queue_stats[qId].overflowCount++;
        s_queue_stats[qId].droppedMessageCount++;

        if (!dropOldestOnFull) {
            return false; // Drop newest
        }

        // Drop oldest element by advancing tail
        q->tail = (q->tail + 1) % q->capacity;
        q->count--;
    }

    // Copy element into buffer at head
    uint8_t* pDest = q->buffer + (q->head * q->elemSize);
    memcpy(pDest, pElem, q->elemSize);

    q->head = (q->head + 1) % q->capacity;
    q->count++;

    s_queue_stats[qId].totalSent++;
    s_queue_stats[qId].currentQueueDepth = (uint16_t)q->count;
    if (q->count > s_queue_stats[qId].peakQueueDepth) {
        s_queue_stats[qId].peakQueueDepth = (uint16_t)q->count;
    }

    return true;
}

static bool queue_pop(vhl_static_ring_queue_t* q, void* pOutElem, vhl_queue_id_t qId)
{
    if (q->count == 0) {
        return false;
    }

    const uint8_t* pSrc = q->buffer + (q->tail * q->elemSize);
    memcpy(pOutElem, pSrc, q->elemSize);

    q->tail = (q->tail + 1) % q->capacity;
    q->count--;

    s_queue_stats[qId].totalReceived++;
    s_queue_stats[qId].currentQueueDepth = (uint16_t)q->count;

    return true;
}

/* -----------------------------------------------------------------------------
 * Public Task-Context API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_init(void)
{
    memset(s_queue_stats, 0, sizeof(s_queue_stats));
    for (int i = 0; i < VHL_QUEUE_MAX_QUEUES; ++i) {
        s_queue_stats[i].queueId = (vhl_queue_id_t)i;
    }

    queue_init(&s_key_queue, VHL_QUEUE_CAP_KEY_EVENTS, sizeof(vhl_key_event_t));
    queue_init(&s_ui_queue, VHL_QUEUE_CAP_UI_CMDS, sizeof(vhl_ui_cmd_t));
    queue_init(&s_storage_queue, VHL_QUEUE_CAP_STORAGE, sizeof(vhl_storage_msg_t));

    s_system_event_bits = 0;

    /* Stage 11: clear recorder tap on (re-)init */
    s_recorder_tap_fn  = NULL;
    s_recorder_tap_ctx = NULL;

    /* Stage 13: clear LED tap on (re-)init */
    s_led_tap_fn  = NULL;
    s_led_tap_ctx = NULL;

    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * Stage 11: Recorder Tap Hook Registration
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_set_recorder_hook(vhl_recorder_tap_fn_t tapFn, void* pCtx)
{
    s_recorder_tap_fn  = tapFn;
    s_recorder_tap_ctx = pCtx;
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * Stage 13: LED Tap Hook Registration
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_set_led_hook(vhl_led_tap_fn_t tapFn, void* pCtx)
{
    s_led_tap_fn  = tapFn;
    s_led_tap_ctx = pCtx;
    return VHL_OK;
}

/* -----------------------------------------------------------------------------
 * Key Event Queue — posts to queue then fires recorder tap (non-blocking)
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_post_key_event(const vhl_key_event_t* pEvent)
{
    if (!pEvent) {
        return VHL_ERR_INVALID_ARG;
    }

    // Key Queue Overflow Policy: Drop newest if full (NoteOn/NoteOff are critical)
    bool success = queue_push(&s_key_queue, pEvent, VHL_QUEUE_ID_KEY_EVENTS, false);

    /*
     * Stage 11: Pure Event Bus-Driven Recorder Tap.
     * After enqueueing, fire the recorder tap callback if registered.
     * This is non-blocking (<1 us) — the recorder's push_event() is lock-free.
     * Playback (Stage 12) will re-inject events through this same path.
     */
    if (s_recorder_tap_fn) {
        s_recorder_tap_fn(pEvent, s_recorder_tap_ctx);
    }

    /*
     * Stage 13: LED Tap Hook.
     * Fires immediately after recorder tap. Also non-blocking (<1 us).
     * This drives the LED engine for both live play and VHR playback re-injection,
     * without the LED engine needing to know the event source.
     */
    if (s_led_tap_fn) {
        s_led_tap_fn(pEvent, s_led_tap_ctx);
    }

    return success ? VHL_OK : VHL_ERR_NO_MEM;
}

vhl_err_t vhl_event_bus_pop_key_event(vhl_key_event_t* pOutEvent, uint32_t timeoutMs)
{
    if (!pOutEvent) {
        return VHL_ERR_INVALID_ARG;
    }
    (void)timeoutMs; /* Non-blocking in host mode */
    bool success = queue_pop(&s_key_queue, pOutEvent, VHL_QUEUE_ID_KEY_EVENTS);
    return success ? VHL_OK : VHL_ERR_TIMEOUT;
}

vhl_err_t vhl_event_bus_post_ui_cmd(const vhl_ui_cmd_t* pCmd)
{
    if (!pCmd) {
        return VHL_ERR_INVALID_ARG;
    }
    bool success = queue_push(&s_ui_queue, pCmd, VHL_QUEUE_ID_UI_CMDS, true);
    return success ? VHL_OK : VHL_ERR_NO_MEM;
}

vhl_err_t vhl_event_bus_pop_ui_cmd(vhl_ui_cmd_t* pOutCmd, uint32_t timeoutMs)
{
    if (!pOutCmd) {
        return VHL_ERR_INVALID_ARG;
    }
    (void)timeoutMs;
    bool success = queue_pop(&s_ui_queue, pOutCmd, VHL_QUEUE_ID_UI_CMDS);
    return success ? VHL_OK : VHL_ERR_TIMEOUT;
}

vhl_err_t vhl_event_bus_post_storage_msg(const vhl_storage_msg_t* pMsg)
{
    if (!pMsg) {
        return VHL_ERR_INVALID_ARG;
    }
    bool success = queue_push(&s_storage_queue, pMsg, VHL_QUEUE_ID_STORAGE_MSGS, false);
    return success ? VHL_OK : VHL_ERR_NO_MEM;
}

vhl_err_t vhl_event_bus_pop_storage_msg(vhl_storage_msg_t* pOutMsg, uint32_t timeoutMs)
{
    if (!pOutMsg) {
        return VHL_ERR_INVALID_ARG;
    }
    (void)timeoutMs;
    bool success = queue_pop(&s_storage_queue, pOutMsg, VHL_QUEUE_ID_STORAGE_MSGS);
    return success ? VHL_OK : VHL_ERR_TIMEOUT;
}

vhl_err_t vhl_event_bus_set_bits(uint32_t bits)
{
    s_system_event_bits |= bits;
    return VHL_OK;
}

vhl_err_t vhl_event_bus_clear_bits(uint32_t bits)
{
    s_system_event_bits &= ~bits;
    return VHL_OK;
}

uint32_t vhl_event_bus_get_bits(void)
{
    return s_system_event_bits;
}

/* -----------------------------------------------------------------------------
 * Dedicated ISR-Safe API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_post_key_event_from_isr(const vhl_key_event_t* pEvent, bool* pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = false;
    }
    return vhl_event_bus_post_key_event(pEvent);
}

vhl_err_t vhl_event_bus_post_ui_cmd_from_isr(const vhl_ui_cmd_t* pCmd, bool* pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = false;
    }
    return vhl_event_bus_post_ui_cmd(pCmd);
}

vhl_err_t vhl_event_bus_set_bits_from_isr(uint32_t bits, bool* pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) {
        *pxHigherPriorityTaskWoken = false;
    }
    return vhl_event_bus_set_bits(bits);
}

/* -----------------------------------------------------------------------------
 * Event Bus Diagnostics Instrumentation API
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_event_bus_get_queue_stats(vhl_queue_id_t queueId, vhl_queue_stats_t* outStats)
{
    if (queueId >= VHL_QUEUE_MAX_QUEUES || !outStats) {
        return VHL_ERR_INVALID_ARG;
    }
    *outStats = s_queue_stats[queueId];
    return VHL_OK;
}
