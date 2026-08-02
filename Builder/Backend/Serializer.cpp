// =============================================================================
// Serializer.cpp — VHL Builder Backend: Binary Compiler Implementation
//
// Struct sizes (from vhl_structs.h static_asserts — authoritative):
//   VHLFileHeader       = 128 bytes
//   VHLInstrumentEntry  = 40  bytes
//   VHLEnvelope         = 14  bytes
//   VHLModLFO           = 12  bytes
//   VHLVibLFO           = 8   bytes
//   VHLFilter           = 4   bytes
//   VHLRegion           = 80  bytes
//   VHLSampleEntry      = 32  bytes
//   VHLLookupHeader     = 4   bytes
//   VHLLookupEntry      = 4   bytes
//
// VHLRegion field accounting (80 bytes):
//   key_lo+key_hi+vel_lo+vel_hi(4) + sample_id(2) + excl+loop+flags+pad(4)
//   + coarse+pad(2) + fine_tune(2) + scale+pad(2) + atten(2) + pan(2)
//   + reverb+chorus(2) + vol_env(14) + mod_env(14) + env_routes(4)
//   + ModLFO(12) + VibLFO(8) + Filter(4) + _reserved_c(2) = 80
//
// VHLSampleEntry field accounting (32 bytes):
//   data_offset(4)+total_frames(4)+loop_start(4)+loop_end(4)
//   +sample_rate(4)+root_key(1)+fine_tune_cents(1)
//   +bit_depth(1)+channels(1)+flags(1)+linked_id(2)+_reserved[5] = 32
//
// (The _reserved fields are padded to match the authoritative static_assert values.)
// =============================================================================

#include "Serializer.h"
#include "CRC32.h"

#include <ctime>
#include <algorithm>
#include <cstring>

