// =============================================================================
// VhlFormatter.cpp — VHL Core Library: Output & Visual Formatting Implementation
// =============================================================================

#include "VhlFormatter.h"
#include "VhlDocument.h"

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace Vhl {
namespace Tools {
namespace Core {

std::string VhlFormatter::formatBytes(size_t bytes)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);

    if (bytes >= 1024 * 1024 * 1024)
        ss << ((double)bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    else if (bytes >= 1024 * 1024)
        ss << ((double)bytes / (1024.0 * 1024.0)) << " MB";
    else if (bytes >= 1024)
        ss << ((double)bytes / 1024.0) << " KB";
    else
        ss << bytes << " B";

    return ss.str();
}

std::string VhlFormatter::formatBar(float percentage, int width)
{
    percentage = std::clamp(percentage, 0.0f, 100.0f);
    int filled = static_cast<int>(std::round((percentage / 100.0f) * width));

    std::string result = "[";
    for (int i = 0; i < width; ++i)
    {
        if (i < filled)
            result += "#"; // standard ASCII for wide compatibility
        else
            result += ".";
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << result << "] " << percentage << "%";
    return ss.str();
}

std::string VhlFormatter::formatKeyCoverageMap(const VhlDocument& doc)
{
    std::vector<int> regionCountPerKey(128, 0);

    for (const auto& lkTable : doc.lookupTables)
    {
        for (int k = 0; k < 128 && k < (int)lkTable.size(); ++k)
        {
            regionCountPerKey[k] += lkTable[k].region_count;
        }
    }

    std::stringstream ss;
    ss << "  [C0..B0] : ";
    for (int k = 0; k < 128; ++k)
    {
        if (k > 0 && k % 12 == 0)
        {
            int octave = (k / 12) - 1;
            ss << "\n  [C" << octave << "..B" << octave << "] : ";
        }
        int count = regionCountPerKey[k];
        if (count == 0)      ss << ".";
        else if (count == 1) ss << "#";
        else if (count == 2) ss << "2";
        else                 ss << "+";
    }
    return ss.str();
}

std::string VhlFormatter::noteName(uint8_t noteNum)
{
    static const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (noteNum / 12) - 1;
    std::string name = NAMES[noteNum % 12];
    return name + std::to_string(octave);
}

} // namespace Core
} // namespace Tools
} // namespace Vhl
