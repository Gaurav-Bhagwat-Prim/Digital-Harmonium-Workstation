// =============================================================================
// BuilderProject.h — VHL Builder Object Model: Top-Level Container
//
// The BuilderProject is the root object produced by the Frontend (SfzParser).
// It contains all instruments parsed from one or more SFZ files, and becomes
// the single source of truth for all subsequent Builder stages.
//
// The Frontend produces it. Asset Processing enriches it (WAV loading, param
// conversion). The Backend consumes it and serializes it to VHL binary.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "BomInstrument.h"

namespace Vhl {
namespace Bom {

/// Top-level container for a complete VHL build job.
/// One BuilderProject corresponds to exactly one .vhl output file.
struct BuilderProject
{
    // -------------------------------------------------------------------------
    // Build configuration
    // -------------------------------------------------------------------------

    /// Root directory containing all SFZ files and sample subdirectories.
    /// All relative paths in BomSampleRef are resolved from here.
    std::string rootDirectory;

    /// Output VHL file path.
    std::string outputVhlPath;

    /// Target sample rate for the ESP32 runtime (Hz).
    /// All samples will be validated against this. Default: 44100.
    uint32_t targetSampleRate = 44100;

    /// Number of DMA buffer samples per block (for VHL header).
    /// Default: 512.
    uint32_t dmaBlockSize = 512;

    // -------------------------------------------------------------------------
    // Instruments
    // -------------------------------------------------------------------------

    /// All parsed instruments. One entry per SFZ file.
    std::vector<BomInstrument> instruments;

    // -------------------------------------------------------------------------
    // Build statistics (filled during parsing)
    // -------------------------------------------------------------------------

    int totalRegions  = 0; ///< Total region count across all instruments
    int totalSamples  = 0; ///< Total unique sample count (after deduplication)
    int parseWarnings = 0; ///< Number of non-fatal warnings during parsing
    int parseErrors   = 0; ///< Number of errors (project may be incomplete)

    // -------------------------------------------------------------------------
    // Convenience
    // -------------------------------------------------------------------------

    bool isValid() const { return parseErrors == 0 && !instruments.empty(); }
    size_t instrumentCount() const { return instruments.size(); }

    void addInstrument(BomInstrument&& inst)
    {
        totalRegions += static_cast<int>(inst.regionCount());
        instruments.push_back(std::move(inst));
    }
};

} // namespace Bom
} // namespace Vhl
