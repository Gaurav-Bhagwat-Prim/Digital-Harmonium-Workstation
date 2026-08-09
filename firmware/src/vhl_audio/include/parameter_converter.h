/* =============================================================================
 * parameter_converter.h — Polyphone Parameter Conversion Layer
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#ifndef PARAMETER_CONVERTER_H
#define PARAMETER_CONVERTER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"
#include "vhl_structs.h"
#include "vhl_dsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Centibels to linear gain conversion */
float vhl_cb_to_gain(uint16_t centibels);

/* Pan (-500 to +500) to constant-power stereo gains */
void vhl_pan_to_gains(int16_t pan, float* pOutLeft, float* pOutRight);

/* Millihertz to Hertz conversion */
float vhl_mhz_to_hz(uint16_t mHz);

/* Cents x10 to floating point cents conversion */
float vhl_cents10_to_cents(int16_t cents10);

/* Decode VHLRegion and VHLSampleEntry into complete VHLRuntimeVoiceConfig structure */
vhl_err_t vhl_decode_region_to_runtime(const VHLRegion* pRegion, const VHLSampleEntry* pSample, VHLRuntimeVoiceConfig* pOutConfig);

#ifdef __cplusplus
}
#endif

#endif /* PARAMETER_CONVERTER_H */
