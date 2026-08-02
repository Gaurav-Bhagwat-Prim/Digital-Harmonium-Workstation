// =============================================================================
// VhlDocument.h — VHL Core Library: Decoded Document Model
//
// In-memory object model representing a decoded .vhl binary instrument file.
// Stores header, instrument table, region table, sample directory, lookup maps,
// and computed binary integrity metrics.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "vhl_structs.h"

namespace Vhl {
namespace Tools {
namespace Core {

// =============================================================================
// SampleStats — Calculated per-sample metrics
// =============================================================================

struct SampleStats
{
    uint16_t    sampleId = 0;
    std::string name;
    uint32_t    totalFrames = 0;
    float       durationSec = 0.0f;
    size_t      pcmSizeBytes = 0;
    bool        isLooped = false;
    uint32_t    loopLengthFrames = 0;
};

// =============================================================================
// ComputedStats — Calculated instrument-wide statistics
// =============================================================================

struct ComputedStats
{
    size_t   totalPcmBytes = 0;
    float    totalPcmDurationSec = 0.0f;
    float    avgSampleDurationSec = 0.0f;
    int      loopedSampleCount = 0;
    int      oneShotSampleCount = 0;

    float    avgVolAttackMs = 0.0f;
    float    avgVolReleaseMs = 0.0f;
    float    avgFilterCutoffHz = 0.0f;

    uint8_t  minKeyCovered = 127;
    uint8_t  maxKeyCovered = 0;
    int      activeKeysCount = 0;

    // Largest sample ranking
    std::vector<SampleStats> largestSamples;
};

// =============================================================================
// VhlDocument — Decoded binary container
// =============================================================================

class VhlDocument
{
public:
    VhlDocument() = default;

    std::string                  filePath;
    std::vector<uint8_t>         rawBuffer;
    bool                         isLoaded = false;

    // Header & integrity
    VHLFileHeader                header = {};
    uint32_t                     computedCrc32 = 0;
    bool                         crcValid = false;

    // Decoded Table Collections
    std::vector<VHLInstrumentEntry> instruments;
    std::vector<VHLRegion>          regions;
    std::vector<VHLSampleEntry>     samples;

    // Per-instrument key lookup entries (instrumentId -> vector of 128 lookup entries)
    std::vector<std::vector<VHLLookupEntry>> lookupTables;

    // Computed Statistics
    ComputedStats                stats;

    /// Calculate/refresh computed instrument statistics
    void computeStatistics();
};

} // namespace Core
} // namespace Tools
} // namespace Vhl
