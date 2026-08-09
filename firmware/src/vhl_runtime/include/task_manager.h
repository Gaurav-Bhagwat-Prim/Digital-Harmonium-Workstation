/* =============================================================================
 * task_manager.h — FreeRTOS Task Architecture & Dual-Core Affinity
 * Project: Digital Harmonium Workstation Firmware
 * Stage 3 — Task Architecture & Core Pinning Engine
 * ============================================================================= */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task Identifiers */
typedef enum {
    VHL_TASK_ID_AUDIO = 0,
    VHL_TASK_ID_INPUT,
    VHL_TASK_ID_UI,
    VHL_TASK_ID_LOADER,
    VHL_TASK_ID_STORAGE,
    VHL_TASK_ID_DIAG,
    VHL_TASK_MAX_TASKS
} vhl_task_id_t;

/* Task Information & Health Diagnostic Struct */
typedef struct {
    vhl_task_id_t taskId;
    const char*   taskName;
    uint32_t      coreId;
    uint32_t      priority;
    size_t        stackSizeBytes;
    size_t        stackHighWaterMarkBytes;
    uint32_t      lastHeartbeatMs;
    uint32_t      heartbeatTimeoutMs;
    bool          isStalled;
    bool          isRunning;
} vhl_task_info_t;

/* -----------------------------------------------------------------------------
 * Public Task API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize static task stack memory, core affinity matrix, and launch all 6 tasks */
vhl_err_t vhl_tasks_init(void);

/** Publish periodic heartbeat from running task loop */
vhl_err_t vhl_task_send_heartbeat(vhl_task_id_t taskId, uint32_t timestampMs);

/** Audit task heartbeats against timeout thresholds (Called by TASK_DIAG) */
vhl_err_t vhl_task_audit_heartbeats(uint32_t currentTimestampMs);

/** Thread-safe query for task information & stack high-water mark */
vhl_err_t vhl_task_get_info(vhl_task_id_t taskId, vhl_task_info_t* outInfo);

/** Query stack high-water mark in bytes for specified task */
size_t    vhl_task_get_stack_high_water_mark(vhl_task_id_t taskId);

/** Query CPU core affinity ID (0 or 1) for specified task */
uint32_t  vhl_task_get_core_affinity(vhl_task_id_t taskId);

#ifdef __cplusplus
}
#endif

#endif /* TASK_MANAGER_H */
