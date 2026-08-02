// =============================================================================
// Serializer.h — VHL Builder Backend: Binary Compiler Orchestrator
//
// The Serializer is the top-level Backend component.
// It accepts a ProcessedProject (output of Phase 2B) and produces a valid
// .vhl binary file conforming exactly to vhl_structs.h v1.0.
//
// Internal execution order (matches the prompt's specified sequence):
//   Step 1  writeHeader()       — placeholder header with zeroed CRC
//   Step 2  writeInstruments()  — VHLInstrumentEntry[] section
//   Step 3  writeRegions()      — VHLRegion[] section (all instruments)
//   Step 4  writeSampleDir()    — VHLSampleEntry[] section
//   Step 5  writeLookupTables() — VHLLookupHeader + VHLLookupEntry[128] per instrument
//   Step 6  writePcmData()      — raw int16_t PCM blocks
//   Step 7  fixupHeader()       — patch header with final offsets and file size
//   Step 8  computeAndWriteCRC()— CRC-32 over complete file, written into header
//   Step 9  flush()             — write buffer to disk
//
// No semantic decisions are made here. All values come from ProcessedProject.
// =============================================================================

#pragma once

#include <string>
#include "../BuilderObjectModel/ProcessedProject.h"
#include "BinaryWriter.h"
#include "LayoutPlanner.h"

namespace Vhl {
namespace Backend {

// =============================================================================
// SerializerResult — outcome of a compile() call
// =============================================================================

struct SerializerResult
{
    bool        success      = false;
    uint32_t    fileSize     = 0;
    uint32_t    crc32        = 0;
    std::string outputPath;
    std::string errorMessage;

    // Build statistics
    int         regionsWritten = 0;
    int         samplesWritten = 0;
    int         pcmBytesWritten = 0;
};

// =============================================================================
// Serializer
// =============================================================================

class Serializer
{
public:
    Serializer() = default;

    /// Compile a ProcessedProject into a .vhl binary file.
    /// outputPath: absolute path for the output file (created or overwritten).
    /// buildTag:   optional human-readable tag written into the header (max 31 chars).
    SerializerResult compile(const Processed::ProcessedProject& project,
                              const std::string& outputPath,
                              const std::string& buildTag = "");

private:
    // =========================================================================
    // Offset of the CRC field within VHLFileHeader
    // Computed from the struct layout:
    //   magic[4]=4, version=2, flags=2, num_instruments=2,
    //   total_region_count+total_sample_count=4,
    //   target_sample_rate=4, dma_block_size=2,
    //   10 × uint32_t section offsets = 40,
    //   total_file_size=4
    //   → crc32 is at offset 4+2+2+2+4+4+2+40+4 = 64
    // =========================================================================
    static constexpr size_t CRC_FIELD_OFFSET = 64;

    // =========================================================================
    // Section writers
    // =========================================================================

    /// Step 1: Write VHLFileHeader with zeroed CRC and placeholder offsets.
    void writeHeader(BinaryWriter& w,
                     const Processed::ProcessedProject& project,
                     const VhlLayout& layout,
                     const std::string& buildTag);

    /// Step 2: Write VHLInstrumentEntry[] section.
    void writeInstruments(BinaryWriter& w,
                          const Processed::ProcessedProject& project,
                          const VhlLayout& layout);

    /// Step 3: Write VHLRegion[] section (all instruments, all regions).
    void writeRegions(BinaryWriter& w,
                      const Processed::ProcessedProject& project);

    /// Step 4: Write VHLSampleEntry[] section.
    void writeSampleDir(BinaryWriter& w,
                        const Processed::ProcessedProject& project,
                        const VhlLayout& layout);

    /// Step 5: Write lookup tables (VHLLookupHeader + VHLLookupEntry[128]) per instrument.
    void writeLookupTables(BinaryWriter& w,
                           const Processed::ProcessedProject& project);

    /// Step 6: Write raw PCM data blocks (with 4-byte alignment padding).
    void writePcmData(BinaryWriter& w,
                      const Processed::ProcessedProject& project,
                      const VhlLayout& layout);

    /// Step 7: Seek back to header and patch total_file_size.
    ///         (Offsets are already written correctly in Step 1.)
    void fixupHeader(BinaryWriter& w, const VhlLayout& layout);

    /// Step 8: Compute CRC-32 over entire buffer (CRC field = 0) and patch it.
    uint32_t computeAndWriteCRC(BinaryWriter& w);

    // =========================================================================
    // Per-structure writers
    // =========================================================================

    void writeOneRegion (BinaryWriter& w, const Processed::ProcessedRegion& r);
    void writeEnvelope  (BinaryWriter& w, const Processed::ProcessedEnvelope& e);
    void writeModLFO    (BinaryWriter& w, const Processed::ProcessedModLFO& lfo);
    void writeVibLFO    (BinaryWriter& w, const Processed::ProcessedVibLFO& lfo);
    void writeFilter    (BinaryWriter& w, const Processed::ProcessedFilter& f);

    // =========================================================================
    // Build statistics (accumulated during compile())
    // =========================================================================

    int _regionsWritten  = 0;
    int _samplesWritten  = 0;
    int _pcmBytesWritten = 0;
};

} // namespace Backend
} // namespace Vhl
