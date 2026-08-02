// =============================================================================
// InspectCommand.cpp — VHL Tools: Rich Instrument Inspector Implementation
// =============================================================================

#include "InspectCommand.h"
#include "../Core/VhlLoader.h"
#include "../Core/VhlFormatter.h"

#include <iostream>
#include <iomanip>
#include <algorithm>

using namespace Vhl::Tools::Core;

namespace Vhl {
namespace Tools {
namespace Commands {

int InspectCommand::execute(const std::vector<std::string>& args)
{
    if (args.empty())
    {
        std::cerr << "Usage: vhl_tools inspect <file.vhl> [--all] [--regions] [--samples]\n";
        return 1;
    }

    std::string filePath;
    bool showRegions = false;
    bool showSamples = false;

    for (const auto& arg : args)
    {
        if (arg == "--all")           { showRegions = true; showSamples = true; }
        else if (arg == "--regions")  { showRegions = true; }
        else if (arg == "--samples")  { showSamples = true; }
        else if (arg[0] != '-')       { filePath = arg; }
    }

    if (filePath.empty())
    {
        std::cerr << "[ERROR] No VHL file specified for inspect\n";
        return 1;
    }

    VhlDocument doc;
    if (!VhlLoader::loadFromFile(filePath, doc))
    {
        std::cerr << "[FATAL] Failed to open/load VHL file: " << filePath << "\n";
        return 1;
    }

    std::cout << "=============================================================\n";
    std::cout << "VHL INSPECTOR: " << doc.filePath << "\n";
    std::cout << "=============================================================\n\n";

    // 1. Header & Build Metadata
    const auto& h = doc.header;
    std::string magicStr(reinterpret_cast<const char*>(h.magic), 4);

    std::cout << "--- Header & Build Metadata ---\n";
    std::cout << "  Magic Magic           : " << magicStr << " (v" << (int)h.version_major << "." << (int)h.version_minor << ")\n";
    std::cout << "  Build Tag             : " << h.build_tag << "\n";
    std::cout << "  Builder Version       : " << (h.builder_version >> 8) << "." << (h.builder_version & 0xFF) << "\n";
    std::cout << "  Audio Target Rate     : " << h.target_sample_rate << " Hz (DMA Block: " << h.dma_block_size << " samples)\n";
    std::cout << "  CRC-32 Checksum       : 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << h.crc32 << std::dec;
    std::cout << " (" << (doc.crcValid ? "VALID" : "INVALID") << ")\n";
    std::cout << "  Total File Size       : " << VhlFormatter::formatBytes(doc.rawBuffer.size()) << " (" << doc.rawBuffer.size() << " bytes)\n\n";

    // 2. Section Memory Footprint Breakdown
    size_t totalBytes = doc.rawBuffer.size();
    if (totalBytes > 0)
    {
        size_t headerBytes = sizeof(VHLFileHeader);
        size_t instBytes   = doc.instruments.size() * sizeof(VHLInstrumentEntry);
        size_t regBytes    = doc.regions.size() * sizeof(VHLRegion);
        size_t sampBytes   = doc.samples.size() * sizeof(VHLSampleEntry);
        size_t lkBytes     = doc.instruments.size() * (sizeof(VHLLookupHeader) + 128 * sizeof(VHLLookupEntry));
        size_t pcmBytes    = doc.stats.totalPcmBytes;

        std::cout << "--- Memory Footprint Distribution ---\n";
        std::cout << "  Header           (" << std::setw(8) << VhlFormatter::formatBytes(headerBytes) << ") : "
                  << VhlFormatter::formatBar((float)headerBytes * 100.0f / totalBytes) << "\n";
        std::cout << "  Instrument Table (" << std::setw(8) << VhlFormatter::formatBytes(instBytes) << ") : "
                  << VhlFormatter::formatBar((float)instBytes * 100.0f / totalBytes) << "\n";
        std::cout << "  Region Table     (" << std::setw(8) << VhlFormatter::formatBytes(regBytes) << ") : "
                  << VhlFormatter::formatBar((float)regBytes * 100.0f / totalBytes) << "\n";
        std::cout << "  Sample Directory (" << std::setw(8) << VhlFormatter::formatBytes(sampBytes) << ") : "
                  << VhlFormatter::formatBar((float)sampBytes * 100.0f / totalBytes) << "\n";
        std::cout << "  Lookup Tables    (" << std::setw(8) << VhlFormatter::formatBytes(lkBytes) << ") : "
                  << VhlFormatter::formatBar((float)lkBytes * 100.0f / totalBytes) << "\n";
        std::cout << "  PCM Sample Data  (" << std::setw(8) << VhlFormatter::formatBytes(pcmBytes) << ") : "
                  << VhlFormatter::formatBar((float)pcmBytes * 100.0f / totalBytes) << "\n\n";
    }

    // 3. Computed Instrument Statistics
    const auto& st = doc.stats;
    std::cout << "--- Computed Instrument Statistics ---\n";
    std::cout << "  Instruments Count     : " << doc.instruments.size() << "\n";
    std::cout << "  Total Regions         : " << doc.regions.size() << "\n";
    std::cout << "  Total Unique Samples  : " << doc.samples.size() << "\n";
    std::cout << "  Total PCM Memory      : " << VhlFormatter::formatBytes(st.totalPcmBytes) << "\n";
    std::cout << "  Total PCM Audio Length: " << std::fixed << std::setprecision(2) << st.totalPcmDurationSec << " seconds\n";
    std::cout << "  Average Sample Length : " << std::fixed << std::setprecision(2) << st.avgSampleDurationSec << " seconds\n";
    std::cout << "  Looped vs One-Shot    : " << st.loopedSampleCount << " looped, " << st.oneShotSampleCount << " one-shot\n";
    std::cout << "  Average Vol Attack    : " << std::fixed << std::setprecision(1) << st.avgVolAttackMs << " ms\n";
    std::cout << "  Average Vol Release   : " << std::fixed << std::setprecision(1) << st.avgVolReleaseMs << " ms\n";
    std::cout << "  Average Filter Cutoff : " << std::fixed << std::setprecision(0) << st.avgFilterCutoffHz << " Hz\n";
    std::cout << "  Key Range Covered     : " << VhlFormatter::noteName(st.minKeyCovered) << " (" << (int)st.minKeyCovered
              << ") to " << VhlFormatter::noteName(st.maxKeyCovered) << " (" << (int)st.maxKeyCovered << ")\n";
    std::cout << "  Active MIDI Keys      : " << st.activeKeysCount << " / 128\n\n";

    // 4. Key Coverage Map
    std::cout << "--- MIDI Key Coverage Map ---\n";
    std::cout << VhlFormatter::formatKeyCoverageMap(doc) << "\n\n";

    // 5. Largest Samples Ranking
    std::cout << "--- Top Largest Samples ---\n";
    for (size_t i = 0; i < st.largestSamples.size(); ++i)
    {
        const auto& ls = st.largestSamples[i];
        std::cout << "  " << (i + 1) << ". Sample [" << ls.sampleId << "] (" << std::setw(8) << VhlFormatter::formatBytes(ls.pcmSizeBytes)
                  << ") : " << ls.totalFrames << " frames (" << std::fixed << std::setprecision(2) << ls.durationSec << "s)"
                  << (ls.isLooped ? " [LOOPED]" : "") << "\n";
    }
    std::cout << "\n";

    // 6. Instrument Details
    std::cout << "--- Instrument Directory ---\n";
    for (size_t i = 0; i < doc.instruments.size(); ++i)
    {
        const auto& inst = doc.instruments[i];
        std::cout << "  Inst [" << (int)inst.instrument_id << "] \"" << inst.name << "\" | MIDI " << (int)inst.bank << ":" << (int)inst.program
                  << " | Regions [" << inst.region_offset << ".." << (inst.region_offset + inst.region_count - 1) << "] (" << inst.region_count << " regions)\n";
    }
    std::cout << "\n";

    // 7. Optional Region Detail Table
    if (showRegions)
    {
        std::cout << "--- Region Details (" << doc.regions.size() << ") ---\n";
        for (size_t i = 0; i < doc.regions.size(); ++i)
        {
            const auto& r = doc.regions[i];
            std::cout << "  Region [" << i << "] keys[" << (int)r.key_lo << ".." << (int)r.key_hi << "] vel[" << (int)r.vel_lo << ".." << (int)r.vel_hi << "]"
                      << " sample=" << r.sample_id << " atten=" << r.attenuation_cb << "cb pan=" << r.pan
                      << " VolEnv[A=" << r.vol_env.attack_ms << "ms D=" << r.vol_env.decay_ms << "ms S=" << r.vol_env.sustain_level << "cb R=" << r.vol_env.release_ms << "ms]"
                      << " Flt[" << r.filter.cutoff_hz << "Hz " << r.filter.resonance_cb << "cb]\n";
        }
        std::cout << "\n";
    }

    // 8. Optional Sample Detail Table
    if (showSamples)
    {
        std::cout << "--- Sample Directory Details (" << doc.samples.size() << ") ---\n";
        for (size_t i = 0; i < doc.samples.size(); ++i)
        {
            const auto& s = doc.samples[i];
            std::cout << "  Sample [" << i << "] " << s.sample_rate << "Hz " << (int)s.bit_depth << "bit " << (s.channels == 1 ? "mono" : "stereo")
                      << " " << s.total_frames << " frames (" << VhlFormatter::formatBytes(s.total_frames * s.channels * 2) << ")";
            if (s.flags & 0x01)
                std::cout << " loop[" << s.loop_start_frame << ".." << s.loop_end_frame << "]";
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Inspection complete.\n\n";
    return 0;
}

} // namespace Commands
} // namespace Tools
} // namespace Vhl
