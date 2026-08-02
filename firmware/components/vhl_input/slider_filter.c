/* =============================================================================
 * slider_filter.c — ADC Exponential Moving Average & Hysteresis Filter Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 5 — Input Layer Architecture
 * ============================================================================= */

#include "slider_filter.h"
#include <math.h>
#include <string.h>

static vhl_slider_state_t s_sliders[VHL_SLIDER_MAX_CHANNELS];

vhl_err_t vhl_slider_filter_init(void)
{
    memset(s_sliders, 0, sizeof(s_sliders));
    for (int i = 0; i < VHL_SLIDER_MAX_CHANNELS; ++i) {
        s_sliders[i].channel = (vhl_slider_chan_t)i;
    }
    return VHL_OK;
}

uint8_t vhl_slider_update(vhl_slider_chan_t channel, uint16_t rawAdcReading, uint32_t timestampMs)
{
    if (channel >= VHL_SLIDER_MAX_CHANNELS) {
        return 0;
    }

    vhl_slider_state_t* pSld = &s_sliders[channel];
    pSld->rawAdcValue = rawAdcReading;

    // First reading initialization pass
    if (pSld->lastOutputAdc == 0 && pSld->emaFilteredValue == 0.0f && rawAdcReading > 0) {
        pSld->emaFilteredValue = (float)rawAdcReading;
        pSld->lastOutputAdc    = rawAdcReading;
        pSld->normalizedValue  = (uint8_t)((uint32_t)rawAdcReading >> 5);
        return pSld->normalizedValue;
    }

    // Apply Exponential Moving Average (EMA): Y[n] = α·X[n] + (1-α)·Y[n-1]
    float alpha = VHL_SLIDER_EMA_ALPHA;
    pSld->emaFilteredValue = (alpha * (float)rawAdcReading) + ((1.0f - alpha) * pSld->emaFilteredValue);

    uint16_t currentAdcInt = (uint16_t)(pSld->emaFilteredValue + 0.5f);

    // Apply Hysteresis Deadband Guard (VHL_SLIDER_HYSTERESIS_LSB)
    int32_t delta = (int32_t)currentAdcInt - (int32_t)pSld->lastOutputAdc;
    if (labs(delta) >= (long)VHL_SLIDER_HYSTERESIS_LSB) {
        pSld->lastOutputAdc = currentAdcInt;

        // Scale 12-bit ADC (0..4095) to 7-bit MIDI CC (0..127)
        uint32_t norm = (uint32_t)currentAdcInt >> 5; // 4095 >> 5 = 127
        if (norm > 127) norm = 127;
        pSld->normalizedValue = (uint8_t)norm;
    }

    return pSld->normalizedValue;
}

vhl_err_t vhl_slider_get_state(vhl_slider_chan_t channel, vhl_slider_state_t* outState)
{
    if (channel >= VHL_SLIDER_MAX_CHANNELS || !outState) {
        return VHL_ERR_INVALID_ARG;
    }
    *outState = s_sliders[channel];
    return VHL_OK;
}
