// =============================================================================
// AssetProcessor.h — VHL Builder Phase 2B: Orchestrator
//
// Transforms a BuilderProject (output of Phase 2A / SfzParser) into a
// ProcessedProject (output of Phase 2B, input to Phase 2C / Backend).
//
// Pipeline (in order):
//
//   Stage 1: Sample Discovery & Deduplication
//     - Walk all BomRegions across all instruments
//     - Collect unique sample paths (deduplicate by absolute path)
//     - Assign contiguous integer sample IDs
//
//   Stage 2: WAV Loading
//     - Load each unique sample via WavLoader
//     - Extract PCM data, loop points, audio properties
//     - Resolve root_key from BomRegion.pitch_keycenter (overrides WAV default)
//     - Validate sample rate against project target rate (warning if mismatch)
//
//   Stage 3: Parameter Conversion
//     - For each BomRegion, call ParameterConverter::convert()
//     - Produces ProcessedRegion with VHL-native field values
//     - Injects sample_id from Step 1
//
//   Stage 4: Instrument Assembly
//     - Assign contiguous instrument IDs
//     - Assign regionTableOffset (global region start index for each instrument)
//     - Sort regions by key_lo then vel_lo (for lookup table correctness)
//
//   Stage 5: Lookup Table Generation
//     - Build keyLookup[128] for each instrument via LookupTableBuilder
//     - Validate coverage and report gaps
//
//   Stage 6: Statistics & Size Estimation
//     - Count total regions, samples, PCM bytes
//     - Estimate output VHL file size
// =============================================================================

#pragma once

#include "../BuilderObjectModel/BuilderProject.h"
#include "../BuilderObjectModel/ProcessedProject.h"
#include "../Frontend/ParseError.h"
#include <vector>

namespace Vhl {
namespace AssetProcessing {

class AssetProcessor
{
public:
    AssetProcessor() = default;

    /// Run all 6 stages and return the fully processed project.
    /// On error, result.processingErrors > 0; partial data may still be valid.
    Processed::ProcessedProject process(const Bom::BuilderProject& project,
                                         Frontend::DiagnosticCollector& diag);

private:
    // Stage implementations
    struct SampleRecord
    {
        std::string absolutePath;
        uint16_t    sampleId;
    };

    // Stage 1: collect and deduplicate sample paths
    std::vector<SampleRecord> discoverSamples(const Bom::BuilderProject& project,
                                               Frontend::DiagnosticCollector& diag);

    // Stage 2: load all WAV files
    void loadSamples(std::vector<SampleRecord>& records,
                     Processed::ProcessedProject& result,
                     const Bom::BuilderProject& project,
                     Frontend::DiagnosticCollector& diag);

    // Stage 3: convert one BomInstrument → ProcessedInstrument
    Processed::ProcessedInstrument convertInstrument(
        const Bom::BomInstrument&            inst,
        uint8_t                              instrumentId,
        uint16_t                             regionTableOffset,
        const std::vector<SampleRecord>&     sampleRecords,
        Frontend::DiagnosticCollector&       diag);

    // Resolve sample_id for a given absolute path
    uint16_t resolveSampleId(const std::string& absPath,
                              const std::vector<SampleRecord>& records,
                              int sourceLine,
                              Frontend::DiagnosticCollector& diag);
};

} // namespace AssetProcessing
} // namespace Vhl
