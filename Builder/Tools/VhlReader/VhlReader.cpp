// =============================================================================
// VhlReader.cpp — VHL Reader: Pre-Phase 2D Verification Tool Implementation
//
// Reads and validates .vhl binary files directly using packed structs defined
// in vhl_structs.h. Includes full CRC-32 checking and structural validation.
// =============================================================================

#include "VhlReader.h"
#include "../../Backend/CRC32.h"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace Vhl {
namespace Tools {

// =============================================================================
// load — Read entire binary file into _buffer using std::ifstream
// =============================================================================

bool VhlReader::load(const std::string& filePath)
{
    _filePath = filePath;
    _buffer.clear();
    _loaded = false;

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "[ERROR] Cannot open VHL file: " << filePath << "\n";
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < static_cast<std::streamsize>(sizeof(VHLFileHeader)))
    {
        std::cerr << "[ERROR] File size (" << fileSize << " bytes) is smaller than VHLFileHeader (128 bytes)\n";
        return false;
    }

    file.seekg(0, std::ios::beg);
    _buffer.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(_buffer.data()), fileSize))
    {
        std::cerr << "[ERROR] Failed to read full contents of: " << filePath << "\n";
        _buffer.clear();
        return false;
    }

    _loaded = true;
    return true;
}

// =============================================================================
// header — Cast offset 0 of _buffer to const VHLFileHeader*
// =============================================================================

const VHLFileHeader* VhlReader::header() const
{
    if (!_loaded || _buffer.size() < sizeof(VHLFileHeader))
        return nullptr;
    return reinterpret_cast<const VHLFileHeader*>(_buffer.data());
}

// =============================================================================
// printAll — Render complete file report matching requested section styling
// =============================================================================

void VhlReader::printAll() const
{
    if (!_loaded)
    {
        std::cout << "No VHL file loaded.\n";
        return;
    }

    std::cout << "==============================\n";
    std::cout << "VHL FILE: " << _filePath << "\n";
    std::cout << "==============================\n\n";

    printHeader();
    printInstrumentTable();
    printRegionTable();
    printSampleDirectory();
    printLookupTables();
    printVerificationSummary();
}

// =============================================================================
// printHeader — Display VHLFileHeader
// =============================================================================

void VhlReader::printHeader() const
{
    const auto* h = header();
    if (!h) return;

    std::string magicStr(reinterpret_cast<const char*>(h->magic), 4);

    std::cout << "Header\n";
    std::cout << "------------------------------\n";
    std::cout << "Magic                   : " << magicStr << "\n";
    std::cout << "Version                 : " << (int)h->version_major << "." << (int)h->version_minor << "\n";
    std::cout << "Flags                   : 0x" << std::hex << std::setfill('0') << std::setw(4) << h->flags << std::dec << "\n";
    std::cout << "Total File Size         : " << h->total_file_size << " bytes (" << (h->total_file_size / 1024) << " KB)\n";
    std::cout << "CRC-32                  : 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << h->crc32 << std::dec << "\n";
    std::cout << "Build Timestamp         : " << h->build_timestamp << "\n";
    std::cout << "Builder Version         : " << (h->builder_version >> 8) << "." << (h->builder_version & 0xFF) << "\n";
    std::cout << "Build Tag               : " << h->build_tag << "\n";
    std::cout << "Audio Target Rate       : " << h->target_sample_rate << " Hz\n";
    std::cout << "DMA Block Size          : " << h->dma_block_size << " samples\n";
    std::cout << "Instruments Count       : " << h->num_instruments << "\n";
    std::cout << "Total Regions Count     : " << h->total_region_count << "\n";
    std::cout << "Total Samples Count     : " << h->total_sample_count << "\n\n";

    std::cout << "Section Offsets & Sizes:\n";
    std::cout << "  [0] Header            : offset=0x0 (0), size=128 bytes\n";
    std::cout << "  [1] Instrument Table  : offset=0x" << std::hex << h->offset_instrument_table << " (" << std::dec << h->offset_instrument_table << ")\n";
    std::cout << "  [2] Region Table      : offset=0x" << std::hex << h->offset_region_table << " (" << std::dec << h->offset_region_table << ")\n";
    std::cout << "  [3] Sample Directory  : offset=0x" << std::hex << h->offset_sample_directory << " (" << std::dec << h->offset_sample_directory << ")\n";
    std::cout << "  [4] Lookup Tables     : offset=0x" << std::hex << h->offset_lookup_table << " (" << std::dec << h->offset_lookup_table << ")\n";
    std::cout << "  [5] PCM Data          : offset=0x" << std::hex << h->offset_pcm_data << " (" << std::dec << h->offset_pcm_data << ")\n\n";
}

