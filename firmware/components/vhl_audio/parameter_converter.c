/* =============================================================================
 * parameter_converter.c — Polyphone Parameter Conversion Layer Implementation
 * Project: Digital Harmonium Workstation Firmware
 * Stage 9 — Synthesis Runtime Infrastructure
 * ============================================================================= */

#include "parameter_converter.h"
#include <math.h>
#include <string.h>

float vhl_cb_to_gain(uint16_t centibels)
{
    if (centibels >= 1440) {
        return 0.0f; // Silence threshold
    }
    float db = (float)centibels / 10.0f;
    return powf(10.0f, -db / 20.0f);
}

void vhl_pan_to_gains(int16_t pan, float* pOutLeft, float* pOutRight)
{
    if (!pOutLeft || !pOutRight) return;

    if (pan < -500) pan = -500;
    if (pan > 500)  pan = 500;

    // Convert -500..+500 to angle 0..pi/2
    float angle = ((float)(pan + 500) / 1000.0f) * (3.14159265f / 2.0f);
    *pOutLeft  = cosf(angle);
    *pOutRight = sinf(angle);
}

float vhl_mhz_to_hz(uint16_t mHz)
{
    return (float)mHz / 1000.0f;
}

float vhl_cents10_to_cents(int16_t cents10)
{
    return (float)cents10 / 10.0f;
}

vhl_err_t vhl_decode_region_to_runtime(const VHLRegion* pRegion, const VHLSampleEntry* pSample, VHLRuntimeVoiceConfig* pOutConfig)
{
    if (!pRegion || !pOutConfig) {
        return VHL_ERR_INVALID_ARG;
    }

    memset(pOutConfig, 0, sizeof(VHLRuntimeVoiceConfig));

    pOutConfig->exclusiveClass = pRegion->exclusive_class;
    pOutConfig->loopMode       = pRegion->loop_mode;
    pOutConfig->coarseTune     = pRegion->coarse_tune;
    pOutConfig->fineTuneCents  = vhl_cents10_to_cents(pRegion->fine_tune);
    pOutConfig->scaleTuning   = pRegion->scale_tuning;
    pOutConfig->attenuationGain= vhl_cb_to_gain(pRegion->attenuation_cb);
    vhl_pan_to_gains(pRegion->pan, &pOutConfig->panLeft, &pOutConfig->panRight);
    pOutConfig->reverbSend     = pRegion->reverb_send;
    pOutConfig->chorusSend     = pRegion->chorus_send;

    // Volume Envelope
    pOutConfig->volEnvConfig.delayMs     = pRegion->vol_env.delay_ms;
    pOutConfig->volEnvConfig.attackMs    = pRegion->vol_env.attack_ms;
    pOutConfig->volEnvConfig.holdMs      = pRegion->vol_env.hold_ms;
    pOutConfig->volEnvConfig.decayMs     = pRegion->vol_env.decay_ms;
    pOutConfig->volEnvConfig.sustainGain = vhl_cb_to_gain(pRegion->vol_env.sustain_level);
    pOutConfig->volEnvConfig.releaseMs   = pRegion->vol_env.release_ms;
    pOutConfig->volEnvConfig.keyToHold   = pRegion->vol_env.key_to_hold;
    pOutConfig->volEnvConfig.keyToDecay  = pRegion->vol_env.key_to_decay;

    // Mod Envelope
    pOutConfig->modEnvConfig.delayMs          = pRegion->mod_env.delay_ms;
    pOutConfig->modEnvConfig.attackMs         = pRegion->mod_env.attack_ms;
    pOutConfig->modEnvConfig.holdMs           = pRegion->mod_env.hold_ms;
    pOutConfig->modEnvConfig.decayMs          = pRegion->mod_env.decay_ms;
    pOutConfig->modEnvConfig.sustainGain      = (float)pRegion->mod_env.sustain_level / 1000.0f; // Permille -> Gain
    pOutConfig->modEnvConfig.releaseMs        = pRegion->mod_env.release_ms;
    pOutConfig->modEnvConfig.keyToHold        = pRegion->mod_env.key_to_hold;
    pOutConfig->modEnvConfig.keyToDecay       = pRegion->mod_env.key_to_decay;
    pOutConfig->modEnvConfig.depthPitchCents  = vhl_cents10_to_cents(pRegion->mod_env_to_pitch);
    pOutConfig->modEnvConfig.depthFilterCents = vhl_cents10_to_cents(pRegion->mod_env_to_filter);

    // Mod LFO
    pOutConfig->modLfoConfig.delayMs          = pRegion->lfo_mod.delay_ms;
    pOutConfig->modLfoConfig.freqHz           = vhl_mhz_to_hz(pRegion->lfo_mod.freq_mHz);
    pOutConfig->modLfoConfig.depthPitchCents  = vhl_cents10_to_cents(pRegion->lfo_mod.to_pitch_cb);
    pOutConfig->modLfoConfig.depthFilterCents = vhl_cents10_to_cents(pRegion->lfo_mod.to_filter_cb);
    pOutConfig->modLfoConfig.depthVolumeDb    = (float)pRegion->lfo_mod.to_volume_cb / 10.0f;

    // Vib LFO
    pOutConfig->vibLfoConfig.delayMs          = pRegion->lfo_vib.delay_ms;
    pOutConfig->vibLfoConfig.freqHz           = vhl_mhz_to_hz(pRegion->lfo_vib.freq_mHz);
    pOutConfig->vibLfoConfig.depthPitchCents  = vhl_cents10_to_cents(pRegion->lfo_vib.to_pitch_cb);

    // Biquad Filter Configuration
    pOutConfig->filterConfig.cutoffHz   = (float)pRegion->filter.cutoff_hz;
    pOutConfig->filterConfig.resonanceDb= (float)pRegion->filter.resonance_cb / 10.0f;
    pOutConfig->filterConfig.active     = (pRegion->flags & VHL_REGION_FLAG_FILTER_ACTIVE) != 0;

    // Sample References
    if (pSample) {
        pOutConfig->sampleId     = pRegion->sample_id;
        pOutConfig->pSampleEntry = pSample;
    }

    return VHL_OK;
}
