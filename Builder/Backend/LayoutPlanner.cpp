// =============================================================================
// LayoutPlanner.cpp — VHL Builder Backend: File Memory Map Calculator
// =============================================================================

#include "LayoutPlanner.h"
#include <numeric>

namespace Vhl {
namespace Backend {

VhlLayout LayoutPlanner::compute(const Processed::ProcessedProject& project)
{
    VhlLayout layout = {};

    // =========================================================================
    // Counts
    // =========================================================================

    layout.num_instruments   = static_cast<uint16_t>(project.instruments.size());
    layout.total_sample_count = static_cast<uint16_t>(project.samples.size());

    uint32_t totalRegions = 0;
    for (const auto& inst : project.instruments)
        totalRegions += static_cast<uint32_t>(inst.regionCount());
    layout.total_region_count = static_cast<uint16_t>(totalRegions);

    // =========================================================================
    // Section 0: File Header
    // =========================================================================

    layout.offset_header = 0;
    layout.size_header   = HEADER_SIZE;

    // =========================================================================
    // Section 1: Instrument Table
    // =========================================================================

    layout.offset_instrument_table = HEADER_SIZE; // immediately follows header
    layout.size_instrument_table   = layout.num_instruments * INSTRUMENT_ENTRY_SIZE;

    // =========================================================================
    // Section 2: Region Table
    // =========================================================================

    layout.offset_region_table = layout.offset_instrument_table
                                + layout.size_instrument_table;
    layout.size_region_table   = totalRegions * REGION_SIZE;

    // =========================================================================
    // Section 3: Sample Directory
    // =========================================================================

    layout.offset_sample_directory = layout.offset_region_table
                                   + layout.size_region_table;
    layout.size_sample_directory   = layout.total_sample_count * SAMPLE_ENTRY_SIZE;

    // =========================================================================
    // Section 4: Lookup Tables
    //
    // One block per instrument, each block = VHLLookupHeader (4 bytes)
    //                                       + VHLLookupEntry[128] (512 bytes)
    //                                       = 516 bytes
    // =========================================================================

    layout.offset_lookup_table = layout.offset_sample_directory
                                + layout.size_sample_directory;
    layout.size_lookup_table   = layout.num_instruments * LOOKUP_BLOCK_SIZE;

    // Per-instrument lookup offset (absolute file offset)
    layout.instrument_lookup_offsets.resize(layout.num_instruments);
    for (uint16_t i = 0; i < layout.num_instruments; ++i)
    {
        layout.instrument_lookup_offsets[i] =
            layout.offset_lookup_table + i * LOOKUP_BLOCK_SIZE;
    }

    // =========================================================================
    // Section 5: PCM Data
    //
    // Each sample starts at a 4-byte aligned boundary within the PCM section.
    // =========================================================================

    layout.offset_pcm_data = layout.offset_lookup_table
                           + layout.size_lookup_table;

    // Per-sample PCM layout
    layout.sample_layouts.resize(project.samples.size());
    uint32_t pcmCursor = 0;

    for (size_t i = 0; i < project.samples.size(); ++i)
    {
        const auto& s = project.samples[i];

        // Align to 4-byte boundary
        uint32_t alignedStart = alignUp(pcmCursor, PCM_ALIGNMENT);
        uint32_t pcmBytes     = static_cast<uint32_t>(s.pcm.size() * sizeof(int16_t));
        // Aligned size (so next sample also starts aligned)
        uint32_t alignedSize  = alignUp(pcmBytes, PCM_ALIGNMENT);

        layout.sample_layouts[i].pcmSectionOffset = alignedStart;
        layout.sample_layouts[i].pcmSizeBytes     = alignedSize;

        pcmCursor = alignedStart + alignedSize;
    }

    layout.size_pcm_data = pcmCursor;

    // =========================================================================
    // Total file size
    // =========================================================================

    layout.total_file_size = layout.offset_pcm_data + layout.size_pcm_data;

    return layout;
}

} // namespace Backend
} // namespace Vhl
