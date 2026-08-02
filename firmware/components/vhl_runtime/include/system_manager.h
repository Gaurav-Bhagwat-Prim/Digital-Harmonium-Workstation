/* =============================================================================
 * system_manager.h — Global System Runtime State Machine Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSTEM_STATE_BOOT = 0,
    SYSTEM_STATE_IDLE,
    SYSTEM_STATE_PERFORMING,
    SYSTEM_STATE_LOADING_INST,
    SYSTEM_STATE_RECORDING,
    SYSTEM_STATE_ERROR
} vhl_system_state_t;

vhl_err_t          vhl_system_init(void);
vhl_system_state_t vhl_system_get_state(void);
vhl_err_t          vhl_system_set_state(vhl_system_state_t newState);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MANAGER_H */
