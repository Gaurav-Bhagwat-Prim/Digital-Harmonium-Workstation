/* =============================================================================
 * gpio_driver.h — GPIO Matrix & Hardware Pin Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize GPIO matrix pins and input interrupts */
vhl_err_t vhl_hal_gpio_init(void);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_DRIVER_H */
