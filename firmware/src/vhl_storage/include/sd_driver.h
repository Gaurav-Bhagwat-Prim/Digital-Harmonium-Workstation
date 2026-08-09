/* =============================================================================
 * sd_driver.h — SPI SD Card Driver & File System Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef SD_DRIVER_H
#define SD_DRIVER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

vhl_err_t vhl_sd_driver_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_DRIVER_H */
