// =============================================================================
// DiffCommand.cpp — VHL Tools: Structure-Level Binary Diff Implementation
// =============================================================================

#include "DiffCommand.h"
#include "../Core/VhlLoader.h"
#include "../Core/VhlFormatter.h"

#include <iostream>
#include <iomanip>
#include <set>
#include <cmath>
#include <cstring>

using namespace Vhl::Tools::Core;

namespace Vhl {
namespace Tools {
namespace Commands {

int DiffCommand::execute(const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        std::cerr << "Usage: vhl_tools diff <old.vhl> <new.vhl> [--detailed]\n";
        return 1;
    }

    std::string fileA;
    std::string fileB;
    bool detailedMode = false;

    for (const auto& arg : args)
    {
        if (arg == "--detailed" || arg == "-v") { detailedMode = true; }
        else if (fileA.empty() && arg[0] != '-') { fileA = arg; }
        else if (fileB.empty() && arg[0] != '-') { fileB = arg; }
    }

    if (fileA.empty() || fileB.empty())
    {
        std::cerr << "[ERROR] Please specify two VHL files to compare\n";
        return 1;
    }

    VhlDocument docA, docB;
    if (!VhlLoader::loadFromFile(fileA, docA))
    {
        std::cerr << "[FATAL] Could not load file A: " << fileA << "\n";
        return 1;
    }
    if (!VhlLoader::loadFromFile(fileB, docB))
    {
        std::cerr << "[FATAL] Could not load file B: " << fileB << "\n";
        return 1;
    }

    std::cout << "=============================================================\n";
    std::cout << "VHL BINARY DIFF: " << fileA << "  vs  " << fileB << "\n";
    std::cout << "=============================================================\n\n";

    // Track modified parameters
    std::set<std::string> modifiedParams;
    int changedRegionsCount = 0;
    int changedSamplesCount = 0;

    // 1. Compare Regions
    size_t minRegions = std::min(docA.regions.size(), docB.regions.size());
    size_t maxRegions = std::max(docA.regions.size(), docB.regions.size());

    if (docA.regions.size() != docB.regions.size())
    {
        modifiedParams.insert("Region Count");
        changedRegionsCount += static_cast<int>(maxRegions - minRegions);
    }

    for (size_t i = 0; i < minRegions; ++i)
    {
        const auto& rA = docA.regions[i];
        const auto& rB = docB.regions[i];
        bool regionChanged = false;

        if (rA.key_lo != rB.key_lo || rA.key_hi != rB.key_hi)
        {
            modifiedParams.insert("Key Range");
            regionChanged = true;
        }
        if (rA.vel_lo != rB.vel_lo || rA.vel_hi != rB.vel_hi)
        {
            modifiedParams.insert("Velocity Range");
            regionChanged = true;
        }
        if (rA.sample_id != rB.sample_id)
        {
            modifiedParams.insert("Sample Assignment");
            regionChanged = true;
        }
        if (rA.coarse_tune != rB.coarse_tune || rA.fine_tune != rB.fine_tune)
        {
            modifiedParams.insert("Tuning");
            regionChanged = true;
        }
        if (rA.attenuation_cb != rB.attenuation_cb || rA.pan != rB.pan)
        {
            modifiedParams.insert("Volume & Pan");
            regionChanged = true;
        }
        if (std::memcmp(&rA.vol_env, &rB.vol_env, sizeof(VHLEnvelope)) != 0)
        {
            modifiedParams.insert("Volume Envelope");
            regionChanged = true;
        }
        if (std::memcmp(&rA.mod_env, &rB.mod_env, sizeof(VHLEnvelope)) != 0 ||
            rA.mod_env_to_pitch != rB.mod_env_to_pitch || rA.mod_env_to_filter != rB.mod_env_to_filter)
        {
            modifiedParams.insert("Modulation Envelope");
            regionChanged = true;
        }
        if (std::memcmp(&rA.filter, &rB.filter, sizeof(VHLFilter)) != 0)
        {
            modifiedParams.insert("Filter Cutoff / Resonance");
            regionChanged = true;
        }
        if (std::memcmp(&rA.lfo_mod, &rB.lfo_mod, sizeof(VHLModLFO)) != 0 ||
            std::memcmp(&rA.lfo_vib, &rB.lfo_vib, sizeof(VHLVibLFO)) != 0)
        {
            modifiedParams.insert("LFO Settings");
            regionChanged = true;
        }

        if (regionChanged)
            changedRegionsCount++;
    }

