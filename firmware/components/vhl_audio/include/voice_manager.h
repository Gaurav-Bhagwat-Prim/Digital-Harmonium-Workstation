/* =============================================================================
 * voice_manager.h — Voice Pool Allocator & Stealing Manager Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

vhl_err_t vhl_voice_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_MANAGER_H */
