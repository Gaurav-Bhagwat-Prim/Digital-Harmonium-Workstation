// =============================================================================
// ProcessedProject.h — Enriched BOM: Top-Level Processed Container
//
// Produced by AssetProcessor. Contains all instruments fully processed.
// This is the output of Phase 2B and the input to Phase 2C (Backend).
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <map>
#include "ProcessedSample.h"
#include "ProcessedRegion.h"

namespace Vhl {
namespace Processed {

// =============================================================================
// LookupEntry — one cell of the key lookup table (maps to VHLLookupEntry)
// =============================================================================

struct LookupEntry
{
    uint16_t region_start_idx = 0xFFFF; ///< VHLLookupEntry.region_start_idx
    uint8_t  region_count     = 0;      ///< VHLLookupEntry.region_count
};

// =============================================================================
// ProcessedInstrument — one instrument, all data processed
// =============================================================================

struct ProcessedInstrument
{
    // -------------------------------------------------------------------------
    // Identity (from BomMetadata)
    // -------------------------------------------------------------------------

    uint8_t     instrumentId = 0; ///< 0-based sequential ID
    uint8_t     midiBank     = 0; ///< MIDI bank
    uint8_t     midiProgram  = 0; ///< MIDI program number
    std::string name;             ///< Instrument name (truncated to 23 chars)

    // -------------------------------------------------------------------------
    // Regions (all processed, in key-ascending order)
    // -------------------------------------------------------------------------

    std::vector<ProcessedRegion> regions;

    /// Global start index of this instrument's first region in the VHL file's
    /// flat region table. Set by AssetProcessor during ID assignment.
    uint16_t regionTableOffset = 0;

    // -------------------------------------------------------------------------
    // Lookup table (128 entries, one per MIDI key)
    // -------------------------------------------------------------------------

    /// Key lookup table: keyLookup[midiKey] → (region_start_idx, region_count).
    /// region_start_idx is an index into the GLOBAL region table (regionTableOffset + local idx).
    LookupEntry keyLookup[128];

    // -------------------------------------------------------------------------
    // Convenience
    // -------------------------------------------------------------------------

    size_t regionCount() const { return regions.size(); }

    bool hasRegionsForKey(uint8_t key) const
    {
        return keyLookup[key].region_count > 0;
    }
};

// =============================================================================
// ProcessedProject — complete output of Phase 2B
// =============================================================================

struct ProcessedProject
{
    // -------------------------------------------------------------------------
    // Build configuration (mirrors BuilderProject)
    // -------------------------------------------------------------------------

    std::string  outputVhlPath;
    uint32_t     targetSampleRate = 44100;
    uint16_t     dmaBlockSize     = 512;

    // -------------------------------------------------------------------------
    // Instruments
    // -------------------------------------------------------------------------

    std::vector<ProcessedInstrument> instruments;

    // -------------------------------------------------------------------------
    // Sample directory (globally deduplicated)
    // All samples across all instruments share this flat list.
    // Indexed by ProcessedRegion.sample_id.
    // -------------------------------------------------------------------------

    std::vector<ProcessedSample> samples;

    // -------------------------------------------------------------------------
    // Build statistics
    // -------------------------------------------------------------------------

    int  totalRegions        = 0;
    int  totalSamples        = 0;
    int  totalPcmFrames      = 0;
    int  totalPcmBytes       = 0;
    int  processingWarnings  = 0;
    int  processingErrors    = 0;

    // -------------------------------------------------------------------------
    // Convenience
    // -------------------------------------------------------------------------

    bool isValid() const { return processingErrors == 0 && !instruments.empty(); }
    size_t instrumentCount() const { return instruments.size(); }
    size_t sampleCount()     const { return samples.size(); }

    /// Estimated VHL file size in bytes (approximate, before alignment).
    size_t estimatedFileSize() const
    {
        size_t headerSize  = 128;
        size_t instSize    = instruments.size() * 40;
        size_t regionSize  = totalRegions * 80;
        size_t sampleDirSz = totalSamples * 32;
        // Lookup: 4 (header) + 128*4 (entries) per instrument
        size_t lookupSize  = instruments.size() * (4 + 128 * 4);
        size_t pcmSize     = static_cast<size_t>(totalPcmBytes);
        return headerSize + instSize + regionSize + sampleDirSz + lookupSize + pcmSize;
    }
};

} // namespace Processed
} // namespace Vhl
