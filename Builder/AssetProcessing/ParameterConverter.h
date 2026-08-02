// =============================================================================
// ParameterConverter.h — VHL Builder Asset Processing: Unit Conversion Engine
//
// Converts every SFZ opcode value (from BomRegion) into the exact VHL binary
// format unit expected by vhl_structs.h. All conversion formulas come directly
// from parameter_mapping.csv.
//
// This is a pure-function utility class. It has no state.
//
// CONVERSION REFERENCE (from parameter_mapping.csv):
//
//   Envelope times:
//     SFZ seconds → VHL ms = round(seconds × 1000), capped at 65535
//
//   Vol env sustain:
//     SFZ ampeg_sustain (0-100 %) → VHL centibels
//     centibels = round(-20 × log10(sustain_pct / 100) × 10)
//     Range: 0 = full level (0 dB atten), 1440 = silence (144 dB atten)
//
//   Mod env sustain:
//     SFZ pitcheg/fileg_sustain (0-100 %) → VHL permille
//     permille = round(sustain_pct × 10)
//     (Note: Polyphone writes 100-x for mod sustain; we receive the already
//      written value, so we convert percent to permille directly)
//
//   LFO frequency:
//     SFZ Hz → VHL mHz = round(Hz × 1000), capped at 65535
//
//   LFO delay:
//     SFZ seconds → VHL ms (same as envelope times)
//
//   LFO depths to pitch/filter:
//     SFZ cents → VHL cents×10 = round(cents × 10), capped at ±32767
//
//   LFO depth to volume:
//     SFZ dB → VHL centibels×10 = round(dB × 100), capped at ±32767
//
//   Filter cutoff:
//     SFZ Hz → VHL uint16_t Hz, clamped to 1-20000
//
//   Filter resonance:
//     SFZ dB → VHL centibels = round(dB × 10)
//
//   Attenuation:
//     SFZ volume= dB (negative = attenuation) → VHL centibels
//     centibels = round(-volume_db × 10), clamped to 0-1440
//     Note: SFZ volume= can be positive (gain) or negative (attenuation)
//
//   Pan:
//     SFZ pan ±100 → VHL pan ±500: multiply by 5
//
//   Fine tune:
//     SFZ cents → VHL cents×10: multiply by 10
//
//   Key-to-envelope scaling:
//     SFZ ampeg_holdcc133/ampeg_decaycc133 (seconds/semitone) → VHL int8_t ×0.1
//     vhl_value = clamp(round(sfz_value / 0.1), -128, 127)
//     (stored as ×0.1 to fit scaling into int8_t while preserving 1 decimal place)
// =============================================================================

#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <optional>
#include "../BuilderObjectModel/BomRegion.h"
#include "../BuilderObjectModel/BomEnvelope.h"
#include "../BuilderObjectModel/BomLFO.h"
#include "../BuilderObjectModel/ProcessedRegion.h"
#include "../Frontend/ParseError.h"

namespace Vhl {
namespace AssetProcessing {

class ParameterConverter
{
public:
    // =========================================================================
    // Envelope conversion
    // =========================================================================

    /// Convert SFZ seconds to VHL ms (uint16_t, capped at 65535).
    static uint16_t secsToMs(float seconds, [[maybe_unused]] uint16_t defaultMs = 0)
    {
        if (seconds < 0.0f) return 0;
        float ms = seconds * 1000.0f;
        if (ms >= 65535.0f) return 65535;
        return static_cast<uint16_t>(std::lroundf(ms));
    }

    /// Convert SFZ ampeg_sustain (0-100%) to VHL centibels (vol env sustain).
    /// centibels = -20 × log10(pct/100) × 10
    /// Special case: 0% → 1440 cb (silence). 100% → 0 cb (full level).
    static uint16_t sustainPctToCentibels(float pct)
    {
        if (pct <= 0.0f)   return 1440; // silence
        if (pct >= 100.0f) return 0;    // full level
        double db = -20.0 * std::log10(static_cast<double>(pct) / 100.0);
        double cb = db * 10.0;
        if (cb >= 1440.0) return 1440;
        return static_cast<uint16_t>(std::lround(cb));
    }

