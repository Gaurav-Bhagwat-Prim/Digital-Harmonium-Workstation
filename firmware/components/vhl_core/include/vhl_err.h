/* =============================================================================
 * vhl_err.h — Unified System Error Code Architecture
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 & 2 — System Core Architecture
 * ============================================================================= */

#ifndef VHL_ERR_H
#define VHL_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t vhl_err_t;

/* Common System Status Codes */
#define VHL_OK                      (0)
#define VHL_ERR_FAIL                (-1)
#define VHL_ERR_INVALID_ARG         (-2)
#define VHL_ERR_NO_MEM              (-3)
#define VHL_ERR_TIMEOUT             (-4)
#define VHL_ERR_NOT_FOUND           (-5)
#define VHL_ERR_NOT_SUPPORTED       (-6)
#define VHL_ERR_BUSY                (-7)
#define VHL_ERR_INVALID_STATE       (-8)

/* Storage & VHL Format Errors (-100 to -199) */
#define VHL_ERR_VHL_INVALID_MAGIC   (-100)
#define VHL_ERR_VHL_CRC_MISMATCH    (-101)
#define VHL_ERR_FLASH_WRITE_FAIL    (-102)
#define VHL_ERR_SD_READ_FAIL        (-103)

/* Audio & Voice Errors (-200 to -299) */
#define VHL_ERR_VOICE_UNAVAILABLE   (-200)
#define VHL_ERR_DMA_UNDERRUN        (-201)
#define VHL_ERR_SAMPLE_CORRUPT      (-202)

/* Recorder & Event Errors (-300 to -399) */
#define VHL_ERR_QUEUE_FULL          (-300)
#define VHL_ERR_RECORDER_INACTIVE   (-301)

/* Error Guard Helper Macro */
#define VHL_RETURN_ON_ERROR(expr) do { \
    vhl_err_t _err = (expr); \
    if (_err != VHL_OK) return _err; \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* VHL_ERR_H */
