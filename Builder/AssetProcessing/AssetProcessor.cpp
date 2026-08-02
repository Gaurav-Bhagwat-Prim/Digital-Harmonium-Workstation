// =============================================================================
// AssetProcessor.cpp — VHL Builder Phase 2B: Orchestrator Implementation
// =============================================================================

#include "AssetProcessor.h"
#include "WavLoader.h"
#include "ParameterConverter.h"
#include "LookupTableBuilder.h"

#include <algorithm>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace Vhl {
namespace AssetProcessing {

// =============================================================================
// process — main entry point
// =============================================================================

Processed::ProcessedProject AssetProcessor::process(
    const Bom::BuilderProject& project,
    Frontend::DiagnosticCollector& diag)
{
    Processed::ProcessedProject result;
    result.outputVhlPath    = project.outputVhlPath;
    result.targetSampleRate = project.targetSampleRate;
    result.dmaBlockSize     = static_cast<uint16_t>(project.dmaBlockSize);

    if (project.instruments.empty())
    {
        diag.warning(-1, "project", "No instruments to process");
        return result;
    }

    // -------------------------------------------------------------------------
    // Stage 1: Discover and deduplicate samples
    // -------------------------------------------------------------------------
    diag.info(-1, "stage1", "Discovering samples...");
    auto sampleRecords = discoverSamples(project, diag);
    diag.info(-1, "stage1", std::to_string(sampleRecords.size()) + " unique samples found");

    // -------------------------------------------------------------------------
    // Stage 2: Load WAV files
    // -------------------------------------------------------------------------
    diag.info(-1, "stage2", "Loading WAV files...");
    loadSamples(sampleRecords, result, project, diag);

    if (result.samples.empty())
    {
        diag.error(-1, "stage2", "No samples could be loaded — aborting");
        ++result.processingErrors;
        return result;
    }

    // -------------------------------------------------------------------------
    // Stages 3–5: Convert instruments
    // -------------------------------------------------------------------------
    uint16_t globalRegionOffset = 0;
    uint8_t  instrumentId       = 0;

    for (const auto& inst : project.instruments)
    {
        diag.info(-1, "stage3", "Processing instrument: " + inst.metadata.name);

        auto procInst = convertInstrument(inst, instrumentId, globalRegionOffset,
                                           sampleRecords, diag);

        globalRegionOffset += static_cast<uint16_t>(procInst.regionCount());
        ++instrumentId;

        result.instruments.push_back(std::move(procInst));
    }

    // -------------------------------------------------------------------------
    // Stage 6: Statistics
    // -------------------------------------------------------------------------
    result.totalRegions = static_cast<int>(globalRegionOffset);
    result.totalSamples = static_cast<int>(result.samples.size());

    for (const auto& s : result.samples)
    {
        result.totalPcmFrames += static_cast<int>(s.totalFrames);
        result.totalPcmBytes  += static_cast<int>(s.totalPcmBytes());
    }

    diag.info(-1, "summary",
        "Total: " + std::to_string(result.totalRegions) + " regions, "
        + std::to_string(result.totalSamples) + " samples, "
        + std::to_string(result.totalPcmBytes / 1024) + " KB PCM");

    return result;
}

// =============================================================================
// Stage 1: discoverSamples
// =============================================================================

std::vector<AssetProcessor::SampleRecord> AssetProcessor::discoverSamples(
    const Bom::BuilderProject& project,
    Frontend::DiagnosticCollector& diag)
{
    // Use a map to deduplicate by absolute path
    std::map<std::string, uint16_t> seen;
    std::vector<SampleRecord> records;
    uint16_t nextId = 0;

    for (const auto& inst : project.instruments)
    {
        for (const auto& region : inst.regions)
        {
            const std::string& path = region.sample.absolutePath;
            if (path.empty()) continue;

            if (seen.find(path) == seen.end())
            {
                if (nextId >= 1024)
                {
                    diag.error(-1, "samples",
                        "Sample count exceeds VHL_MAX_SAMPLES (1024) — truncating");
                    break;
                }
                seen[path] = nextId;
                records.push_back({path, nextId});
                ++nextId;
            }
        }
    }

    return records;
}

// =============================================================================
// Stage 2: loadSamples
// =============================================================================

void AssetProcessor::loadSamples(std::vector<SampleRecord>& records,
                                   Processed::ProcessedProject& result,
                                   const Bom::BuilderProject& project,
                                   Frontend::DiagnosticCollector& diag)
{
    WavLoader loader;
    result.samples.reserve(records.size());

    for (auto& rec : records)
    {
        std::string basename = fs::path(rec.absolutePath).filename().string();
        diag.info(-1, "wav", "Loading: " + basename);

        Frontend::DiagnosticCollector wavDiag(basename);
        auto s = loader.load(rec.absolutePath, wavDiag);

        // Forward WAV diagnostics to main collector
        for (const auto& d : wavDiag.diagnostics())
            diag.warning(d.line, "wav:" + d.opcode, d.message);

        if (!s.has_value())
        {
            diag.error(-1, "wav", "Failed to load: " + rec.absolutePath);
            ++result.processingErrors;

            // Insert a placeholder sample so IDs stay consistent
            Processed::ProcessedSample placeholder;
            placeholder.sampleId   = rec.sampleId;
            placeholder.name       = basename;
            placeholder.sampleRate = project.targetSampleRate;
            placeholder.bitDepth   = 16;
            placeholder.channels   = 1;
            placeholder.totalFrames = 0;
            result.samples.push_back(std::move(placeholder));
            continue;
        }

        s->sampleId = rec.sampleId;

        // Derive name from filename (max 23 chars)
        std::string stem = fs::path(rec.absolutePath).stem().string();
        if (stem.size() > 23) stem = stem.substr(0, 23);
        s->name = stem;

        // Warn on sample rate mismatch
        if (s->sampleRate != project.targetSampleRate)
        {
            diag.warning(-1, "wav",
                basename + ": sample rate " + std::to_string(s->sampleRate)
                + " Hz differs from target " + std::to_string(project.targetSampleRate)
                + " Hz — runtime resampling required");
        }

        result.samples.push_back(std::move(*s));
    }
}

// =============================================================================
// Stage 3–5: convertInstrument
// =============================================================================

Processed::ProcessedInstrument AssetProcessor::convertInstrument(
    const Bom::BomInstrument&            inst,
    uint8_t                              instrumentId,
    uint16_t                             regionTableOffset,
    const std::vector<SampleRecord>&     sampleRecords,
    Frontend::DiagnosticCollector&       diag)
{
    Processed::ProcessedInstrument procInst;
    procInst.instrumentId      = instrumentId;
    procInst.regionTableOffset = regionTableOffset;
    procInst.midiBank          = static_cast<uint8_t>(inst.midiBank);
    procInst.midiProgram       = static_cast<uint8_t>(inst.midiProgram);

    // Truncate name to 23 chars (VHL_NAME_LEN - 1 for null terminator)
    std::string nm = inst.metadata.name;
    if (nm.size() > 23) nm = nm.substr(0, 23);
    procInst.name = nm;

    // -------------------------------------------------------------------------
    // Stage 3: Convert each BomRegion → ProcessedRegion
    // -------------------------------------------------------------------------
    for (const auto& r : inst.regions)
    {
        uint16_t sid = resolveSampleId(r.sample.absolutePath, sampleRecords,
                                        r.sourceLine, diag);

        Frontend::DiagnosticCollector regionDiag(
            inst.metadata.name + ":region" + std::to_string(r.regionIndex));

        auto pr = ParameterConverter::convert(r, sid, regionDiag);

        // Resolve root key: BomRegion.pitch_keycenter takes priority over WAV root
        if (r.pitch_keycenter.has_value())
            pr.fine_tune = ParameterConverter::fineTuneCentsToVhl(
                               r.tune_cents.value_or(0));
        // (root key is stored in VHLSampleEntry.root_key, set during loadSamples)

        // Forward diagnostics
        for (const auto& d : regionDiag.diagnostics())
            diag.warning(d.line, d.opcode, d.message);

        procInst.regions.push_back(pr);
    }

    // -------------------------------------------------------------------------
    // Stage 4: Sort regions — key_lo ascending, then vel_lo ascending
    // -------------------------------------------------------------------------
    std::sort(procInst.regions.begin(), procInst.regions.end(),
              [](const Processed::ProcessedRegion& a, const Processed::ProcessedRegion& b)
              {
                  if (a.key_lo != b.key_lo) return a.key_lo < b.key_lo;
                  return a.vel_lo < b.vel_lo;
              });

    // -------------------------------------------------------------------------
    // Stage 5: Build lookup table
    // -------------------------------------------------------------------------
    Frontend::DiagnosticCollector lutDiag(procInst.name);
    LookupTableBuilder::build(procInst, lutDiag);
    LookupTableBuilder::validate(procInst, lutDiag);

    for (const auto& d : lutDiag.diagnostics())
        diag.warning(d.line, "lookup:" + d.opcode, d.message);

    return procInst;
}

// =============================================================================
// resolveSampleId
// =============================================================================

uint16_t AssetProcessor::resolveSampleId(const std::string& absPath,
                                          const std::vector<SampleRecord>& records,
                                          int sourceLine,
                                          Frontend::DiagnosticCollector& diag)
{
    for (const auto& rec : records)
        if (rec.absolutePath == absPath) return rec.sampleId;

    diag.error(sourceLine, "sample_id",
        "Sample path not found in discovery list: " + absPath);
    return 0; // fallback to first sample
}

} // namespace AssetProcessing
} // namespace Vhl
