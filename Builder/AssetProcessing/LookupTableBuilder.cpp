// =============================================================================
// LookupTableBuilder.cpp
// =============================================================================

#include "LookupTableBuilder.h"
#include <algorithm>

namespace Vhl {
namespace AssetProcessing {

using Processed::ProcessedInstrument;
using Processed::LookupEntry;

// ---------------------------------------------------------------------------
// build
// ---------------------------------------------------------------------------

void LookupTableBuilder::build(ProcessedInstrument& inst,
                                Frontend::DiagnosticCollector& diag)
{
    // Reset all entries to "no regions"
    for (int k = 0; k < 128; ++k)
    {
        inst.keyLookup[k].region_start_idx = 0xFFFF;
        inst.keyLookup[k].region_count     = 0;
    }

    if (inst.regions.empty()) return;

    const uint16_t base = inst.regionTableOffset;
    const int      n    = static_cast<int>(inst.regions.size());

    // For each key, find all regions that cover it.
    // Regions are already sorted by key_lo ascending (AssetProcessor guarantee).
    // We can do this efficiently with a single pass per key or a two-pass algorithm.
    // Given typical harmonium size (88 keys, 1-3 layers), brute force is fast enough.

    for (int key = 0; key < 128; ++key)
    {
        // Find the first region that might cover this key
        int first = -1;
        int count = 0;

        for (int i = 0; i < n; ++i)
        {
            const auto& r = inst.regions[i];
            if (r.key_lo <= static_cast<uint8_t>(key) &&
                r.key_hi >= static_cast<uint8_t>(key))
            {
                if (first == -1)
                    first = i;
                ++count;
            }
        }

        if (count > 0)
        {
            uint32_t globalIdx = static_cast<uint32_t>(base) + static_cast<uint32_t>(first);
            if (globalIdx > 0xFFFF)
            {
                diag.error(-1, "lookup",
                    "Region index overflow for key " + std::to_string(key)
                    + " (global index " + std::to_string(globalIdx)
                    + " exceeds uint16_t range)");
                globalIdx = 0;
            }
            if (count > 255)
            {
                diag.warning(-1, "lookup",
                    "Key " + std::to_string(key) + " has " + std::to_string(count)
                    + " regions — capping at 255");
                count = 255;
            }
            inst.keyLookup[key].region_start_idx = static_cast<uint16_t>(globalIdx);
            inst.keyLookup[key].region_count     = static_cast<uint8_t>(count);
        }
    }
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

void LookupTableBuilder::validate(const ProcessedInstrument& inst,
                                   Frontend::DiagnosticCollector& diag)
{
    int keysWithNoRegions = 0;
    int keysWithVelGaps   = 0;

    for (int key = 0; key < 128; ++key)
    {
        const auto& entry = inst.keyLookup[key];
        if (entry.region_count == 0)
        {
            ++keysWithNoRegions;
            continue;
        }

        // Verify velocity coverage: scan all regions for this key and check
        // that every velocity 0-127 is covered by at least one region.
        // (Only a warning, not an error — gaps in vel coverage are allowed.)
        bool covered[128] = {};
        uint16_t start = entry.region_start_idx;
        uint8_t  cnt   = entry.region_count;

        // Adjust for regionTableOffset to get local index
        int localStart = static_cast<int>(start) - static_cast<int>(inst.regionTableOffset);

        for (int ri = localStart; ri < localStart + cnt; ++ri)
        {
            if (ri < 0 || ri >= static_cast<int>(inst.regions.size())) continue;
            const auto& r = inst.regions[ri];
            for (int v = r.vel_lo; v <= r.vel_hi; ++v)
                covered[v] = true;
        }

        bool hasGap = false;
        for (int v = 0; v < 128; ++v)
        {
            if (!covered[v]) { hasGap = true; break; }
        }
        if (hasGap)
            ++keysWithVelGaps;
    }

    if (keysWithNoRegions > 0)
        diag.info(-1, "lookup",
            std::to_string(keysWithNoRegions) + " keys have no regions (expected for sparse instruments)");

    if (keysWithVelGaps > 0)
        diag.warning(-1, "lookup",
            std::to_string(keysWithVelGaps) + " keys have velocity coverage gaps");
}

} // namespace AssetProcessing
} // namespace Vhl
