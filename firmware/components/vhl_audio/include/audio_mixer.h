/* =============================================================================
 * audio_mixer.h — Floating-Point Master Accumulator & Limiter Interface Stub
 * Project: Digital Harmonium Workstation Firmware
 * Stage 1 — System Skeleton
 * ============================================================================= */

#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include "vhl_types.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

vhl_err_t vhl_audio_mixer_init(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MIXER_H */
