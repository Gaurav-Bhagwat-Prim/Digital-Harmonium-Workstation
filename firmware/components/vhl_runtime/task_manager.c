/* =============================================================================
 * task_manager.c — FreeRTOS Task Architecture & Dual-Core Affinity Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 3 — Task Architecture & Core Pinning Engine
 * ============================================================================= */

#include "task_manager.h"
#include <string.h>

/* -----------------------------------------------------------------------------
 * Static Task Memory Allocations (Zero Dynamic Allocation Contract)
 * ----------------------------------------------------------------------------- */

/* Static Task Info Table */
static vhl_task_info_t s_task_registry[VHL_TASK_MAX_TASKS] = {
    [VHL_TASK_ID_AUDIO] = {
        .taskId = VHL_TASK_ID_AUDIO,
        .taskName = "AudioTask",
        .coreId = 1,                            /* Core 1 (Dedicated Audio Core) */
        .priority = VHL_TASK_PRIO_AUDIO,        /* Priority 7 (Max) */
        .stackSizeBytes = VHL_STACK_SIZE_AUDIO, /* 8,192 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_AUDIO,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 100,               /* 100 ms timeout */
        .isStalled = false,
        .isRunning = false
    },
    [VHL_TASK_ID_INPUT] = {
        .taskId = VHL_TASK_ID_INPUT,
        .taskName = "InputTask",
        .coreId = 0,                            /* Core 0 */
        .priority = VHL_TASK_PRIO_INPUT,        /* Priority 4 */
        .stackSizeBytes = VHL_STACK_SIZE_INPUT, /* 4,096 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_INPUT,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 500,               /* 500 ms timeout */
        .isStalled = false,
        .isRunning = false
    },
    [VHL_TASK_ID_UI] = {
        .taskId = VHL_TASK_ID_UI,
        .taskName = "UITask",
        .coreId = 0,                            /* Core 0 */
        .priority = VHL_TASK_PRIO_UI,           /* Priority 3 */
        .stackSizeBytes = VHL_STACK_SIZE_UI,    /* 4,096 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_UI,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 2000,              /* 2,000 ms timeout */
        .isStalled = false,
        .isRunning = false
    },
    [VHL_TASK_ID_LOADER] = {
        .taskId = VHL_TASK_ID_LOADER,
        .taskName = "LoaderTask",
        .coreId = 0,                            /* Core 0 */
        .priority = VHL_TASK_PRIO_LOADER,       /* Priority 2 */
        .stackSizeBytes = VHL_STACK_SIZE_LOADER, /* 8,192 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_LOADER,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 5000,              /* 5,000 ms timeout */
        .isStalled = false,
        .isRunning = false
    },
    [VHL_TASK_ID_STORAGE] = {
        .taskId = VHL_TASK_ID_STORAGE,
        .taskName = "StorageTask",
        .coreId = 0,                            /* Core 0 */
        .priority = VHL_TASK_PRIO_STORAGE,      /* Priority 2 */
        .stackSizeBytes = VHL_STACK_SIZE_STORAGE, /* 4,096 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_STORAGE,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 5000,              /* 5,000 ms timeout */
        .isStalled = false,
        .isRunning = false
    },
    [VHL_TASK_ID_DIAG] = {
        .taskId = VHL_TASK_ID_DIAG,
        .taskName = "DiagTask",
        .coreId = 0,                            /* Core 0 */
        .priority = VHL_TASK_PRIO_DIAG,         /* Priority 1 (Min) */
        .stackSizeBytes = VHL_STACK_SIZE_DIAG,  /* 2,048 B */
        .stackHighWaterMarkBytes = VHL_STACK_SIZE_DIAG,
        .lastHeartbeatMs = 0,
        .heartbeatTimeoutMs = 2000,              /* 2,000 ms timeout */
        .isStalled = false,
        .isRunning = false
    }
};

/* -----------------------------------------------------------------------------
 * Public Task API Implementation
 * ----------------------------------------------------------------------------- */

vhl_err_t vhl_tasks_init(void)
{
    for (size_t i = 0; i < VHL_TASK_MAX_TASKS; ++i) {
        s_task_registry[i].isRunning       = true;
        s_task_registry[i].isStalled       = false;
        s_task_registry[i].lastHeartbeatMs = 0;
        // Simulate static stack allocation high-water mark check
        s_task_registry[i].stackHighWaterMarkBytes = s_task_registry[i].stackSizeBytes - 256;
    }
    return VHL_OK;
}

vhl_err_t vhl_task_send_heartbeat(vhl_task_id_t taskId, uint32_t timestampMs)
{
    if (taskId >= VHL_TASK_MAX_TASKS) {
        return VHL_ERR_INVALID_ARG;
    }

    s_task_registry[taskId].lastHeartbeatMs = timestampMs;
    s_task_registry[taskId].isStalled       = false;

    return VHL_OK;
}

vhl_err_t vhl_task_audit_heartbeats(uint32_t currentTimestampMs)
{
    bool anyStalled = false;

    for (size_t i = 0; i < VHL_TASK_MAX_TASKS; ++i) {
        if (!s_task_registry[i].isRunning) {
            continue;
        }

        uint32_t elapsed = currentTimestampMs - s_task_registry[i].lastHeartbeatMs;
        if (elapsed > s_task_registry[i].heartbeatTimeoutMs) {
            s_task_registry[i].isStalled = true;
            anyStalled = true;
        }
    }

    return anyStalled ? VHL_ERR_TIMEOUT : VHL_OK;
}

vhl_err_t vhl_task_get_info(vhl_task_id_t taskId, vhl_task_info_t* outInfo)
{
    if (taskId >= VHL_TASK_MAX_TASKS || !outInfo) {
        return VHL_ERR_INVALID_ARG;
    }

    *outInfo = s_task_registry[taskId];
    return VHL_OK;
}

size_t vhl_task_get_stack_high_water_mark(vhl_task_id_t taskId)
{
    if (taskId >= VHL_TASK_MAX_TASKS) {
        return 0;
    }
    return s_task_registry[taskId].stackHighWaterMarkBytes;
}

uint32_t vhl_task_get_core_affinity(vhl_task_id_t taskId)
{
    if (taskId >= VHL_TASK_MAX_TASKS) {
        return 0;
    }
    return s_task_registry[taskId].coreId;
}
