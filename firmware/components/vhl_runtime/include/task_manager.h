/* =============================================================================
 * task_manager.h — FreeRTOS Dual-Core Task Allocation Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

vhl_err_t vhl_tasks_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_MANAGER_H */
