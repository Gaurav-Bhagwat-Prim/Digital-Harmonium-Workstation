// =============================================================================
// VhlReader.h — VHL Reader: Pre-Phase 2D Verification Tool
//
// Standalone read-only inspector and structural validator for .vhl binary files.
// Reads packed structs directly from memory buffer loaded via std::ifstream.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "vhl_structs.h"

namespace Vhl {
namespace Tools {

class VhlReader
{
public:
    VhlReader() = default;

    /// Read the complete binary file into memory. Returns false if file cannot be opened.
    bool load(const std::string& filePath);

    /// Print complete inspection details with clean section headings.
    void printAll() const;

    /// Individual section printers
    void printHeader() const;
    void printInstrumentTable() const;
    void printRegionTable() const;
    void printSampleDirectory() const;
    void printLookupTables() const;

    /// Perform structural sanity checks and CRC-32 verification.
    /// Fills warnings and errors lists. Returns true if zero errors.
    bool verifyIntegrity(std::vector<std::string>& warnings, std::vector<std::string>& errors) const;

    /// Print summary box and final PASS / FAIL badge.
    void printVerificationSummary() const;

    /// Direct access to raw buffer and header pointer
    const std::vector<uint8_t>& buffer() const { return _buffer; }
    const VHLFileHeader* header() const;

private:
    std::string          _filePath;
    std::vector<uint8_t> _buffer;
    bool                 _loaded = false;

    /// Bounds-checked struct casting helper
    template <typename T>
    const T* getStructAt(size_t byteOffset) const
    {
        if (byteOffset + sizeof(T) > _buffer.size())
            return nullptr;
        return reinterpret_cast<const T*>(_buffer.data() + byteOffset);
    }
};

} // namespace Tools
} // namespace Vhl