// =============================================================================
// printInstrumentTable — Display VHLInstrumentEntry records
// =============================================================================

void VhlReader::printInstrumentTable() const
{
    const auto* h = header();
    if (!h) return;

    std::cout << "Instrument Table\n";
    std::cout << "------------------------------\n";

    for (uint16_t i = 0; i < h->num_instruments; ++i)
    {
        size_t entryOffset = h->offset_instrument_table + (i * sizeof(VHLInstrumentEntry));
        const auto* inst = getStructAt<VHLInstrumentEntry>(entryOffset);
        if (!inst)
        {
            std::cout << "  [ERROR] Instrument entry " << i << " at offset " << entryOffset << " extends past end of file!\n";
            break;
        }

        std::cout << "  Instrument [" << (int)inst->instrument_id << "]: \"" << inst->name << "\"\n";
        std::cout << "    MIDI Bank:Program   : " << (int)inst->bank << ":" << (int)inst->program << "\n";
        std::cout << "    Regions             : offset=" << inst->region_offset << ", count=" << inst->region_count << "\n";
        std::cout << "    Lookup Table Offset : 0x" << std::hex << inst->lookup_table_offset << std::dec << "\n";
    }
    std::cout << "\n";
}

// =============================================================================
// printRegionTable — Display VHLRegion records
// =============================================================================

void VhlReader::printRegionTable() const
{
    const auto* h = header();
    if (!h) return;

    std::cout << "Region Table (" << h->total_region_count << " total regions)\n";
    std::cout << "------------------------------\n";

    for (uint16_t i = 0; i < h->total_region_count; ++i)
    {
        size_t regionOffset = h->offset_region_table + (i * sizeof(VHLRegion));
        const auto* r = getStructAt<VHLRegion>(regionOffset);
        if (!r)
        {
            std::cout << "  [ERROR] Region " << i << " extends past end of file!\n";
            break;
        }

        std::cout << "  Region [" << i << "]:\n";
        std::cout << "    Key Range           : [" << (int)r->key_lo << ".." << (int)r->key_hi << "]\n";
        std::cout << "    Vel Range           : [" << (int)r->vel_lo << ".." << (int)r->vel_hi << "]\n";
        std::cout << "    Sample ID           : " << r->sample_id << "\n";
        std::cout << "    Exclusive Class     : " << (int)r->exclusive_class << "\n";
        std::cout << "    Loop Mode           : " << (int)r->loop_mode << "\n";
        std::cout << "    Tuning              : coarse=" << (int)r->coarse_tune << " semitones, fine=" << r->fine_tune << " centsx10, scale=" << (int)r->scale_tuning << "\n";
        std::cout << "    Attenuation / Pan   : attenuation=" << r->attenuation_cb << " cb, pan=" << r->pan << " (send: rev=" << (int)r->reverb_send << "%, cho=" << (int)r->chorus_send << "%)\n";

        // Vol Envelope
        const auto& ve = r->vol_env;
        std::cout << "    Vol Envelope        : A=" << ve.attack_ms << "ms, D=" << ve.decay_ms << "ms, S=" << ve.sustain_level << "cb, R=" << ve.release_ms << "ms (delay=" << ve.delay_ms << "ms, hold=" << ve.hold_ms << "ms)\n";

        // Mod Envelope
        const auto& me = r->mod_env;
        std::cout << "    Mod Envelope        : A=" << me.attack_ms << "ms, D=" << me.decay_ms << "ms, S=" << me.sustain_level << "permille, R=" << me.release_ms << "ms (pitch_depth=" << r->mod_env_to_pitch << ", filter_depth=" << r->mod_env_to_filter << ")\n";

        // LFOs
        std::cout << "    Mod LFO             : delay=" << r->lfo_mod.delay_ms << "ms, freq=" << r->lfo_mod.freq_mHz << "mHz, to_pitch=" << r->lfo_mod.to_pitch_cb << ", to_flt=" << r->lfo_mod.to_filter_cb << ", to_vol=" << r->lfo_mod.to_volume_cb << "\n";
        std::cout << "    Vib LFO             : delay=" << r->lfo_vib.delay_ms << "ms, freq=" << r->lfo_vib.freq_mHz << "mHz, to_pitch=" << r->lfo_vib.to_pitch_cb << "\n";

        // Filter
        std::cout << "    Filter              : cutoff=" << r->filter.cutoff_hz << " Hz, resonance=" << r->filter.resonance_cb << " cb\n";
    }
    std::cout << "\n";
}

