// =============================================================================
// main.cpp — VHL Builder CLI Entry Point
//
// Full pipeline: 2A (SFZ Parse) → 2B (Asset Processing) → 2C (Binary Compile)
//
// Usage:
//   vhl_builder <instrument.sfz> [instrument2.sfz ...] [--output <path.vhl>]
//                                                      [--tag <build-tag>]
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "BuilderObjectModel/BuilderProject.h"
#include "BuilderObjectModel/BomRegion.h"
#include "BuilderObjectModel/ProcessedProject.h"
#include "Frontend/SfzParser.h"
#include "Frontend/NoteParser.h"
#include "AssetProcessing/AssetProcessor.h"
#include "Backend/Serializer.h"

namespace fs = std::filesystem;
using namespace Vhl;

// ---------------------------------------------------------------------------
// Print one BomRegion (Phase 2A diagnostic)
// ---------------------------------------------------------------------------

static void printRegion(const Bom::BomRegion& r, int index)
{
    std::cout << "  Region " << index
              << " [" << (int)r.keyRange.lo  << ".." << (int)r.keyRange.hi  << "]"
              << " vel["    << (int)r.velRange.lo << ".." << (int)r.velRange.hi << "]";

    if (!r.sample.absolutePath.empty())
        std::cout << "  sample=" << fs::path(r.sample.absolutePath).filename().string();

    if (r.pitch_keycenter)
        std::cout << "  root=" << Frontend::NoteParser::toName(*r.pitch_keycenter);

    std::cout << "\n";

    if (r.hasVolEnv())
    {
        const auto& e = r.vol_env;
        std::cout << "    VolEnv:";
        if (e.attack_s)    std::cout << " A=" << *e.attack_s  << "s";
        if (e.decay_s)     std::cout << " D=" << *e.decay_s   << "s";
        if (e.sustain_pct) std::cout << " S=" << *e.sustain_pct << "%";
        if (e.release_s)   std::cout << " R=" << *e.release_s << "s";
        std::cout << "\n";
    }
    if (r.hasFilter())
    {
        const auto& f = r.filter;
        std::cout << "    Filter: cutoff=" << *f.cutoff_hz << "Hz";
        if (f.resonance_db) std::cout << " Q=" << *f.resonance_db << "dB";
        std::cout << "\n";
    }
}

// ---------------------------------------------------------------------------
// Print Phase 2A BomInstrument summary
// ---------------------------------------------------------------------------

static void printInstrument(const Bom::BomInstrument& inst)
{
    std::cout << "=============================================================\n";
    std::cout << "Instrument: " << inst.metadata.name << "\n";
    if (!inst.metadata.author.empty())
        std::cout << "  Author:  " << inst.metadata.author << "\n";
    std::cout << "  Source:  " << inst.metadata.sfzPath  << "\n";
    std::cout << "  Regions: " << inst.regionCount()      << "\n\n";
    for (size_t i = 0; i < inst.regions.size(); ++i)
        printRegion(inst.regions[i], static_cast<int>(i));
}

// ---------------------------------------------------------------------------
// Print one ProcessedSample (Phase 2B)
// ---------------------------------------------------------------------------

