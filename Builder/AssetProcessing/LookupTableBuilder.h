// =============================================================================
// LookupTableBuilder.h / .cpp — VHL Builder Asset Processing: Key Lookup Tables
//
// Builds the per-instrument lookup acceleration table described in vhl_structs.h
// section 4 (VHLLookupHeader + VHLLookupEntry[128]).
//
// Algorithm:
//   For each MIDI key 0-127:
//     Scan all regions of the instrument.
//     Collect every region whose [key_lo, key_hi] includes this key.
//     Sort them by vel_lo ascending (for O(M) velocity scan at runtime).
//     Store: region_start_idx = global index of first matching region
//            region_count     = number of matching regions for this key
//
// Note on region_start_idx:
//   The lookup entry stores a GLOBAL region table index, i.e.
//   instrument.regionTableOffset + local_region_index.
//
//   For the runtime: at Note-On, it reads keyLookup[key], then scans
//   VHLRegion[region_start_idx .. region_start_idx + region_count - 1]
//   checking vel_lo <= vel <= vel_hi.
//
// Design decision: regions for each key are stored contiguously in the
// ProcessedInstrument.regions array, sorted first by key_lo then by vel_lo.
// This is guaranteed by the sort step in AssetProcessor before building lookups.
// =============================================================================

#pragma once

#include "../BuilderObjectModel/ProcessedProject.h"
#include "../Frontend/ParseError.h"

namespace Vhl {
namespace AssetProcessing {

class LookupTableBuilder
{
public:
    /// Build and populate the keyLookup[128] table in a ProcessedInstrument.
    /// Regions must already be sorted by key_lo (done by AssetProcessor).
    /// regionTableOffset must already be set in the instrument.
    static void build(Processed::ProcessedInstrument& inst,
                      Frontend::DiagnosticCollector& diag);

    /// Validate the lookup table: every key that has regions must find at
    /// least one region matching every velocity in [0-127].
    static void validate(const Processed::ProcessedInstrument& inst,
                         Frontend::DiagnosticCollector& diag);
};

} // namespace AssetProcessing
} // namespace Vhl