    /// Convert SFZ pitcheg/fileg_sustain (0-100%) to VHL permille (mod env sustain).
    static uint16_t sustainPctToPermille(float pct)
    {
        if (pct <= 0.0f)   return 0;
        if (pct >= 100.0f) return 1000;
        return static_cast<uint16_t>(std::lroundf(pct * 10.0f));
    }

    /// Build a ProcessedEnvelope from a BomEnvelope.
    /// isVolEnv controls whether sustain uses centibels (vol) or permille (mod).
    static Processed::ProcessedEnvelope convertEnvelope(const Bom::BomEnvelope& e,
                                                          bool isVolEnv)
    {
        Processed::ProcessedEnvelope r;
        r.delay_ms    = e.delay_s     ? secsToMs(*e.delay_s)   : 0;
        r.attack_ms   = e.attack_s    ? secsToMs(*e.attack_s,1): 1;
        r.hold_ms     = e.hold_s      ? secsToMs(*e.hold_s)     : 0;
        r.decay_ms    = e.decay_s     ? secsToMs(*e.decay_s)    : 0;
        r.release_ms  = e.release_s   ? secsToMs(*e.release_s, 20) : 20;

        if (e.sustain_pct)
            r.sustain_lvl = isVolEnv
                          ? sustainPctToCentibels(*e.sustain_pct)
                          : sustainPctToPermille(*e.sustain_pct);
        else
            r.sustain_lvl = isVolEnv ? 0 : 1000; // default: full level

        // Key-to-time scaling (stored as int8_t × 0.1)
        r.key_to_hold  = e.key_to_hold_s  ? clampInt8Scale(*e.key_to_hold_s)  : 0;
        r.key_to_decay = e.key_to_decay_s ? clampInt8Scale(*e.key_to_decay_s) : 0;

        return r;
    }

    // =========================================================================
    // LFO conversion
    // =========================================================================

    /// Convert SFZ Hz to VHL millihertz (uint16_t, capped at 65535).
    static uint16_t hzToMilliHz(float hz)
    {
        if (hz <= 0.0f) return 0;
        float mHz = hz * 1000.0f;
        if (mHz >= 65535.0f) return 65535;
        return static_cast<uint16_t>(std::lroundf(mHz));
    }

    /// Convert SFZ cents depth to VHL cents×10 (int16_t).
    static int16_t centsToTenths(int cents)
    {
        long v = static_cast<long>(cents) * 10;
        return static_cast<int16_t>(std::clamp(v, -32768L, 32767L));
    }

    /// Convert SFZ dB LFO volume depth to VHL centibels×10 (int16_t).
    /// centibels×10 = dB × 100
    static int16_t dbToCentibelsX10(float db)
    {
        long v = std::lroundf(db * 100.0f);
        return static_cast<int16_t>(std::clamp(v, -32768L, 32767L));
    }

    static Processed::ProcessedModLFO convertModLFO(const Bom::BomModLFO& lfo)
    {
        Processed::ProcessedModLFO r;
        r.delay_ms     = lfo.delay_s   ? secsToMs(*lfo.delay_s) : 0;
        r.freq_mHz     = lfo.freq_hz   ? hzToMilliHz(*lfo.freq_hz) : 0;
        r.to_pitch_cb  = 0; // modLFO→pitch is promoted to vibLFO by Polyphone
        r.to_filter_cb = lfo.depth_filter_cents
                        ? centsToTenths(static_cast<int>(*lfo.depth_filter_cents)) : 0;
        r.to_volume_cb = lfo.depth_volume_db
                        ? dbToCentibelsX10(*lfo.depth_volume_db) : 0;
        r.active = lfo.isActive();
        return r;
    }

