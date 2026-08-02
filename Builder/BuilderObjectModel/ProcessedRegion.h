// =============================================================================
// ProcessedRegion.h — Enriched BOM: VHL-Ready Region
//
// Produced by the Parameter Conversion stage of Asset Processing.
// Every field in this struct maps 1:1 to a field in VHLRegion or VHLSampleEntry
// (from vhl_structs.h). All values are in the exact units the VHL binary format
// expects — no further unit conversion is needed by the Backend.
//
// UNIT MAPPING (from parameter_mapping.csv):
//   SFZ seconds    → VHL ms (uint16_t, capped at 65535)
//   SFZ percent    → VHL centibels (vol env sustain) or permille (mod env sustain)
//   SFZ Hz         → VHL Hz (uint16_t, filter cutoff)
//   SFZ Hz (LFO)   → VHL millihertz (uint16_t)
//   SFZ cents      → VHL cents×10 (int16_t)
//   SFZ dB         → VHL centibels (uint16_t)
//   SFZ pan ±100   → VHL pan ±500 (int16_t)
//   SFZ dB (atten) → VHL centibels (converted via the DB_SF2_TO_REAL_DB factor)
// =============================================================================

#pragma once

#include <cstdint>
#include <string>

namespace Vhl {
namespace Processed {

// =============================================================================
// ProcessedEnvelope — all times in ms, sustain in VHL native unit
// =============================================================================

struct ProcessedEnvelope
{
    uint16_t delay_ms    = 0;    ///< VHLEnvelope.delay_ms
    uint16_t attack_ms   = 1;    ///< VHLEnvelope.attack_ms    (default: 1ms)
    uint16_t hold_ms     = 0;    ///< VHLEnvelope.hold_ms
    uint16_t decay_ms    = 0;    ///< VHLEnvelope.decay_ms
    uint16_t sustain_lvl = 0;    ///< VHLEnvelope.sustain_level (centibels for vol / permille for mod)
    uint16_t release_ms  = 20;   ///< VHLEnvelope.release_ms   (default: 20ms)
    int8_t   key_to_hold = 0;    ///< VHLEnvelope.key_to_hold  (×0.1 per semitone from key 60)
    int8_t   key_to_decay= 0;    ///< VHLEnvelope.key_to_decay
};

// =============================================================================
// ProcessedFilter — cutoff in Hz, resonance in centibels
// =============================================================================

struct ProcessedFilter
{
    uint16_t cutoff_hz     = 20000; ///< VHLFilter.cutoff_hz (Hz, 1–20000; 20000 = open)
    uint16_t resonance_cb  = 0;     ///< VHLFilter.resonance_cb (0 = Butterworth Q)
    bool     active        = false; ///< mirrors VHL_REGION_FLAG_FILTER_ACTIVE
};

// =============================================================================
// ProcessedModLFO — all VHL units
// =============================================================================

struct ProcessedModLFO
{
    uint16_t delay_ms      = 0;    ///< VHLModLFO.delay_ms
    uint16_t freq_mHz      = 0;    ///< VHLModLFO.freq_mHz (millihertz)
    int16_t  to_pitch_cb   = 0;    ///< VHLModLFO.to_pitch_cb   (cents×10)
    int16_t  to_filter_cb  = 0;    ///< VHLModLFO.to_filter_cb  (cents×10)
    int16_t  to_volume_cb  = 0;    ///< VHLModLFO.to_volume_cb  (centibels×10)
    bool     active        = false;
};

// =============================================================================
// ProcessedVibLFO — all VHL units
// =============================================================================

struct ProcessedVibLFO
{
    uint16_t delay_ms    = 0;    ///< VHLVibLFO.delay_ms
    uint16_t freq_mHz    = 0;    ///< VHLVibLFO.freq_mHz
    int16_t  to_pitch_cb = 0;    ///< VHLVibLFO.to_pitch_cb (cents×10)
    bool     active      = false;
};

// =============================================================================
// ProcessedRegion — one VHLRegion worth of data, all values pre-converted
// =============================================================================

struct ProcessedRegion
{
    // -------------------------------------------------------------------------
    // Source tracking
    // -------------------------------------------------------------------------

    int      sourceRegionIndex = 0; ///< Index into BomInstrument.regions
    int      sourceLine        = 0; ///< Line number in SFZ file

    // =========================================================================
    // VHLRegion fields — exact types and units
    // =========================================================================

    // Key and velocity range
    uint8_t  key_lo = 0;         ///< VHLRegion.key_lo
    uint8_t  key_hi = 127;       ///< VHLRegion.key_hi
    uint8_t  vel_lo = 0;         ///< VHLRegion.vel_lo
    uint8_t  vel_hi = 127;       ///< VHLRegion.vel_hi

    // Sample reference
    uint16_t sample_id = 0;      ///< VHLRegion.sample_id → index into ProcessedSample[]

    // Voice behavior
    uint8_t  exclusive_class = 0;///< VHLRegion.exclusive_class (0 = no choke)
    uint8_t  loop_mode       = 0;///< VHLRegion.loop_mode (VHL_LOOP_NONE/CONTINUOUS/SUSTAIN)
    uint8_t  flags           = 0;///< VHLRegion.flags (VHL_REGION_FLAG_* bitmask)

    // Tuning
    int8_t   coarse_tune = 0;    ///< VHLRegion.coarse_tune (semitones, −96..+96)
    int16_t  fine_tune   = 0;    ///< VHLRegion.fine_tune   (cents×10, −1000..+1000)
    uint8_t  scale_tuning = 100; ///< VHLRegion.scale_tuning (0=fixed, 100=equal temp.)

    // Volume and spatial
    uint16_t attenuation_cb = 0; ///< VHLRegion.attenuation_cb (centibels, 0-1440)
    int16_t  pan            = 0; ///< VHLRegion.pan (−500 to +500)
    uint8_t  reverb_send    = 0; ///< VHLRegion.reverb_send (0-100)
    uint8_t  chorus_send    = 0; ///< VHLRegion.chorus_send (0-100)

    // Envelopes
    ProcessedEnvelope vol_env;
    ProcessedEnvelope mod_env;

    // Mod envelope routing
    int16_t  mod_env_to_pitch  = 0; ///< VHLRegion.mod_env_to_pitch  (cents×10)
    int16_t  mod_env_to_filter = 0; ///< VHLRegion.mod_env_to_filter (cents×10)

    // LFOs
    ProcessedModLFO lfo_mod;
    ProcessedVibLFO lfo_vib;

    // Filter
    ProcessedFilter filter;

    // =========================================================================
    // Helpers
    // =========================================================================

    bool hasFilter()  const { return filter.active; }
    bool hasModLFO()  const { return lfo_mod.active; }
    bool hasVibLFO()  const { return lfo_vib.active; }
    bool hasModEnv()  const { return (flags >> 2) & 1; }
};

} // namespace Processed
} // namespace Vhl
