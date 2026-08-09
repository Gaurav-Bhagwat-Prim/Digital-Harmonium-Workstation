/* =============================================================================
 * system_monitor.h — Task Stack & Heap Health Monitor Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

vhl_err_t vhl_system_monitor_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MONITOR_H */