    static Processed::ProcessedVibLFO convertVibLFO(const Bom::BomVibLFO& lfo)
    {
        Processed::ProcessedVibLFO r;
        r.delay_ms    = lfo.delay_s    ? secsToMs(*lfo.delay_s)  : 0;
        r.freq_mHz    = lfo.freq_hz    ? hzToMilliHz(*lfo.freq_hz) : 0;
        r.to_pitch_cb = lfo.depth_cents ? centsToTenths(*lfo.depth_cents) : 0;
        r.active = lfo.isActive();
        return r;
    }

    // =========================================================================
    // Filter conversion
    // =========================================================================

    static Processed::ProcessedFilter convertFilter(const Bom::BomFilter& f)
    {
        Processed::ProcessedFilter r;
        if (!f.isActive()) { r.active = false; return r; }

        r.active     = true;
        float hz     = f.cutoff_hz ? *f.cutoff_hz : 20000.0f;
        r.cutoff_hz  = static_cast<uint16_t>(std::clamp(
                           static_cast<int>(std::lroundf(hz)), 1, 20000));
        // resonance: SFZ dB → VHL centibels = dB × 10
        float rdb    = f.resonance_db ? *f.resonance_db : 0.0f;
        long  rcb    = std::lroundf(rdb * 10.0f);
        r.resonance_cb = static_cast<uint16_t>(std::clamp(rcb, 0L, 960L)); // 0-96dB
        return r;
    }

    // =========================================================================
    // Volume and spatial
    // =========================================================================

    /// SFZ volume= (dB, negative=attenuation) → VHL attenuation_cb (centibels, 0-1440).
    /// VHL stores attenuation (positive), so we negate the (negative) SFZ value.
    static uint16_t volumeDbToAttenuationCb(float volumeDb)
    {
        float attenuationDb = -volumeDb; // SFZ volume= -6dB → VHL 6dB attenuation
        if (attenuationDb <= 0.0f) return 0;   // gain → treated as 0 attenuation
        long cb = std::lroundf(attenuationDb * 10.0f);
        return static_cast<uint16_t>(std::clamp(cb, 0L, 1440L));
    }

    /// SFZ pan (±100) → VHL pan (±500). Multiply by 5.
    static int16_t sfzPanToVhlPan(float sfzPan)
    {
        long v = std::lroundf(sfzPan * 5.0f);
        return static_cast<int16_t>(std::clamp(v, -500L, 500L));
    }

    /// SFZ tune= (cents) → VHL fine_tune (cents×10, int16_t).
    static int16_t fineTuneCentsToVhl(int sfzCents)
    {
        long v = static_cast<long>(sfzCents) * 10;
        return static_cast<int16_t>(std::clamp(v, -1000L, 1000L));
    }

    // =========================================================================
    // Mod envelope routing
    // =========================================================================

    /// SFZ pitcheg_depth / fileg_depth (cents) → VHL cents×10
    static int16_t modEnvDepthToVhl(int sfzCents)
    {
        return centsToTenths(sfzCents);
    }

    // =========================================================================
    // Full BomRegion → ProcessedRegion conversion
    // Requires the sample_id to already be resolved by the AssetProcessor.
    // =========================================================================

