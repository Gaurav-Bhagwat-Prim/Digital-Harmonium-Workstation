// =============================================================================
// ValidateCommand.cpp — VHL Tools: Logical Integrity & Quality Score Validator
// =============================================================================

#include "ValidateCommand.h"
#include "../Core/VhlLoader.h"
#include "../Core/VhlFormatter.h"

#include <iostream>
#include <iomanip>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace Vhl::Tools::Core;

namespace Vhl {
namespace Tools {
namespace Commands {

struct DiagnosticMsg
{
    enum Level { Info, Warning, Error };
    Level       level;
    std::string category;
    std::string message;
};

int ValidateCommand::execute(const std::vector<std::string>& args)
{
    if (args.empty())
    {
        std::cerr << "Usage: vhl_tools validate <file.vhl> [--strict]\n";
        return 1;
    }

    std::string filePath;
    bool strictMode = false;

    for (const auto& arg : args)
    {
        if (arg == "--strict") { strictMode = true; }
        else if (arg[0] != '-') { filePath = arg; }
    }

    if (filePath.empty())
    {
        std::cerr << "[ERROR] No VHL file specified for validate\n";
        return 1;
    }

    VhlDocument doc;
    if (!VhlLoader::loadFromFile(filePath, doc))
    {
        std::cerr << "[FATAL] Failed to open/load VHL file: " << filePath << "\n";
        return 1;
    }

    std::vector<DiagnosticMsg> diagnostics;

    auto addDiag = [&](DiagnosticMsg::Level level, const std::string& cat, const std::string& msg)
    {
        diagnostics.push_back({ level, cat, msg });
    };

    const auto& h = doc.header;
    size_t fileSize = doc.rawBuffer.size();

    // 1. Magic check
    if (std::memcmp(h.magic, "VHL1", 4) != 0)
    {
        addDiag(DiagnosticMsg::Error, "Header", "Invalid magic bytes (expected 'VHL1')");
    }
    else
    {
        addDiag(DiagnosticMsg::Info, "Header", "Magic bytes 'VHL1' verified");
    }

    // 2. File size field match
    if (h.total_file_size != fileSize)
    {
        addDiag(DiagnosticMsg::Error, "Header", "total_file_size (" + std::to_string(h.total_file_size) +
                ") does not match binary buffer size (" + std::to_string(fileSize) + ")");
    }

    // 3. CRC-32 Check
    if (!doc.crcValid)
    {
        addDiag(DiagnosticMsg::Error, "CRC", "CRC-32 checksum mismatch (Header=0x" +
                [&]() { std::stringstream ss; ss << std::hex << std::uppercase << h.crc32; return ss.str(); }() +
                ", Computed=0x" +
                [&]() { std::stringstream ss; ss << std::hex << std::uppercase << doc.computedCrc32; return ss.str(); }());
    }
    else
    {
        addDiag(DiagnosticMsg::Info, "CRC", "CRC-32 checksum valid");
    }

    // 4. Section offset bounds & order checks
    if (h.offset_instrument_table >= fileSize) addDiag(DiagnosticMsg::Error, "Layout", "Instrument table offset out of file bounds");
    if (h.offset_region_table >= fileSize)     addDiag(DiagnosticMsg::Error, "Layout", "Region table offset out of file bounds");
    if (h.offset_sample_directory >= fileSize) addDiag(DiagnosticMsg::Error, "Layout", "Sample directory offset out of file bounds");
    if (h.offset_lookup_table >= fileSize)     addDiag(DiagnosticMsg::Error, "Layout", "Lookup table offset out of file bounds");
    if (h.offset_pcm_data >= fileSize)          addDiag(DiagnosticMsg::Error, "Layout", "PCM data offset out of file bounds");

    // Overlap checks
    if (h.offset_instrument_table < 128)
        addDiag(DiagnosticMsg::Error, "Layout", "Instrument table overlaps file header");
    if (h.offset_region_table < h.offset_instrument_table + h.num_instruments * sizeof(VHLInstrumentEntry))
        addDiag(DiagnosticMsg::Error, "Layout", "Region table overlaps instrument table");
    if (h.offset_sample_directory < h.offset_region_table + h.total_region_count * sizeof(VHLRegion))
        addDiag(DiagnosticMsg::Error, "Layout", "Sample directory overlaps region table");
    if (h.offset_lookup_table < h.offset_sample_directory + h.total_sample_count * sizeof(VHLSampleEntry))
        addDiag(DiagnosticMsg::Error, "Layout", "Lookup tables overlap sample directory");
    if (h.offset_pcm_data < h.offset_lookup_table + h.num_instruments * (sizeof(VHLLookupHeader) + 128 * sizeof(VHLLookupEntry)))
        addDiag(DiagnosticMsg::Error, "Layout", "PCM data section overlaps lookup tables");

    // 5. Instrument Table Checks
    std::vector<uint8_t> seenInstIds;
    for (size_t i = 0; i < doc.instruments.size(); ++i)
    {
        const auto& inst = doc.instruments[i];
        if (std::find(seenInstIds.begin(), seenInstIds.end(), inst.instrument_id) != seenInstIds.end())
        {
            addDiag(DiagnosticMsg::Warning, "Instrument", "Duplicate instrument ID " + std::to_string(inst.instrument_id));
        }
        seenInstIds.push_back(inst.instrument_id);

        if (inst.region_offset + inst.region_count > doc.regions.size())
        {
            addDiag(DiagnosticMsg::Error, "Instrument", "Instrument [" + std::to_string(i) + "] region slice exceeds total region count");
        }
        if (inst.lookup_table_offset >= fileSize)
        {
            addDiag(DiagnosticMsg::Error, "Instrument", "Instrument [" + std::to_string(i) + "] lookup table offset out of bounds");
        }
    }

    // 6. Region Table Checks
    for (size_t i = 0; i < doc.regions.size(); ++i)
    {
        const auto& r = doc.regions[i];
        if (r.sample_id >= doc.samples.size())
        {
            addDiag(DiagnosticMsg::Error, "Region", "Region [" + std::to_string(i) + "] references invalid sample_id " + std::to_string(r.sample_id));
        }
        if (r.key_lo > r.key_hi)
        {
            addDiag(DiagnosticMsg::Error, "Region", "Region [" + std::to_string(i) + "] key_lo > key_hi (" + std::to_string(r.key_lo) + " > " + std::to_string(r.key_hi) + ")");
        }
        if (r.vel_lo > r.vel_hi)
        {
            addDiag(DiagnosticMsg::Error, "Region", "Region [" + std::to_string(i) + "] vel_lo > vel_hi (" + std::to_string(r.vel_lo) + " > " + std::to_string(r.vel_hi) + ")");
        }
        if (r.vol_env.release_ms > 30000)
        {
            addDiag(DiagnosticMsg::Warning, "Region", "Region [" + std::to_string(i) + "] unusually long release time (" + std::to_string(r.vol_env.release_ms) + " ms)");
        }
        if (r.filter.cutoff_hz > 22050)
        {
            addDiag(DiagnosticMsg::Warning, "Region", "Region [" + std::to_string(i) + "] filter cutoff exceeds Nyquist rate (" + std::to_string(r.filter.cutoff_hz) + " Hz)");
        }
    }

    // 7. Sample Directory Checks
    for (size_t i = 0; i < doc.samples.size(); ++i)
    {
        const auto& s = doc.samples[i];
        size_t absPcmStart = h.offset_pcm_data + s.data_offset;
        size_t samplePcmBytes = s.total_frames * s.channels * 2;

        if (s.total_frames == 0)
        {
            addDiag(DiagnosticMsg::Warning, "Sample", "Sample [" + std::to_string(i) + "] total_frames is 0");
        }

        if (absPcmStart + samplePcmBytes > fileSize)
        {
            addDiag(DiagnosticMsg::Error, "Sample", "Sample [" + std::to_string(i) + "] PCM data extends past file boundary");
        }

        if (absPcmStart % 4 != 0)
        {
            addDiag(DiagnosticMsg::Warning, "Sample", "Sample [" + std::to_string(i) + "] PCM start offset 0x" +
                    [&]() { std::stringstream ss; ss << std::hex << absPcmStart; return ss.str(); }() + " is not 4-byte aligned");
        }

        if (s.flags & 0x01) // Loop valid
        {
            if (s.loop_start_frame >= s.loop_end_frame)
            {
                addDiag(DiagnosticMsg::Error, "Sample", "Sample [" + std::to_string(i) + "] loop_start >= loop_end (" + std::to_string(s.loop_start_frame) + " >= " + std::to_string(s.loop_end_frame) + ")");
            }
            if (s.loop_end_frame > s.total_frames)
            {
                addDiag(DiagnosticMsg::Error, "Sample", "Sample [" + std::to_string(i) + "] loop_end exceeds total_frames (" + std::to_string(s.loop_end_frame) + " > " + std::to_string(s.total_frames) + ")");
            }
        }
    }

    // Tally Diagnostics
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;

    std::cout << "=============================================================\n";
    std::cout << "VHL VALIDATOR REPORT: " << doc.filePath << "\n";
    std::cout << "=============================================================\n\n";

    for (const auto& d : diagnostics)
    {
        if (d.level == DiagnosticMsg::Error)
        {
            errorCount++;
            std::cout << "[ERROR]   (" << d.category << ") " << d.message << "\n";
        }
        else if (d.level == DiagnosticMsg::Warning)
        {
            warningCount++;
            std::cout << "[WARNING] (" << d.category << ") " << d.message << "\n";
        }
        else
        {
            infoCount++;
            if (strictMode)
                std::cout << "[INFO]    (" << d.category << ") " << d.message << "\n";
        }
    }
    std::cout << "\n";

    // Calculate Quality Score %
    // Quality = 100% - (Errors * 25%) - (Warnings * 5%)
    float quality = 100.0f - (errorCount * 25.0f) - (warningCount * 5.0f);
    quality = std::clamp(quality, 0.0f, 100.0f);

    bool pass = (errorCount == 0);
    if (strictMode && warningCount > 0)
        pass = false;

    std::cout << "Validation Summary\n";
    std::cout << "------------------------------\n";
    std::cout << "Errors    : " << errorCount << "\n";
    std::cout << "Warnings  : " << warningCount << "\n";
    std::cout << "Quality   : " << std::fixed << std::setprecision(0) << quality << "%\n";
    std::cout << "Status    : " << (pass ? "PASS" : "FAIL") << "\n";
    std::cout << "==============================\n\n";

    return pass ? 0 : 1;
}

} // namespace Commands
} // namespace Tools
} // namespace Vhl
