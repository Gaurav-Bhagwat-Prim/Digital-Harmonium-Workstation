// =============================================================================
// VhlFormatter.h — VHL Core Library: Output & Visual Formatting Utilities
// =============================================================================

#pragma once

#include <string>
#include <cstdint>

namespace Vhl {
namespace Tools {
namespace Core {

class VhlDocument;

class VhlFormatter
{
public:
    /// Format byte size into human-readable string (e.g. "983.8 KB", "1.2 MB", "450 B")
    static std::string formatBytes(size_t bytes);

    /// Generate an ASCII progress/percentage bar (e.g., "[████████░░] 80%")
    static std::string formatBar(float percentage, int width = 10);

    /// Generate ASCII text-based key range coverage map for MIDI keys 0-127
    static std::string formatKeyCoverageMap(const VhlDocument& doc);

    /// Format MIDI note number to name (e.g., 60 -> "C4", 69 -> "A4")
    static std::string noteName(uint8_t noteNum);
};

} // namespace Core
} // namespace Tools
} // namespace Vhl