    // 2. Compare Samples
    size_t minSamples = std::min(docA.samples.size(), docB.samples.size());
    size_t maxSamples = std::max(docA.samples.size(), docB.samples.size());

    if (docA.samples.size() != docB.samples.size())
    {
        modifiedParams.insert("Sample Count");
        changedSamplesCount += static_cast<int>(maxSamples - minSamples);
    }

    for (size_t i = 0; i < minSamples; ++i)
    {
        const auto& sA = docA.samples[i];
        const auto& sB = docB.samples[i];
        bool sampleChanged = false;

        if (sA.total_frames != sB.total_frames)
        {
            modifiedParams.insert("Sample Length");
            sampleChanged = true;
        }
        if (sA.sample_rate != sB.sample_rate)
        {
            modifiedParams.insert("Sample Rate");
            sampleChanged = true;
        }
        if (sA.loop_start_frame != sB.loop_start_frame || sA.loop_end_frame != sB.loop_end_frame)
        {
            modifiedParams.insert("Loop Points");
            sampleChanged = true;
        }

        if (sampleChanged)
            changedSamplesCount++;
    }

    // Calculate PCM Delta
    long long pcmDeltaBytes = (long long)docB.stats.totalPcmBytes - (long long)docA.stats.totalPcmBytes;

    // --- Output Section ---

    std::cout << "--- High-Level Diff Summary ---\n";
    std::cout << "  File A Size       : " << VhlFormatter::formatBytes(docA.rawBuffer.size()) << "\n";
    std::cout << "  File B Size       : " << VhlFormatter::formatBytes(docB.rawBuffer.size()) << "\n";
    std::cout << "  Changed Regions   : " << changedRegionsCount << "\n";
    std::cout << "  Changed Samples   : " << changedSamplesCount << "\n";
    std::cout << "  PCM Delta         : " << (pcmDeltaBytes >= 0 ? "+" : "-") << VhlFormatter::formatBytes(std::abs(pcmDeltaBytes)) << "\n\n";

    if (!modifiedParams.empty())
    {
        std::cout << "Modified Parameter Categories:\n";
        for (const auto& param : modifiedParams)
        {
            std::cout << "  • " << param << "\n";
        }
        std::cout << "\n";
    }
    else
    {
        std::cout << "No structural parameter differences detected.\n\n";
    }

    // Detailed Mode
    if (detailedMode && (!modifiedParams.empty() || docA.rawBuffer.size() != docB.rawBuffer.size()))
    {
        std::cout << "--- Detailed Parameter Differences ---\n";

        if (docA.header.crc32 != docB.header.crc32)
        {
            std::cout << "  Header CRC-32     : 0x" << std::hex << docA.header.crc32
                      << " -> 0x" << docB.header.crc32 << std::dec << "\n";
        }

        for (size_t i = 0; i < minRegions; ++i)
        {
            const auto& rA = docA.regions[i];
            const auto& rB = docB.regions[i];

            if (rA.vol_env.attack_ms != rB.vol_env.attack_ms)
                std::cout << "  Region [" << i << "] Attack      : " << rA.vol_env.attack_ms << " ms -> " << rB.vol_env.attack_ms << " ms\n";

            if (rA.vol_env.release_ms != rB.vol_env.release_ms)
                std::cout << "  Region [" << i << "] Release     : " << rA.vol_env.release_ms << " ms -> " << rB.vol_env.release_ms << " ms\n";

            if (rA.filter.cutoff_hz != rB.filter.cutoff_hz)
                std::cout << "  Region [" << i << "] Cutoff      : " << rA.filter.cutoff_hz << " Hz -> " << rB.filter.cutoff_hz << " Hz\n";

            if (rA.attenuation_cb != rB.attenuation_cb)
                std::cout << "  Region [" << i << "] Attenuation : " << rA.attenuation_cb << " cb -> " << rB.attenuation_cb << " cb\n";
        }
        std::cout << "\n";
    }

    std::cout << "Diff completed.\n\n";
    return (modifiedParams.empty() && docA.rawBuffer.size() == docB.rawBuffer.size()) ? 0 : 1;
}

} // namespace Commands
} // namespace Tools
} // namespace Vhl