static void printSample(const Processed::ProcessedSample& s)
{
    std::cout << "  Sample[" << s.sampleId << "] " << s.name
              << "  " << s.sampleRate << "Hz"
              << "  " << (int)s.bitDepth << "bit"
              << "  " << (s.channels == 1 ? "mono" : "stereo")
              << "  " << s.totalFrames << " frames";
    if (s.loopPoints.valid)
        std::cout << "  loop[" << s.loopPoints.start << ".." << s.loopPoints.end << "]";
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Print Phase 2B summary
// ---------------------------------------------------------------------------

static void printProcessedProject(const Processed::ProcessedProject& p)
{
    std::cout << "\n=============================================================\n";
    std::cout << "Phase 2B: Asset Processing Results\n";
    std::cout << "=============================================================\n";

    std::cout << "\n--- Sample Directory (" << p.samples.size() << " unique samples) ---\n";
    for (const auto& s : p.samples)
        printSample(s);

    for (const auto& inst : p.instruments)
    {
        std::cout << "\n--- Instrument: " << inst.name
                  << " (" << inst.regionCount() << " regions) ---\n";

        int keysWithData = 0;
        for (int k = 0; k < 128; ++k)
            if (inst.keyLookup[k].region_count > 0) ++keysWithData;
        std::cout << "  Keys covered : " << keysWithData << "/128\n";

        for (const auto& r : inst.regions)
        {
            std::cout << "  Region[" << r.sourceRegionIndex << "]"
                      << " key["    << (int)r.key_lo << ".." << (int)r.key_hi << "]"
                      << " vel["    << (int)r.vel_lo << ".." << (int)r.vel_hi << "]"
                      << " sample=" << r.sample_id
                      << " atten="  << r.attenuation_cb << "cb"
                      << " loop="   << (int)r.loop_mode
                      << " A="      << r.vol_env.attack_ms  << "ms"
                      << " D="      << r.vol_env.decay_ms   << "ms"
                      << " S="      << r.vol_env.sustain_lvl << "cb"
                      << " R="      << r.vol_env.release_ms  << "ms"
                      << "\n";
        }
    }

    size_t est = p.estimatedFileSize();
    std::cout << "\nEstimated VHL size : " << est / 1024 << " KB\n";
    std::cout << "  Regions  : " << p.totalRegions  << "\n";
    std::cout << "  Samples  : " << p.totalSamples  << "\n";
    std::cout << "  PCM data : " << p.totalPcmBytes / 1024 << " KB\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::cout << "VHL Builder v2.0 — Full Compiler Toolchain\n";
    std::cout << "  Phase 2A: SFZ Parsing\n";
    std::cout << "  Phase 2B: Asset Processing\n";
    std::cout << "  Phase 2C: Binary Serialization\n";
    std::cout << "============================================\n\n";

    if (argc < 2)
    {
        std::cerr << "Usage: vhl_builder <instrument.sfz> [instrument2.sfz ...]\n";
        std::cerr << "       [--output <output.vhl>] [--tag <build-tag>]\n";
        std::cerr << "       [--no-compile]    (parse + process only, skip .vhl write)\n";
        return 1;
    }

    // Parse CLI arguments
    std::vector<std::string> sfzPaths;
    std::string              outputPath;
    std::string              buildTag;
    bool                     noCompile = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc)       { outputPath = argv[++i]; }
        else if (arg == "--tag"    && i + 1 < argc)  { buildTag   = argv[++i]; }
        else if (arg == "--no-compile")               { noCompile  = true;      }
        else if (arg[0] != '-')                       { sfzPaths.push_back(arg); }
    }

    if (sfzPaths.empty())
    {
        std::cerr << "[FATAL] No SFZ files specified\n";
        return 1;
    }

    // Derive output path from first SFZ if not explicitly set
    if (outputPath.empty())
    {
        fs::path sfz0 = sfzPaths[0];
        outputPath = (sfz0.parent_path() / sfz0.stem()).string() + ".vhl";
    }

    Bom::BuilderProject project;
    project.rootDirectory  = fs::path(sfzPaths[0]).parent_path().string();
    project.outputVhlPath  = outputPath;
    project.targetSampleRate = 44100;
    project.dmaBlockSize     = 512;

    int totalWarnings = 0;
    int totalErrors   = 0;

    // =========================================================================
    // Phase 2A: SFZ Parsing
    // =========================================================================

    std::cout << "--- Phase 2A: SFZ Parsing ---\n\n";
    Frontend::SfzParser parser;

    for (const auto& sfzPath : sfzPaths)
    {
        std::cout << "Parsing: " << sfzPath << "\n";

        if (!fs::exists(sfzPath))
        {
            std::cerr << "[FATAL] File not found: " << sfzPath << "\n";
            ++totalErrors;
            continue;
        }

        auto result = parser.parse(sfzPath);
        for (const auto& d : result.diagnostics)
            std::cout << d.format() << "\n";

        totalWarnings += result.countWarnings();
        totalErrors   += result.countErrors();

        if (result.fatal)
        {
            std::cerr << "[FATAL] Parsing failed: " << sfzPath << "\n";
            ++totalErrors;
            continue;
        }

        project.addInstrument(std::move(result.value));
    }

    for (const auto& inst : project.instruments)
        printInstrument(inst);

    std::cout << "\n=== Phase 2A Summary ===\n";
    std::cout << "  Instruments : " << project.instrumentCount() << "\n";
    std::cout << "  Regions     : " << project.totalRegions      << "\n";
    std::cout << "  Warnings    : " << totalWarnings             << "\n";
    std::cout << "  Errors      : " << totalErrors               << "\n";

    if (totalErrors > 0)
    {
        std::cerr << "\nPhase 2A FAILED — aborting.\n";
        return 1;
    }
    std::cout << "\nPhase 2A COMPLETE.\n";

    // =========================================================================
    // Phase 2B: Asset Processing
    // =========================================================================

    std::cout << "\n--- Phase 2B: Asset Processing ---\n\n";

    Frontend::DiagnosticCollector assetDiag("AssetProcessor");
    AssetProcessing::AssetProcessor assetProcessor;
    Processed::ProcessedProject processed = assetProcessor.process(project, assetDiag);

    for (const auto& d : assetDiag.diagnostics())
        std::cout << d.format() << "\n";

    totalWarnings += assetDiag.warningCount();
    totalErrors   += assetDiag.errorCount();

    if (!processed.isValid())
    {
        std::cerr << "\nPhase 2B FAILED — aborting.\n";
        return 1;
    }

    printProcessedProject(processed);
    std::cout << "\nPhase 2B COMPLETE.\n";

    if (noCompile)
    {
        std::cout << "\n[--no-compile] Skipping Phase 2C.\n";
        return 0;
    }

    // =========================================================================
    // Phase 2C: Binary Serialization
    // =========================================================================

    std::cout << "\n--- Phase 2C: Binary Compilation ---\n\n";
    std::cout << "Output: " << outputPath << "\n";

    Backend::Serializer serializer;
    auto result = serializer.compile(processed, outputPath, buildTag);

    if (!result.success)
    {
        std::cerr << "\n[ERROR] " << result.errorMessage << "\n";
        std::cerr << "Phase 2C FAILED.\n";
        return 1;
    }

    // Print Phase 2C results
    std::cout << "\n=== Phase 2C Results ===\n";
    std::cout << "  Output file  : " << result.outputPath   << "\n";
    std::cout << "  File size    : " << result.fileSize      << " bytes"
              << " (" << result.fileSize / 1024 << " KB)\n";
    std::cout << "  Regions      : " << result.regionsWritten  << "\n";
    std::cout << "  Samples      : " << result.samplesWritten  << "\n";
    std::cout << "  PCM written  : " << result.pcmBytesWritten / 1024 << " KB\n";
    std::cout << "  CRC-32       : 0x" << std::hex << std::uppercase
              << result.crc32 << std::dec << "\n";

    std::cout << "\n=== Build Summary ===\n";
    std::cout << "  Total warnings : " << totalWarnings + assetDiag.warningCount() << "\n";
    std::cout << "  Total errors   : " << totalErrors                              << "\n";

    std::cout << "\nPhase 2C COMPLETE.\n";
    std::cout << "VHL file is ready for Phase 2D inspection or ESP32 deployment.\n";
    std::cout << "  → " << result.outputPath << "\n\n";

    return 0;
}
