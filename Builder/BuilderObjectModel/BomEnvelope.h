// =============================================================================
// BomEnvelope.h — VHL Builder Object Model: DAHDSR Envelope
//
// Represents the volume envelope and the modulation envelope as exported by
// Polyphone. All time values are in seconds (as written by Polyphone to SFZ).
// The sustain for the volume envelope is in percent (0–100).
// The sustain for the mod envelope is also in percent (Polyphone writes 100-x).
// Key-scaling is stored as the cc133 value from the SFZ v2 extension.
// =============================================================================

#pragma once

#include <optional>

namespace Vhl {
namespace Bom {

/// A complete DAHDSR envelope with optional key-number-to-time scaling.
/// All time fields are in seconds, matching SFZ opcode values directly.
/// sustain_pct is 0.0–100.0, matching the SFZ ampeg_sustain opcode.
struct BomEnvelope
{
    // -------------------------------------------------------------------------
    // Time stages — seconds [0.001 .. ~100]
    // These map directly from Polyphone's SFZ output (already converted from
    // timecents to seconds during the Polyphone SFZ export).
    // -------------------------------------------------------------------------
    std::optional<float> delay_s;    ///< ampeg_delay   / pitcheg_delay / fileg_delay
    std::optional<float> attack_s;   ///< ampeg_attack  / pitcheg_attack / fileg_attack
    std::optional<float> hold_s;     ///< ampeg_hold    / pitcheg_hold  / fileg_hold
    std::optional<float> decay_s;    ///< ampeg_decay   / pitcheg_decay / fileg_decay
    std::optional<float> release_s;  ///< ampeg_release / pitcheg_release / fileg_release

    // -------------------------------------------------------------------------
    // Sustain level
    // For the volume envelope (ampeg_sustain): 0=silence, 100=full level.
    // For the mod envelope (pitcheg/fileg_sustain): same range, but note that
    // Polyphone writes (100 - SF2_value), so 0=no modulation at sustain,
    // 100=full modulation at sustain.
    // -------------------------------------------------------------------------
    std::optional<float> sustain_pct; ///< 0.0–100.0 percent

    // -------------------------------------------------------------------------
    // Key-number scaling (SFZ v2: ampeg_holdcc133 / ampeg_decaycc133)
    // These modify the hold and decay time based on the played MIDI key.
    // Derived from SF2 keynumToVolEnvHold / keynumToVolEnvDecay.
    // Unit: seconds per semitone (can be negative).
    // -------------------------------------------------------------------------
    std::optional<float> key_to_hold_s;  ///< ampeg_holdcc133  / pitcheg_holdcc133 / fileg_holdcc133
    std::optional<float> key_to_decay_s; ///< ampeg_decaycc133 / pitcheg_decaycc133 / fileg_decaycc133

    // -------------------------------------------------------------------------
    // Convenience: check if any parameter is set
    // -------------------------------------------------------------------------
    bool hasAnyValue() const
    {
        return delay_s.has_value()    || attack_s.has_value()   ||
               hold_s.has_value()     || decay_s.has_value()    ||
               sustain_pct.has_value()|| release_s.has_value()  ||
               key_to_hold_s.has_value() || key_to_decay_s.has_value();
    }
};

} // namespace Bom
} // namespace Vhl
