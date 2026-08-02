/* =============================================================================
 * i2s_audio_driver.h — I²S DMA Hardware Audio Driver Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef I2S_AUDIO_DRIVER_H
#define I2S_AUDIO_DRIVER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize I²S peripheral and allocate double-buffered DMA */
vhl_err_t vhl_hal_i2s_init(void);

/** Transmit stereo frame buffer to I²S DMA queue */
vhl_err_t vhl_hal_i2s_write(const vhl_pcm16_t* buffer, size_t frameCount);

#ifdef __cplusplus
}
#endif

#endif /* I2S_AUDIO_DRIVER_H */