// =============================================================================
// printSampleDirectory — Display VHLSampleEntry records
// =============================================================================

void VhlReader::printSampleDirectory() const
{
    const auto* h = header();
    if (!h) return;

    std::cout << "Sample Directory (" << h->total_sample_count << " total samples)\n";
    std::cout << "------------------------------\n";

    for (uint16_t i = 0; i < h->total_sample_count; ++i)
    {
        size_t sampleOffset = h->offset_sample_directory + (i * sizeof(VHLSampleEntry));
        const auto* s = getStructAt<VHLSampleEntry>(sampleOffset);
        if (!s)
        {
            std::cout << "  [ERROR] Sample entry " << i << " extends past end of file!\n";
            break;
        }

        std::cout << "  Sample [" << i << "]:\n";
        std::cout << "    PCM Data Offset     : 0x" << std::hex << s->data_offset << std::dec << " (rel to PCM sec), Abs File Offset=0x" << std::hex << (h->offset_pcm_data + s->data_offset) << std::dec << "\n";
        std::cout << "    Total Frames        : " << s->total_frames << " frames (" << (s->total_frames * s->channels * 2) << " bytes)\n";
        std::cout << "    Loop Points         : start=" << s->loop_start_frame << ", end=" << s->loop_end_frame << "\n";
        std::cout << "    Audio Format        : rate=" << s->sample_rate << " Hz, root_key=" << (int)s->root_key << ", fine_tune=" << (int)s->fine_tune_cents << " cents\n";
        std::cout << "    Channels & Bits     : " << (int)s->channels << " ch, " << (int)s->bit_depth << " bit, flags=0x" << std::hex << (int)s->flags << std::dec << ", linked_id=" << s->linked_sample_id << "\n";
    }
    std::cout << "\n";
}

// =============================================================================
// printLookupTables — Display instrument key lookup tables
// =============================================================================

void VhlReader::printLookupTables() const
{
    const auto* h = header();
    if (!h) return;

    std::cout << "Lookup Tables\n";
    std::cout << "------------------------------\n";

    for (uint16_t i = 0; i < h->num_instruments; ++i)
    {
        size_t instOffset = h->offset_instrument_table + (i * sizeof(VHLInstrumentEntry));
        const auto* inst = getStructAt<VHLInstrumentEntry>(instOffset);
        if (!inst) break;

        size_t lkOffset = inst->lookup_table_offset;
        const auto* lkHeader = getStructAt<VHLLookupHeader>(lkOffset);
        if (!lkHeader)
        {
            std::cout << "  [ERROR] Instrument [" << i << "] Lookup header at offset 0x" << std::hex << lkOffset << std::dec << " extends past EOF!\n";
            continue;
        }

        std::cout << "  Instrument [" << lkHeader->instrument_id << "] Key Lookup Map:\n";
        int activeKeys = 0;
        for (int key = 0; key < 128; ++key)
        {
            size_t entryOffset = lkOffset + sizeof(VHLLookupHeader) + (key * sizeof(VHLLookupEntry));
            const auto* entry = getStructAt<VHLLookupEntry>(entryOffset);
            if (!entry) break;

            if (entry->region_count > 0)
            {
                ++activeKeys;
                std::cout << "    Key " << std::setw(3) << key << " -> Region Start Index " << entry->region_start_idx << " (Count: " << (int)entry->region_count << ")\n";
            }
        }
        std::cout << "    Total Active MIDI Keys Covered: " << activeKeys << "/128\n";
    }
    std::cout << "\n";
}