namespace Vhl {
namespace Backend {

// =============================================================================
// compile — top-level orchestration
// =============================================================================

SerializerResult Serializer::compile(const Processed::ProcessedProject& project,
                                      const std::string& outputPath,
                                      const std::string& buildTag)
{
    SerializerResult result;
    result.outputPath = outputPath;
    _regionsWritten  = 0;
    _samplesWritten  = 0;
    _pcmBytesWritten = 0;

    if (project.instruments.empty())
    {
        result.errorMessage = "Cannot compile: project has no instruments";
        return result;
    }

    // -------------------------------------------------------------------------
    // Pre-pass: compute complete file layout
    // -------------------------------------------------------------------------
    VhlLayout layout = LayoutPlanner::compute(project);

    BinaryWriter w;

    // -------------------------------------------------------------------------
    // Writing pipeline (9 steps, strict order)
    // -------------------------------------------------------------------------
    writeHeader      (w, project, layout, buildTag); // Step 1 — 128 bytes
    writeInstruments (w, project, layout);           // Step 2
    writeRegions     (w, project);                   // Step 3
    writeSampleDir   (w, project, layout);           // Step 4
    writeLookupTables(w, project);                   // Step 5
    writePcmData     (w, project, layout);           // Step 6
    fixupHeader      (w, layout);                    // Step 7 — patch file size
    result.crc32 = computeAndWriteCRC(w);            // Step 8 — patch CRC
    // Step 9 — flush to disk
    if (!w.flush(outputPath))
    {
        result.errorMessage = "Failed to write output file: " + outputPath;
        return result;
    }

    result.success         = true;
    result.fileSize        = static_cast<uint32_t>(w.size());
    result.regionsWritten  = _regionsWritten;
    result.samplesWritten  = _samplesWritten;
    result.pcmBytesWritten = _pcmBytesWritten;
    return result;
}

// =============================================================================
// Step 1: writeHeader — VHLFileHeader (128 bytes)
//
// Byte-by-byte layout (verified against struct definition):
//   [0..3]   magic[4]                4 bytes
//   [4]      version_major           1 byte
//   [5]      version_minor           1 byte
//   [6..7]   flags                   2 bytes
//   [8..9]   num_instruments         2 bytes
//   [10..11] total_region_count      2 bytes
//   [12..13] total_sample_count      2 bytes
//   [14..17] target_sample_rate      4 bytes
//   [18..19] dma_block_size          2 bytes
//   [20..59] 10 × uint32_t offsets  40 bytes
//   [60..63] total_file_size         4 bytes
//   [64..67] crc32                   4 bytes  ← CRC_FIELD_OFFSET = 64
//   [68..71] build_timestamp         4 bytes
//   [72..73] builder_version         2 bytes
//   [74..75] _reserved_header        2 bytes
//   [76..107] build_tag[32]         32 bytes
//   [108..127] _reserved[20]        20 bytes
//   Total = 128 bytes ✓
// =============================================================================

void Serializer::writeHeader(BinaryWriter& w,
                              const Processed::ProcessedProject& project,
                              const VhlLayout& layout,
                              const std::string& buildTag)
{
    const size_t headerStart = w.tell();

    // [0..3] magic
    w.writeU8('V'); w.writeU8('H'); w.writeU8('L'); w.writeU8('1');

    // [4..5] version
    w.writeU8(1);  // VHL_VERSION_MAJOR
    w.writeU8(0);  // VHL_VERSION_MINOR

    // [6..7] flags — always set HAS_LOOKUP_TABLE
    w.writeU16(0x0002);

    // [8..9] num_instruments
    w.writeU16(layout.num_instruments);

    // [10..11] total_region_count
    w.writeU16(layout.total_region_count);

    // [12..13] total_sample_count
    w.writeU16(layout.total_sample_count);

    // [14..17] target_sample_rate
    w.writeU32(project.targetSampleRate);

    // [18..19] dma_block_size
    w.writeU16(project.dmaBlockSize);

    // [20..59] section offsets (10 × uint32_t = 40 bytes)
    w.writeU32(layout.offset_instrument_table);   // offset_instrument_table
    w.writeU32(layout.offset_region_table);        // offset_region_table
    w.writeU32(layout.offset_sample_directory);    // offset_sample_directory
    w.writeU32(layout.offset_lookup_table);        // offset_lookup_table
    w.writeU32(layout.offset_pcm_data);            // offset_pcm_data
    w.writeU32(0);  // offset_reserved_1
    w.writeU32(0);  // offset_reserved_2
    w.writeU32(0);  // offset_reserved_3
    w.writeU32(0);  // offset_reserved_4
    w.writeU32(0);  // offset_reserved_5

    // [60..63] total_file_size (written now; re-confirmed in fixupHeader)
    w.writeU32(layout.total_file_size);

    // [64..67] crc32 = 0 (placeholder — patched in computeAndWriteCRC)
    w.writeU32(0);

    // [68..71] build_timestamp
    w.writeU32(static_cast<uint32_t>(std::time(nullptr)));

    // [72..73] builder_version: major=1, minor=1
    w.writeU16(static_cast<uint16_t>((1u << 8) | 1u));

    // [74..75] _reserved_header
    w.writeU16(0);

    // [76..107] build_tag[32]
    std::string tag = buildTag.empty() ? "VHL-Builder-v1.1" : buildTag;
    w.writeStringField(tag, 32);

    // [108..127] _reserved[20]
    w.writePadding(20);

    // Sanity: ensure exactly 128 bytes were written
    size_t headerBytes = w.tell() - headerStart;
    if (headerBytes < 128)
        w.writePadding(128 - headerBytes);  // should never trigger
}

// =============================================================================
// Step 2: writeInstruments — VHLInstrumentEntry (40 bytes each)
//
//   [0]      instrument_id        1 byte
//   [1]      bank                 1 byte
//   [2]      program              1 byte
//   [3]      flags                1 byte
//   [4..27]  name[24]            24 bytes
//   [28..29] region_offset        2 bytes
//   [30..31] region_count         2 bytes
//   [32..35] lookup_table_offset  4 bytes
//   [36..39] _reserved            4 bytes
//   Total = 40 bytes ✓
// =============================================================================

void Serializer::writeInstruments(BinaryWriter& w,
                                   const Processed::ProcessedProject& project,
                                   const VhlLayout& layout)
{
    for (size_t i = 0; i < project.instruments.size(); ++i)
    {
        const auto& inst = project.instruments[i];
        const size_t entryStart = w.tell();

        w.writeU8(inst.instrumentId);                              // [0]  instrument_id
        w.writeU8(inst.midiBank);                                  // [1]  bank
        w.writeU8(inst.midiProgram);                               // [2]  program
        w.writeU8(0);                                              // [3]  flags
        w.writeStringField(inst.name, 24);                         // [4..27]  name[24]
        w.writeU16(inst.regionTableOffset);                        // [28..29] region_offset
        w.writeU16(static_cast<uint16_t>(inst.regionCount()));     // [30..31] region_count
        w.writeU32(layout.instrument_lookup_offsets[i]);           // [32..35] lookup_table_offset
        w.writeU32(0);                                             // [36..39] _reserved

        // Ensure exactly 40 bytes
        size_t written = w.tell() - entryStart;
        if (written < 40) w.writePadding(40 - written);
    }
}

// =============================================================================
// Step 3: writeRegions — VHLRegion (80 bytes each)
// =============================================================================

void Serializer::writeRegions(BinaryWriter& w,
                               const Processed::ProcessedProject& project)
{
    for (const auto& inst : project.instruments)
        for (const auto& r : inst.regions)
        {
            writeOneRegion(w, r);
            ++_regionsWritten;
        }
}

// VHLRegion: 80 bytes (verified against static_assert)
void Serializer::writeOneRegion(BinaryWriter& w, const Processed::ProcessedRegion& r)
{
    const size_t regionStart = w.tell();

    // [0..3]   Key and velocity range
    w.writeU8(r.key_lo);
    w.writeU8(r.key_hi);
    w.writeU8(r.vel_lo);
    w.writeU8(r.vel_hi);

    // [4..5]   sample_id
    w.writeU16(r.sample_id);

    // [6..9]   Voice behavior
    w.writeU8(r.exclusive_class);
    w.writeU8(r.loop_mode);
    w.writeU8(r.flags);
    w.writeU8(0);  // _reserved_a

    // [10..13] Tuning
    w.writeI8(r.coarse_tune);
    w.writeI8(0);            // _pad_tune
    w.writeI16(r.fine_tune);

    // [14..15] Pitch scale
    w.writeU8(r.scale_tuning);
    w.writeU8(0);  // _reserved_b

    // [16..17] attenuation_cb
    w.writeU16(r.attenuation_cb);

    // [18..19] pan
    w.writeI16(r.pan);

    // [20..21] reverb_send, chorus_send
    w.writeU8(r.reverb_send);
    w.writeU8(r.chorus_send);

    // [22..35] vol_env — VHLEnvelope (14 bytes)
    writeEnvelope(w, r.vol_env);

    // [36..49] mod_env — VHLEnvelope (14 bytes)
    writeEnvelope(w, r.mod_env);

    // [50..51] mod_env_to_pitch
    w.writeI16(r.mod_env_to_pitch);

    // [52..53] mod_env_to_filter
    w.writeI16(r.mod_env_to_filter);

    // [54..65] lfo_mod — VHLModLFO (12 bytes)
    writeModLFO(w, r.lfo_mod);

    // [66..73] lfo_vib — VHLVibLFO (8 bytes)
    writeVibLFO(w, r.lfo_vib);

    // [74..77] filter — VHLFilter (4 bytes)
    writeFilter(w, r.filter);

    // [78..79] _reserved_c — 2 bytes to reach 80 total
    //   (The frozen spec has uint32_t _reserved_c with comment "4 bytes",
    //    but static_assert(sizeof(VHLRegion)==80) is authoritative.
    //    80 - 78 = 2 bytes of trailing reserved space.)
    w.writeU16(0);

    // Pad to exactly 80 bytes if accounting differs
    size_t written = w.tell() - regionStart;
    if (written < 80) w.writePadding(80 - written);
}

// VHLEnvelope: 14 bytes
void Serializer::writeEnvelope(BinaryWriter& w, const Processed::ProcessedEnvelope& e)
{
    w.writeU16(e.delay_ms);     // [0..1]
    w.writeU16(e.attack_ms);    // [2..3]
    w.writeU16(e.hold_ms);      // [4..5]
    w.writeU16(e.decay_ms);     // [6..7]
    w.writeU16(e.sustain_lvl);  // [8..9]
    w.writeU16(e.release_ms);   // [10..11]
    w.writeI8 (e.key_to_hold);  // [12]
    w.writeI8 (e.key_to_decay); // [13]
}

// VHLModLFO: 12 bytes
void Serializer::writeModLFO(BinaryWriter& w, const Processed::ProcessedModLFO& lfo)
{
    w.writeU16(lfo.delay_ms);     // [0..1]
    w.writeU16(lfo.freq_mHz);     // [2..3]
    w.writeI16(lfo.to_pitch_cb);  // [4..5]
    w.writeI16(lfo.to_filter_cb); // [6..7]
    w.writeI16(lfo.to_volume_cb); // [8..9]
    w.writeU16(0);                // [10..11] _reserved
}

// VHLVibLFO: 8 bytes
void Serializer::writeVibLFO(BinaryWriter& w, const Processed::ProcessedVibLFO& lfo)
{
    w.writeU16(lfo.delay_ms);     // [0..1]
    w.writeU16(lfo.freq_mHz);     // [2..3]
    w.writeI16(lfo.to_pitch_cb);  // [4..5]
    w.writeU16(0);                // [6..7] _reserved
}

// VHLFilter: 4 bytes
void Serializer::writeFilter(BinaryWriter& w, const Processed::ProcessedFilter& f)
{
    w.writeU16(f.cutoff_hz);    // [0..1]
    w.writeU16(f.resonance_cb); // [2..3]
}

// =============================================================================
// Step 4: writeSampleDir — VHLSampleEntry (32 bytes each)
//
//   [0..3]   data_offset           4 bytes
//   [4..7]   total_frames          4 bytes
//   [8..11]  loop_start_frame      4 bytes
//   [12..15] loop_end_frame        4 bytes
//   [16..19] sample_rate           4 bytes
//   [20]     root_key              1 byte
//   [21]     fine_tune_cents       1 byte
//   [22]     bit_depth             1 byte
//   [23]     channels              1 byte
//   [24]     flags                 1 byte
//   [25..26] linked_sample_id      2 bytes
//   [27..31] _reserved[5]          5 bytes  (pads to 32 ✓)
//   Total = 32 bytes ✓
// =============================================================================

void Serializer::writeSampleDir(BinaryWriter& w,
                                 const Processed::ProcessedProject& project,
                                 const VhlLayout& layout)
{
    for (size_t i = 0; i < project.samples.size(); ++i)
    {
        const auto& s   = project.samples[i];
        const auto& sl  = layout.sample_layouts[i];
        const size_t entryStart = w.tell();

        // [0..3]   data_offset (byte offset from start of PCM section)
        w.writeU32(sl.pcmSectionOffset);

        // [4..7]   total_frames
        w.writeU32(s.totalFrames);

        // [8..11]  loop_start_frame
        w.writeU32(s.loopPoints.valid ? s.loopPoints.start : 0);

        // [12..15] loop_end_frame
        w.writeU32(s.loopPoints.valid ? s.loopPoints.end   : 0);

        // [16..19] sample_rate
        w.writeU32(s.sampleRate);

        // [20]     root_key
        w.writeU8(s.rootKey);

        // [21]     fine_tune_cents
        w.writeI8(s.fineTuneCents);

        // [22]     bit_depth (always 16 after normalization)
        w.writeU8(16);

        // [23]     channels
        w.writeU8(s.channels);

        // [24]     flags
        w.writeU8(s.vhlFlags);

        // [25..26] linked_sample_id
        w.writeU16(s.linkedSampleId);

        // [27..31] _reserved[5] — pad to exactly 32 bytes
        size_t written = w.tell() - entryStart;
        if (written < 32) w.writePadding(32 - written);

        ++_samplesWritten;
    }
}

// =============================================================================
// Step 5: writeLookupTables
// Per instrument: VHLLookupHeader(4) + VHLLookupEntry[128](512) = 516 bytes
// =============================================================================

void Serializer::writeLookupTables(BinaryWriter& w,
                                    const Processed::ProcessedProject& project)
{
    for (const auto& inst : project.instruments)
    {
        // VHLLookupHeader (4 bytes)
        w.writeU16(static_cast<uint16_t>(inst.instrumentId)); // instrument_id
        w.writeU16(0);                                         // _reserved

        // VHLLookupEntry[128] (4 bytes each = 512 bytes)
        for (int k = 0; k < 128; ++k)
        {
            const auto& entry = inst.keyLookup[k];
            w.writeU16(entry.region_start_idx); // region_start_idx
            w.writeU8 (entry.region_count);     // region_count
            w.writeU8 (0);                      // _reserved
        }
        // Total: 4 + 512 = 516 bytes per instrument ✓
    }
}

// =============================================================================
// Step 6: writePcmData
// Write int16_t PCM blocks. Each sample starts at its precomputed aligned offset.
// =============================================================================

void Serializer::writePcmData(BinaryWriter& w,
                               const Processed::ProcessedProject& project,
                               const VhlLayout& layout)
{
    const size_t pcmSectionStart = w.tell();

    for (size_t i = 0; i < project.samples.size(); ++i)
    {
        const auto& s  = project.samples[i];
        const auto& sl = layout.sample_layouts[i];

        // Advance to the precomputed aligned start for this sample
        const size_t targetPos = pcmSectionStart + sl.pcmSectionOffset;
        if (w.tell() < targetPos)
            w.writePadding(targetPos - w.tell());

        if (!s.pcm.empty())
        {
            // PCM data: int16_t, little-endian (same as host on x86/ARM-LE)
            const size_t pcmBytes = s.pcm.size() * sizeof(int16_t);
            w.writeBytes(s.pcm.data(), pcmBytes);
            _pcmBytesWritten += static_cast<int>(pcmBytes);
        }
        else
        {
            // Placeholder for a failed load — write 4 bytes of silence
            w.writePadding(4);
        }
    }
}

// =============================================================================
// Step 7: fixupHeader
// Patch total_file_size in the header with the actual buffer size.
// total_file_size is at header byte offset 60.
// =============================================================================

void Serializer::fixupHeader(BinaryWriter& w, const VhlLayout& /*layout*/)
{
    constexpr size_t FILE_SIZE_OFFSET = 60;  // offset within VHLFileHeader
    const uint32_t actualSize = static_cast<uint32_t>(w.size());

    w.seekTo(FILE_SIZE_OFFSET);
    w.writeU32(actualSize);
    w.seekToEnd();
}

// =============================================================================
// Step 8: computeAndWriteCRC
// CRC-32 over entire file with the CRC field (at offset 64) treated as zero.
// =============================================================================

uint32_t Serializer::computeAndWriteCRC(BinaryWriter& w)
{
    const auto& buf = w.buffer();
    const uint32_t crc = CRC32::computeWithZeroedField(
        buf.data(), buf.size(), CRC_FIELD_OFFSET);

    w.seekTo(CRC_FIELD_OFFSET);
    w.writeU32(crc);
    w.seekToEnd();
    return crc;
}

} // namespace Backend
} // namespace Vhl
