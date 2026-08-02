// =============================================================================
// BomTypes.h — VHL Builder Object Model: Fundamental types and enumerations
//
// Part of the VHL Builder, Phase 2A.
// These types mirror the VHL binary specification (vhl_structs.h) but use
// C++ idioms and are designed for the desktop compiler, not the embedded target.
// =============================================================================

#pragma once

#include <cstdint>
#include <string>

namespace Vhl {
namespace Bom {

// -----------------------------------------------------------------------------
// MIDI key and velocity ranges
// -----------------------------------------------------------------------------

/// A key range covering MIDI notes [lo, hi] inclusive. Defaults to full range.
struct KeyRange
{
    uint8_t lo = 0;
    uint8_t hi = 127;

    bool isDefault() const { return lo == 0 && hi == 127; }
    bool contains(uint8_t key) const { return key >= lo && key <= hi; }
};

/// A velocity range [lo, hi] inclusive. Defaults to full range.
struct VelRange
{
    uint8_t lo = 0;
    uint8_t hi = 127;

    bool isDefault() const { return lo == 0 && hi == 127; }
    bool contains(uint8_t vel) const { return vel >= lo && vel <= hi; }
};

// -----------------------------------------------------------------------------
// Loop mode (matches VHL binary + SFZ spec)
// -----------------------------------------------------------------------------

/// Loop mode for a region, directly mirroring SFZ loop_mode and VHL sampleModes.
enum class LoopMode : uint8_t
{
    NoLoop         = 0, ///< SFZ: no_loop        (SF2 sampleModes bit 0 = 0)
    LoopContinuous = 1, ///< SFZ: loop_continuous (SF2 sampleModes bit 0 = 1)
    OneShot        = 2, ///< SFZ: one_shot        (SF2 sampleModes = 2, trigger=release)
    LoopSustain    = 3, ///< SFZ: loop_sustain    (SF2 sampleModes bit 1 = 1)
};

// -----------------------------------------------------------------------------
// Trigger mode
// -----------------------------------------------------------------------------

/// Controls when a region is triggered. Polyphone exports 'release' for
/// one-shot release samples (sampleModes==2). All other regions use 'attack'.
enum class TriggerMode : uint8_t
{
    Attack  = 0, ///< SFZ: attack  — triggered on note-on (default)
    Release = 1, ///< SFZ: release — triggered on note-off
    First   = 2, ///< SFZ: first
    Legato  = 3, ///< SFZ: legato
};

// -----------------------------------------------------------------------------
// Filter type
// -----------------------------------------------------------------------------

/// Filter type. Polyphone always exports lpf_2p (the only type SF2 supports).
enum class FilterType : uint8_t
{
    LowPass2P  = 0, ///< SFZ: lpf_2p  — 2-pole low-pass (default, Polyphone-only)
    HighPass2P = 1, ///< SFZ: hpf_2p
    BandPass2P = 2, ///< SFZ: bpf_2p
    None       = 0xFF,
};

// -----------------------------------------------------------------------------
// Modulator envelope target
// Polyphone exports the single SF2 mod envelope to BOTH pitch and filter targets.
// The BOM stores them unified (same values, separate depth amounts).
// -----------------------------------------------------------------------------

} // namespace Bom
} // namespace Vhl