// =============================================================================
// verifyIntegrity — Perform all structural checks and CRC32 verification
// =============================================================================

bool VhlReader::verifyIntegrity(std::vector<std::string>& warnings, std::vector<std::string>& errors) const
{
    warnings.clear();
    errors.clear();

    if (!_loaded || _buffer.size() < sizeof(VHLFileHeader))
    {
        errors.push_back("File buffer empty or smaller than VHLFileHeader (128 bytes)");
        return false;
    }

    const auto* h = header();

    // 1. Magic check
    if (std::memcmp(h->magic, "VHL1", 4) != 0)
    {
        errors.push_back("Invalid magic bytes (expected 'VHL1')");
    }

    // 2. File size field match
    if (h->total_file_size != _buffer.size())
    {
        errors.push_back("Header total_file_size (" + std::to_string(h->total_file_size) +
                         ") does not match actual binary file size (" + std::to_string(_buffer.size()) + ")");
    }

    // 3. CRC-32 Check
    uint32_t computedCrc = Backend::CRC32::computeWithZeroedField(_buffer.data(), _buffer.size(), 64);
    if (h->crc32 != computedCrc)
    {
        std::stringstream ssH, ssC;
        ssH << "0x" << std::hex << std::uppercase << h->crc32;
        ssC << "0x" << std::hex << std::uppercase << computedCrc;
        errors.push_back("CRC-32 checksum mismatch: Header=" + ssH.str() + ", Computed=" + ssC.str());
    }

    // 4. Section offset bounds & order checks
    size_t fileSize = _buffer.size();
    if (h->offset_instrument_table >= fileSize) errors.push_back("Instrument table offset out of bounds");
    if (h->offset_region_table >= fileSize)     errors.push_back("Region table offset out of bounds");
    if (h->offset_sample_directory >= fileSize) errors.push_back("Sample directory offset out of bounds");
    if (h->offset_lookup_table >= fileSize)     errors.push_back("Lookup table offset out of bounds");
    if (h->offset_pcm_data >= fileSize)          errors.push_back("PCM data offset out of bounds");

    // Check non-overlapping section layout order
    if (h->offset_instrument_table < 128) errors.push_back("Instrument table overlaps header");
    if (h->offset_region_table < h->offset_instrument_table + h->num_instruments * sizeof(VHLInstrumentEntry))
        errors.push_back("Region table overlaps instrument table");
    if (h->offset_sample_directory < h->offset_region_table + h->total_region_count * sizeof(VHLRegion))
        errors.push_back("Sample directory overlaps region table");
    if (h->offset_lookup_table < h->offset_sample_directory + h->total_sample_count * sizeof(VHLSampleEntry))
        errors.push_back("Lookup table section overlaps sample directory");
    if (h->offset_pcm_data < h->offset_lookup_table + h->num_instruments * (sizeof(VHLLookupHeader) + 128 * sizeof(VHLLookupEntry)))
        errors.push_back("PCM data section overlaps lookup table section");

    // 5. Region references
    for (uint16_t i = 0; i < h->total_region_count; ++i)
    {
        size_t regionOffset = h->offset_region_table + (i * sizeof(VHLRegion));
        const auto* r = getStructAt<VHLRegion>(regionOffset);
        if (!r) break;

        if (r->sample_id >= h->total_sample_count)
        {
            errors.push_back("Region [" + std::to_string(i) + "] references invalid sample_id " + std::to_string(r->sample_id));
        }
        if (r->key_lo > r->key_hi)
        {
            errors.push_back("Region [" + std::to_string(i) + "] key_lo > key_hi (" + std::to_string(r->key_lo) + " > " + std::to_string(r->key_hi) + ")");
        }
        if (r->vel_lo > r->vel_hi)
        {
            errors.push_back("Region [" + std::to_string(i) + "] vel_lo > vel_hi (" + std::to_string(r->vel_lo) + " > " + std::to_string(r->vel_hi) + ")");
        }
    }

    // 6. Sample directory & loop point bounds
    for (uint16_t i = 0; i < h->total_sample_count; ++i)
    {
        size_t sampleOffset = h->offset_sample_directory + (i * sizeof(VHLSampleEntry));
        const auto* s = getStructAt<VHLSampleEntry>(sampleOffset);
        if (!s) break;

        size_t absPcmStart = h->offset_pcm_data + s->data_offset;
        size_t samplePcmBytes = s->total_frames * s->channels * 2;
        if (absPcmStart + samplePcmBytes > fileSize)
        {
            errors.push_back("Sample [" + std::to_string(i) + "] PCM data exceeds file boundary");
        }

        if (s->flags & 0x01) // VHL_SAMPLE_FLAG_LOOP_VALID
        {
            if (s->loop_start_frame >= s->loop_end_frame)
            {
                errors.push_back("Sample [" + std::to_string(i) + "] loop_start >= loop_end (" + std::to_string(s->loop_start_frame) + " >= " + std::to_string(s->loop_end_frame) + ")");
            }
            if (s->loop_end_frame > s->total_frames)
            {
                errors.push_back("Sample [" + std::to_string(i) + "] loop_end exceeds total_frames (" + std::to_string(s->loop_end_frame) + " > " + std::to_string(s->total_frames) + ")");
            }
        }
    }

    return errors.empty();
}

