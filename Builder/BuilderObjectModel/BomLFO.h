// =============================================================================
// BomLFO.h — VHL Builder Object Model: Modulation LFO and Vibrato LFO
//
// Polyphone exports two LFO types from the SF2 specification:
//
//   Modulation LFO (amplfo_* / fillfo_*):
//     Can modulate volume (amplfo_depth) and filter cutoff (fillfo_depth).
//     Note: modLfoToPitch is promoted to VibLFO during Polyphone's SFZ export,
//     so it never appears in the amplfo_ or fillfo_ opcodes.
//
//   Vibrato LFO (pitchlfo_*):
//     Modulates pitch only. May have originated from the SF2 vibrato LFO or
//     from a modLFO-to-pitch promotion in Polyphone (transparent to us).
//
// All frequency values are in Hz (converted from absolute cents by Polyphone).
// All delay values are in seconds (converted from timecents by Polyphone).
// Depth values are in their natural SFZ units (dB for volume, cents for pitch/filter).
// =============================================================================

#pragma once

#include <optional>

namespace Vhl {
namespace Bom {

/// Modulation LFO — modulates volume and/or filter cutoff.
/// Corresponds to SFZ opcodes: amplfo_* and fillfo_*
struct BomModLFO
{
    // -------------------------------------------------------------------------
    // Timing — shared between volume and filter destinations
    // Polyphone exports the same delay/freq for both amplfo and fillfo.
    // -------------------------------------------------------------------------
    std::optional<float> delay_s;  ///< amplfo_delay / fillfo_delay — seconds before LFO starts
    std::optional<float> freq_hz;  ///< amplfo_freq  / fillfo_freq  — oscillation rate in Hz

    // -------------------------------------------------------------------------
    // Volume modulation destination
    // SFZ: amplfo_depth=<dB>. Positive values increase, negative decrease volume.
    // -------------------------------------------------------------------------
    std::optional<float> depth_volume_db; ///< amplfo_depth, dB

    // -------------------------------------------------------------------------
    // Filter cutoff modulation destination
    // SFZ: fillfo_depth=<cents>. Positive values raise, negative lower cutoff.
    // -------------------------------------------------------------------------
    std::optional<float> depth_filter_cents; ///< fillfo_depth, cents

    // -------------------------------------------------------------------------
    // Convenience
    // -------------------------------------------------------------------------
    bool isActive() const
    {
        return depth_volume_db.has_value() || depth_filter_cents.has_value();
    }
};

/// Vibrato LFO — modulates pitch only.
/// Corresponds to SFZ opcodes: pitchlfo_*
/// May originate from SF2 vibLFO or from promoted modLFO-to-pitch.
struct BomVibLFO
{
    std::optional<float> delay_s;       ///< pitchlfo_delay — seconds before LFO starts
    std::optional<float> freq_hz;       ///< pitchlfo_freq  — oscillation rate in Hz
    std::optional<int>   depth_cents;   ///< pitchlfo_depth — peak pitch deviation in cents

    bool isActive() const { return depth_cents.has_value(); }
};

} // namespace Bom
} // namespace Vhl
