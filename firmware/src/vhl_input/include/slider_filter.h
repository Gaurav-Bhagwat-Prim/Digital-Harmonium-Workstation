/* =============================================================================
 * slider_filter.h — ADC Exponential Moving Average & Hysteresis Filter
 * Project: Digital Harmonium Workstation Firmware
 * Stage 5 — Input Layer Architecture
 * ============================================================================= */

#ifndef SLIDER_FILTER_H
#define SLIDER_FILTER_H

#include "vhl_types.h"
#include "vhl_config.h"
#include "vhl_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SLIDER_CHAN_VOLUME = 0,
    SLIDER_CHAN_EXPRESSION,
    SLIDER_CHAN_TEMPO,
    VHL_SLIDER_MAX_CHANNELS
} vhl_slider_chan_t;

typedef struct {
    vhl_slider_chan_t channel;
    uint16_t          rawAdcValue;      /* 0 to 4095 (12-bit ADC) */
    float             emaFilteredValue; /* EMA smoothed value using VHL_SLIDER_EMA_ALPHA */
    uint16_t          lastOutputAdc;    /* Value guarded by VHL_SLIDER_HYSTERESIS_LSB */
    uint8_t           normalizedValue;  /* 0 to 127 (7-bit MIDI CC) */
} vhl_slider_state_t;

/* -----------------------------------------------------------------------------
 * Public API Contracts
 * ----------------------------------------------------------------------------- */

/** Initialize Analog Slider EMA & Hysteresis Filter Engine */
vhl_err_t vhl_slider_filter_init(void);

/** Process raw ADC reading through EMA + Hysteresis filter and return normalized 7-bit value */
uint8_t   vhl_slider_update(vhl_slider_chan_t channel, uint16_t rawAdcReading, uint32_t timestampMs);

/** Query current filtered slider state struct */
vhl_err_t vhl_slider_get_state(vhl_slider_chan_t channel, vhl_slider_state_t* outState);

#ifdef __cplusplus
}
#endif

#endif /* SLIDER_FILTER_H */
