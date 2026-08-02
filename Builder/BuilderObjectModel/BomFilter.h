// =============================================================================
// BomFilter.h — VHL Builder Object Model: Biquad Low-Pass Filter
//
// Represents the filter configuration as exported by Polyphone.
// Polyphone always exports "fil_type=lpf_2p" (the only type SF2 supports).
// The cutoff frequency is in Hz (already converted from absolute cents during
// the Polyphone SFZ export). The resonance is in dB.
// =============================================================================

#pragma once

#include <optional>
#include "BomTypes.h"

namespace Vhl {
namespace Bom {

/// Filter parameters for one region.
/// All values are in SFZ opcode units (Hz, dB, cents/key, integer key number).
struct BomFilter
{
    // -------------------------------------------------------------------------
    // Type
    // Polyphone always writes lpf_2p. Store it for validation.
    // -------------------------------------------------------------------------
    FilterType type = FilterType::None; ///< fil_type opcode

    // -------------------------------------------------------------------------
    // Cutoff frequency
    // SFZ: cutoff=<Hz>. Polyphone exports as integer Hz.
    // Range: 1 Hz – Nyquist (~22050 Hz at 44100 Hz sample rate).
    // -------------------------------------------------------------------------
    std::optional<float> cutoff_hz; ///< cutoff= opcode, Hz

    // -------------------------------------------------------------------------
    // Resonance (Q factor expressed in dB)
    // SFZ: resonance=<dB>. Range: 0–96 dB.
    // -------------------------------------------------------------------------
    std::optional<float> resonance_db; ///< resonance= opcode, dB

    // -------------------------------------------------------------------------
    // Velocity tracking
    // SFZ: fil_veltrack=<cents>. How much velocity affects cutoff.
    // Negative = lower velocity → lower cutoff (typical).
    // -------------------------------------------------------------------------
    std::optional<int> vel_track_cents; ///< fil_veltrack= opcode, cents

    // -------------------------------------------------------------------------
    // Key tracking
    // SFZ: fil_keytrack=<cents/key> and fil_keycenter=<MIDI note>.
    // fil_keytrack: how much each semitone changes the cutoff (cents/semitone).
    // fil_keycenter: the pivot MIDI key where cutoff = nominal value.
    // -------------------------------------------------------------------------
    std::optional<int> key_track_cents; ///< fil_keytrack= opcode, cents/semitone
    std::optional<uint8_t> key_center;  ///< fil_keycenter= opcode, MIDI note 0–127

    // -------------------------------------------------------------------------
    // Convenience: filter is active when cutoff_hz is set
    // -------------------------------------------------------------------------
    bool isActive() const { return cutoff_hz.has_value(); }
};

} // namespace Bom
} // namespace Vhl
