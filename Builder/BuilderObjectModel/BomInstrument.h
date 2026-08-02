// =============================================================================
// BomInstrument.h — VHL Builder Object Model: Instrument
//
// One BomInstrument corresponds to one exported SFZ file (one Polyphone preset).
// It contains the instrument's metadata and its complete list of regions.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include "BomRegion.h"

namespace Vhl {
namespace Bom {

/// Metadata extracted from the SFZ file header comment block.
/// Polyphone writes these as comment lines at the top of each exported SFZ.
struct BomMetadata
{
    std::string name;       ///< Instrument name (from "// Name :")
    std::string author;     ///< Sound designer (from "// Author :")
    std::string copyright;  ///< Copyright string (from "// Copyright :")
    std::string date;       ///< Export date (from "// Date :")
    std::string comment;    ///< Free-form comment (from "// Comment :")
    std::string sfzPath;    ///< Absolute path to the source SFZ file
};

/// One instrument, corresponding to one exported SFZ file.
/// Contains a flat list of BomRegions — each region is fully resolved
/// (group opcodes are already merged in by the SfzParser).
struct BomInstrument
{
    BomMetadata metadata;

    /// All regions, in the order they appear in the SFZ file.
    /// Sorted by keyRange.lo at parse time (Polyphone emits them sorted, but
    /// we re-sort to guarantee ordering for the Backend's lookup table generator).
    std::vector<BomRegion> regions;

    /// MIDI program number extracted from the SFZ filename (if presetPrefix was used).
    /// −1 if not available.
    int midiProgram = -1;

    /// MIDI bank number. 0 for melodic, 128 for percussion.
    int midiBank = 0;

    // -------------------------------------------------------------------------
    // Convenience queries
    // -------------------------------------------------------------------------

    size_t regionCount()   const { return regions.size(); }
    bool   isEmpty()       const { return regions.empty(); }
    bool   hasRegions()    const { return !regions.empty(); }

    /// Return the key range covered by all regions combined.
    KeyRange totalKeyRange() const
    {
        if (regions.empty()) return {0, 127};
        uint8_t lo = 127, hi = 0;
        for (const auto& r : regions)
        {
            if (r.keyRange.lo < lo) lo = r.keyRange.lo;
            if (r.keyRange.hi > hi) hi = r.keyRange.hi;
        }
        return {lo, hi};
    }
};

} // namespace Bom
} // namespace Vhl
