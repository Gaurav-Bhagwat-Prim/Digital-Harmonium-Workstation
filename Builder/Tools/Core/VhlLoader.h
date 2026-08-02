// =============================================================================
// VhlLoader.h — VHL Core Library: Binary Instrument Loader
//
// Reads raw binary .vhl files into memory, validates section offset boundaries,
// calculates CRC-32, and populates a VhlDocument.
// =============================================================================

#pragma once

#include <string>
#include "VhlDocument.h"

namespace Vhl {
namespace Tools {
namespace Core {

class VhlLoader
{
public:
    VhlLoader() = default;

    /// Load a .vhl file into a VhlDocument.
    /// Returns true if file was opened and basic header unpacked successfully.
    static bool loadFromFile(const std::string& filePath, VhlDocument& outDoc);
};

} // namespace Core
} // namespace Tools
} // namespace Vhl