    static Processed::ProcessedRegion convert(const Bom::BomRegion& r,
                                               uint16_t sampleId,
                                               [[maybe_unused]] Frontend::DiagnosticCollector& diag)
    {
        Processed::ProcessedRegion p;
        p.sourceRegionIndex = r.regionIndex;
        p.sourceLine        = r.sourceLine;

        // Key/vel range
        p.key_lo = r.keyRange.lo;
        p.key_hi = r.keyRange.hi;
        p.vel_lo = r.velRange.lo;
        p.vel_hi = r.velRange.hi;

        // Sample reference
        p.sample_id = sampleId;

        // Exclusive class
        p.exclusive_class = r.exclusive_group.has_value()
                          ? static_cast<uint8_t>(*r.exclusive_group & 0xFF)
                          : 0;

        // Loop mode
        switch (r.loop_mode)
        {
            case Bom::LoopMode::LoopContinuous: p.loop_mode = 1; break;
            case Bom::LoopMode::LoopSustain:    p.loop_mode = 3; break;
            case Bom::LoopMode::OneShot:        p.loop_mode = 0; break; // runtime: play once
            default:                            p.loop_mode = 0; break;
        }

        // Flags
        p.flags = 0;
        p.flags |= 0x02; // VHL_REGION_FLAG_VEL_ATTEN — always set (SF2 default)
        if (r.hasModEnv() || r.mod_env_to_pitch_cents || r.mod_env_to_filter_cents)
            p.flags |= 0x04; // VHL_REGION_FLAG_MOD_ENV_ACTIVE
        if (r.hasFilter())
            p.flags |= 0x08; // VHL_REGION_FLAG_FILTER_ACTIVE
        if (r.hasModLFO())
            p.flags |= 0x10; // VHL_REGION_FLAG_MOD_LFO_ACTIVE
        if (r.hasVibLFO())
            p.flags |= 0x20; // VHL_REGION_FLAG_VIB_LFO_ACTIVE

        // Tuning
        p.coarse_tune  = r.transpose_semitones.has_value()
                        ? static_cast<int8_t>(
                              std::clamp(*r.transpose_semitones, -96, 96)) : 0;
        p.fine_tune    = r.tune_cents.has_value()
                        ? fineTuneCentsToVhl(*r.tune_cents) : 0;
        p.scale_tuning = r.pitch_keytrack.has_value()
                        ? static_cast<uint8_t>(
                              std::clamp(*r.pitch_keytrack, 0, 100)) : 100;

        // Volume: SFZ volume= → VHL attenuation_cb
        p.attenuation_cb = r.volume_db.has_value()
                         ? volumeDbToAttenuationCb(*r.volume_db) : 0;

        // Pan: SFZ ±100 → VHL ±500
        p.pan = r.pan.has_value() ? sfzPanToVhlPan(*r.pan) : 0;

        // Effects sends (already 0-100)
        p.reverb_send = r.reverb_send.has_value()
                       ? static_cast<uint8_t>(std::clamp(
                             static_cast<int>(*r.reverb_send), 0, 100)) : 0;
        p.chorus_send = r.chorus_send.has_value()
                       ? static_cast<uint8_t>(std::clamp(
                             static_cast<int>(*r.chorus_send), 0, 100)) : 0;

        // Volume envelope
        p.vol_env = convertEnvelope(r.vol_env, true);

        // Mod envelope
        p.mod_env = convertEnvelope(r.mod_env, false);
        p.mod_env_to_pitch  = r.mod_env_to_pitch_cents.has_value()
                             ? modEnvDepthToVhl(*r.mod_env_to_pitch_cents) : 0;
        p.mod_env_to_filter = r.mod_env_to_filter_cents.has_value()
                             ? modEnvDepthToVhl(*r.mod_env_to_filter_cents) : 0;

        // LFOs
        p.lfo_mod = convertModLFO(r.mod_lfo);
        p.lfo_vib = convertVibLFO(r.vib_lfo);

        // Filter
        p.filter = convertFilter(r.filter);

        return p;
    }

private:
    /// Clamp a float value to int8_t when storing as ×0.1 scaling factor.
    /// Used for keynumToVolEnvHold / keynumToVolEnvDecay.
    static int8_t clampInt8Scale(float value)
    {
        // Value is in seconds/semitone; stored as ×0.1 → multiply by 10
        // Actually from parameter_mapping.csv: stored as (original/10) for int8_t precision
        long v = std::lroundf(value * 10.0f);
        return static_cast<int8_t>(std::clamp(v, -128L, 127L));
    }
};

} // namespace AssetProcessing
} // namespace Vhl
