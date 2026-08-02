// =============================================================================
// BomRegion.h — VHL Builder Object Model: Complete Region
//
// A BomRegion is the canonical unit of data produced by the Frontend.
// It contains every parameter from one SFZ <region> block, after group
// opcodes have been merged in (group opcodes fill gaps; region opcodes
// take priority — identical to the SFZ specification).
//
// All values are in SFZ units (seconds, Hz, dB, cents, percent, MIDI integers).
// No further parsing is needed by any later stage. The Backend reads these
// values, applies unit conversions, and serializes them to VHL binary.
//
// Design rule: every field uses std::optional<T>.
// Unset = the SFZ/SF2 default applies at the Backend stage.
// Set   = an explicit value was present in the SFZ.
// =============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include "BomTypes.h"
#include "BomEnvelope.h"
#include "BomFilter.h"
#include "BomLFO.h"

namespace Vhl {
namespace Bom {

/// A sample file reference, resolved to an absolute path during parsing.
struct BomSampleRef
{
    std::string sfzRelativePath; ///< Path as written in the SFZ (may use backslashes)
    std::string absolutePath;    ///< Resolved absolute path at parse time
    bool        isStereo = false;///< True if the WAV file is stereo (two-channel)
};

// =============================================================================
// BomRegion — one SFZ <region>, fully resolved (group + region opcodes merged)
// =============================================================================

struct BomRegion
{
    // -------------------------------------------------------------------------
    // Source tracking (for diagnostics)
    // -------------------------------------------------------------------------
    int  sourceLine = 0;   ///< Line number of the <region> header in the SFZ file
    int  regionIndex = 0;  ///< 0-based index within the instrument

    // =========================================================================
    // SAMPLE REFERENCE
    // =========================================================================

    BomSampleRef sample; ///< sample= opcode — path to the WAV file

    // =========================================================================
    // KEY AND VELOCITY RANGES
    // =========================================================================

    KeyRange keyRange;   ///< lokey= / hikey= / key= (defaults: lo=0, hi=127)
    VelRange velRange;   ///< lovel= / hivel=       (defaults: lo=0, hi=127)

    // =========================================================================
    // PITCH / TUNING
    // =========================================================================

    /// pitch_keycenter= — the MIDI note at which this sample plays at its
    /// natural pitch. Derived from SF2 overridingRootKey or byOriginalPitch.
    /// Default: 60 (C4, middle C).
    std::optional<uint8_t> pitch_keycenter;

    /// tune= — fine tune offset in cents. Range: −100 to +100.
    /// Derived from SF2 fineTune + sample chPitchCorrection (summed by Polyphone).
    std::optional<int> tune_cents;

    /// transpose= — coarse tune in semitones. Range: −127 to +127.
    /// Derived from SF2 coarseTune.
    std::optional<int> transpose_semitones;

    /// pitch_keytrack= — cents per MIDI key. Range: 0–100.
    /// 100 = standard equal temperament. 0 = fixed pitch (percussion).
    /// Derived from SF2 scaleTuning.
    std::optional<int> pitch_keytrack;

    // =========================================================================
    // VOLUME AND PAN
    // =========================================================================

    /// volume= — gain in dB. Includes: SF2 initialAttenuation (negative) plus
    /// any gain compensation applied by Polyphone's BalanceParameters.
    /// Typically negative (attenuation).
    std::optional<float> volume_db;

    /// pan= — stereo pan. SFZ range: −100 (full left) to +100 (full right).
    /// Derived from SF2 pan (SF2 range ±500 → SFZ ×2 → ±1000... actually ±100).
    /// Polyphone exports pan × 2 where pan is in SF2 units /10.
    std::optional<float> pan;

    /// width= — stereo width for stereo samples. SFZ range: −100 to +100.
    /// Only present for stereo sample regions.
    std::optional<float> width;

    /// position= — stereo position for stereo samples. SFZ range: −100 to +100.
    std::optional<float> position;

    // =========================================================================
    // LOOP AND SAMPLE PLAYBACK
    // =========================================================================

    LoopMode  loop_mode = LoopMode::NoLoop; ///< loop_mode=
    TriggerMode trigger = TriggerMode::Attack; ///< trigger=

    /// loop_start= — loop start frame (absolute frame index in the WAV file).
    std::optional<uint32_t> loop_start_frame;

