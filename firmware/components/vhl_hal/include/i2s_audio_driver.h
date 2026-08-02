/* =============================================================================
 * i2s_audio_driver.h — I²S DMA Hardware Audio Driver
 * Project: Digital Harmonium Workstation Firmware
 * Stage 6 — Audio HAL Architecture
 * ============================================================================= */

#ifndef I2S_AUDIO_DRIVER_H
#define I2S_AUDIO_DRIVER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Generic Audio Render Callback Signature (HAL requests audio from Synthesis Engine) */
typedef void (*vhl_i2s_render_cb_t)(vhl_audio_buffer_t* pBuf, size_t frameCount);

/** Real-time DMA Timing & Performance Instrumentation Struct */
typedef struct {
    uint32_t dmaBlocksTransmitted;
    uint32_t underrunCount;
    uint32_t renderTimeUs;         /* Render execution time for last DMA block */
    uint32_t maxRenderTimeUs;      /* Maximum peak render time observed */
    uint32_t bufferSwapTimeUs;     /* Microseconds taken for DMA buffer swap */
    uint32_t deadlineMisses;       /* Count of blocks exceeding 11.61 ms budget */
    float    cpuLoadPercent;       /* Core 1 CPU load % (renderTimeUs / 11610us) */
} vhl_i2s_stats_t;

/* -----------------------------------------------------------------------------
 * Public Audio HAL APIs
 * ----------------------------------------------------------------------------- */

/** Initialize I²S peripheral, configure dual DMA buffers, and prepare driver */
vhl_err_t vhl_hal_i2s_init(void);

/** Register external audio engine synthesis callback */
vhl_err_t vhl_hal_i2s_register_render_callback(vhl_i2s_render_cb_t cb);

/** Start I²S DMA transmission loop */
vhl_err_t vhl_hal_i2s_start(void);

/** Stop I²S DMA transmission loop */
vhl_err_t vhl_hal_i2s_stop(void);

/** Execute single DMA buffer processing pass (Invoked by Core 1 Audio Task) */
vhl_err_t vhl_hal_i2s_process_block(uint32_t timestampUs);

/** Read real-time DMA performance & timing stats */
vhl_err_t vhl_hal_i2s_get_stats(vhl_i2s_stats_t* outStats);

#ifdef __cplusplus
}
#endif

#endif /* I2S_AUDIO_DRIVER_H */
