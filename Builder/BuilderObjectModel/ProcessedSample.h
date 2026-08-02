// =============================================================================
// ProcessedSample.h — Enriched BOM: Loaded WAV Sample
//
// Produced by the WAV Loader stage of Asset Processing.
// Contains everything the Backend needs to write a VHLSampleEntry and emit
// the raw PCM block into the file. No further file I/O is needed after this.
//
// Deduplication: if two BomRegions reference the same WAV file (same absolute
// path), they share one ProcessedSample. The AssetProcessor assigns unique
// contiguous integer IDs used as VHLSampleEntry indices.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Vhl {
namespace Processed {

/// Validated loop point pair. Both fields are absolute frame indices.
struct LoopPoints
{
    uint32_t start = 0; ///< Inclusive start frame
    uint32_t end   = 0; ///< Exclusive end frame (matches VHL convention)
    bool     valid = false; ///< True if loop points were verified
};

/// One unique sample, fully loaded into memory.
/// Corresponds exactly to one future VHLSampleEntry + its PCM block.
struct ProcessedSample
{
    // -------------------------------------------------------------------------
    // Identity
    // -------------------------------------------------------------------------

    uint16_t    sampleId;        ///< Contiguous 0-based ID → VHLSampleEntry index
    std::string absolutePath;    ///< Absolute path to the source WAV
    std::string name;            ///< Derived from filename (max 23 chars for VHL_NAME_LEN)

    // -------------------------------------------------------------------------
    // Audio properties (from WAV fmt chunk)
    // -------------------------------------------------------------------------

    uint32_t sampleRate;     ///< Source sample rate in Hz (e.g. 44100)
    uint8_t  bitDepth;       ///< PCM bit depth: 16 or 24
    uint8_t  channels;       ///< 1 = mono, 2 = stereo

    // -------------------------------------------------------------------------
    // Frame count
    // -------------------------------------------------------------------------

    uint32_t totalFrames;    ///< Total PCM frames (data_chunk_size / (bitDepth/8 * channels))

    // -------------------------------------------------------------------------
    // Root key and fine tune (from smpl chunk or region overrideRootKey)
    // -------------------------------------------------------------------------

    uint8_t  rootKey;        ///< MIDI note 0-127 — already resolved (BomRegion.pitch_keycenter wins)
    int8_t   fineTuneCents;  ///< Fine tune in cents (-50 to +50, from smpl chunk or SFZ tune=)

    // -------------------------------------------------------------------------
    // Loop points (validated)
    // -------------------------------------------------------------------------

    LoopPoints loopPoints;   ///< Validated loop. valid=false → no loop in WAV

    // -------------------------------------------------------------------------
    // PCM data (normalized to 16-bit signed)
    // -------------------------------------------------------------------------

    /// Raw PCM data, interleaved if stereo (L0,R0,L1,R1,...).
    /// Always 16-bit signed regardless of source bit depth.
    /// 24-bit sources are truncated to 16-bit (top 16 bits of the 24-bit word).
    std::vector<int16_t> pcm;

    // -------------------------------------------------------------------------
    // Stereo linking
    // -------------------------------------------------------------------------

    uint16_t linkedSampleId = 0xFFFF; ///< Partner sample ID for stereo (0xFFFF = none)
    bool     isRightChannel = false;  ///< True if this is the right channel

    // -------------------------------------------------------------------------
    // VHL flags (pre-computed for the Backend)
    // -------------------------------------------------------------------------

    uint8_t vhlFlags = 0;   ///< VHL_SAMPLE_FLAG_* bitmask

    // -------------------------------------------------------------------------
    // PCM data offset within the VHL PCM section (set by Backend)
    // -------------------------------------------------------------------------

    uint32_t pcmSectionOffset = 0; ///< Byte offset within PCM section (Backend fills this)

    // -------------------------------------------------------------------------
    // Convenience
    // -------------------------------------------------------------------------

    bool isMono()   const { return channels == 1; }
    bool isStereo() const { return channels == 2; }
    size_t frameBytes() const { return (bitDepth / 8) * channels; }
    size_t totalPcmBytes() const { return pcm.size() * sizeof(int16_t); }
};

} // namespace Processed
} // namespace Vhl