// =============================================================================
// printVerificationSummary — Pretty-print check results and PASS / FAIL badge
// =============================================================================

void VhlReader::printVerificationSummary() const
{
    std::cout << "Verification Summary\n";
    std::cout << "------------------------------\n";

    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    bool passed = verifyIntegrity(warnings, errors);

    const auto* h = header();
    if (h)
    {
        std::cout << "Header           : " << (std::memcmp(h->magic, "VHL1", 4) == 0 ? "OK" : "FAILED") << "\n";
        std::cout << "Instrument Table : " << (h->num_instruments > 0 ? "OK" : "FAILED") << "\n";
        std::cout << "Region Table     : " << (h->total_region_count > 0 ? "OK" : "FAILED") << "\n";
        std::cout << "Sample Directory : " << (h->total_sample_count > 0 ? "OK" : "FAILED") << "\n";
        std::cout << "Lookup Tables    : OK\n";
        std::cout << "Offsets          : OK\n";

        uint32_t computedCrc = Backend::CRC32::computeWithZeroedField(_buffer.data(), _buffer.size(), 64);
        std::cout << "CRC-32 Checksum  : " << (h->crc32 == computedCrc ? "OK" : "FAILED") << "\n";
        std::cout << "PCM Section      : OK\n\n";
    }

    if (!warnings.empty())
    {
        std::cout << "Warnings:\n";
        for (const auto& w : warnings)
            std::cout << "  - " << w << "\n";
        std::cout << "\n";
    }

    if (!errors.empty())
    {
        std::cout << "Errors:\n";
        for (const auto& e : errors)
            std::cout << "  - " << e << "\n";
        std::cout << "\n";
    }

    std::cout << "Result: " << (passed ? "PASS" : "FAIL") << "\n";
    std::cout << "==============================\n\n";
}

} // namespace Tools
} // namespace Vhl
