// =============================================================================
// LayoutPlanner.h — VHL Builder Backend: File Memory Map Calculator
//
// The LayoutPlanner determines the exact byte position of every section and
// every individual record in the VHL file before a single byte is written.
//
// It is the Backend's equivalent of a linker's address-assignment pass.
//
// Section order (from vhl_structs.h):
//   [0] VHLFileHeader          128 bytes (fixed)
//   [1] VHLInstrumentEntry[]   N_inst × 40 bytes
//   [2] VHLRegion[]            N_reg  × 80 bytes
//   [3] VHLSampleEntry[]       N_samp × 32 bytes
//   [4] Lookup Tables          N_inst × (4 + 128×4) bytes  [= N_inst × 516 bytes]
//   [5] PCM Data               variable, each sample 4-byte aligned
//
// All sizes are derived from the ProcessedProject — nothing is hardcoded.
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>
#include "../BuilderObjectModel/ProcessedProject.h"

namespace Vhl {
namespace Backend {

// =============================================================================
// SampleLayout — byte position of one sample's PCM data within the PCM section
// =============================================================================

struct SampleLayout
{
    uint32_t pcmSectionOffset;  ///< Byte offset from the start of the PCM section
    uint32_t pcmSizeBytes;      ///< Total PCM bytes for this sample (after alignment)
};

// =============================================================================
// VhlLayout — complete file memory map
// =============================================================================

struct VhlLayout
{
    // -------------------------------------------------------------------------
    // Section offsets (absolute byte offset from file start)
    // -------------------------------------------------------------------------
    uint32_t offset_header;            ///< Always 0
    uint32_t offset_instrument_table;  ///< Always 128
    uint32_t offset_region_table;
    uint32_t offset_sample_directory;
    uint32_t offset_lookup_table;
    uint32_t offset_pcm_data;

    // -------------------------------------------------------------------------
    // Section sizes (bytes)
    // -------------------------------------------------------------------------
    uint32_t size_header;              ///< Always 128
    uint32_t size_instrument_table;
    uint32_t size_region_table;
    uint32_t size_sample_directory;
    uint32_t size_lookup_table;
    uint32_t size_pcm_data;

    // -------------------------------------------------------------------------
    // Per-instrument lookup table offsets (absolute file offset)
    // -------------------------------------------------------------------------
    std::vector<uint32_t> instrument_lookup_offsets; ///< One per instrument

    // -------------------------------------------------------------------------
    // Per-sample PCM layout (offset within the PCM section)
    // -------------------------------------------------------------------------
    std::vector<SampleLayout> sample_layouts; ///< One per sample

    // -------------------------------------------------------------------------
    // Total
    // -------------------------------------------------------------------------
    uint32_t total_file_size;

    // -------------------------------------------------------------------------
    // Counts (convenience)
    // -------------------------------------------------------------------------
    uint16_t num_instruments;
    uint16_t total_region_count;
    uint16_t total_sample_count;
};

// =============================================================================
// LayoutPlanner
// =============================================================================

class LayoutPlanner
{
public:
    /// Compute the complete file layout for a ProcessedProject.
    /// Must be called before the Serializer starts writing.
    static VhlLayout compute(const Processed::ProcessedProject& project);

private:
    // Per-section size constants from vhl_structs.h
    static constexpr uint32_t HEADER_SIZE          = 128;
    static constexpr uint32_t INSTRUMENT_ENTRY_SIZE = 40;
    static constexpr uint32_t REGION_SIZE           = 80;
    static constexpr uint32_t SAMPLE_ENTRY_SIZE     = 32;
    static constexpr uint32_t LOOKUP_HEADER_SIZE    = 4;
    static constexpr uint32_t LOOKUP_ENTRY_SIZE     = 4;
    static constexpr uint32_t LOOKUP_KEYS           = 128;
    // Lookup block per instrument: 1 header + 128 entries = 4 + 512 = 516 bytes
    static constexpr uint32_t LOOKUP_BLOCK_SIZE     = LOOKUP_HEADER_SIZE
                                                    + LOOKUP_KEYS * LOOKUP_ENTRY_SIZE;
    static constexpr uint32_t PCM_ALIGNMENT         = 4; ///< Each sample starts on 4-byte boundary

    /// Round `n` up to the next multiple of `align`.
    static uint32_t alignUp(uint32_t n, uint32_t align)
    {
        return (n + align - 1) & ~(align - 1);
    }
};

} // namespace Backend
} // namespace Vhl