    /// loop_end= — loop end frame (inclusive, absolute frame index).
    /// Note: Polyphone writes endloopAddrsOffset−1 to convert SF2 exclusive to SFZ inclusive.
    std::optional<uint32_t> loop_end_frame;

    /// offset= — sample start offset in frames (skip this many frames from beginning).
    std::optional<uint32_t> start_offset_frames;

    /// end= — sample end frame (inclusive). Used when a sub-region of the sample is played.
    std::optional<uint32_t> end_frame;

    // =========================================================================
    // VOLUME ENVELOPE (ampeg_*)
    // =========================================================================

    BomEnvelope vol_env; ///< ampeg_delay/attack/hold/decay/sustain/release + cc133 scaling

    // =========================================================================
    // MODULATION ENVELOPE (pitcheg_* and fileg_* share one SF2 envelope)
    // Both pitch and filter modulations use the same DAHDSR timing from SF2.
    // Polyphone exports this as pitcheg_* AND fileg_* simultaneously.
    // =========================================================================

    BomEnvelope mod_env; ///< pitcheg_*/fileg_* — shared DAHDSR timing

    /// pitcheg_depth= — how many cents the mod envelope modulates pitch.
    /// Derived from SF2 modEnvToPitch.
    std::optional<int> mod_env_to_pitch_cents;

    /// fileg_depth= — how many cents the mod envelope modulates filter cutoff.
    /// Derived from SF2 modEnvToFilterFc.
    std::optional<int> mod_env_to_filter_cents;

    // =========================================================================
    // FILTER (fil_*)
    // =========================================================================

    BomFilter filter; ///< fil_type, cutoff, resonance, fil_veltrack, fil_keytrack

    // =========================================================================
    // MODULATION LFO (amplfo_* / fillfo_*)
    // =========================================================================

    BomModLFO mod_lfo; ///< amplfo_delay/freq/depth + fillfo_delay/freq/depth

    // =========================================================================
    // VIBRATO LFO (pitchlfo_*)
    // =========================================================================

    BomVibLFO vib_lfo; ///< pitchlfo_delay/freq/depth

    // =========================================================================
    // EXCLUSIVE CLASS / CHOKE GROUP
    // =========================================================================

    /// group= — exclusive class group ID. Regions in the same group choke each other.
    /// Derived from SF2 exclusiveClass.
    std::optional<int> exclusive_group;

    /// off_by= — which group this region is turned off by.
    /// Polyphone always writes group == off_by (same value).
    std::optional<int> off_by;

    // =========================================================================
    // EFFECTS SENDS
    // =========================================================================

    /// effect1= — reverb send level 0–100.
    /// Derived from SF2 reverbEffectsSend.
    std::optional<float> reverb_send;

    /// effect2= — chorus send level 0–100.
    /// Derived from SF2 chorusEffectsSend.
    std::optional<float> chorus_send;

    // =========================================================================
    // VELOCITY TRACKING
    // =========================================================================

    /// amp_veltrack= — how much velocity affects volume. −100 to +100 (percent).
    /// Derived from SF2 default modulator: velocity→attenuation.
    std::optional<float> amp_veltrack;

    /// amp_velcurve_1= and amp_velcurve_127= — fixed velocity level override.
    /// Polyphone writes these for fixed-velocity regions (percussion with champ_velocity).
    std::optional<float> amp_velcurve_1;
    std::optional<float> amp_velcurve_127;

    // =========================================================================
    // CHANNEL FILTER (percussion kits)
    // =========================================================================

    /// lochan= / hichan= — MIDI channel filter (1–16).
    /// Polyphone writes lochan=10 hichan=10 for percussion kits (GM channel 10).
    std::optional<int> lo_chan;
    std::optional<int> hi_chan;

    // =========================================================================
    // Convenience queries
    // =========================================================================

    bool hasFilter()  const { return filter.isActive(); }
    bool hasModLFO()  const { return mod_lfo.isActive(); }
    bool hasVibLFO()  const { return vib_lfo.isActive(); }
    bool hasModEnv()  const { return mod_env.hasAnyValue(); }
    bool hasVolEnv()  const { return vol_env.hasAnyValue(); }
    bool hasExclusiveClass() const { return exclusive_group.has_value(); }
    bool isPercussion() const
    {
        return lo_chan.has_value() && hi_chan.has_value()
            && *lo_chan == 10 && *hi_chan == 10;
    }
};

} // namespace Bom
} // namespace Vhl
