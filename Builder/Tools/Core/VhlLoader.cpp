// =============================================================================
// VhlLoader.cpp — VHL Core Library: Binary Instrument Loader Implementation
// =============================================================================

#include "VhlLoader.h"
#include "../../Backend/CRC32.h"

#include <fstream>
#include <iostream>
#include <cstring>

namespace Vhl {
namespace Tools {
namespace Core {

bool VhlLoader::loadFromFile(const std::string& filePath, VhlDocument& doc)
{
    doc = VhlDocument{};
    doc.filePath = filePath;

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < static_cast<std::streamsize>(sizeof(VHLFileHeader)))
    {
        return false;
    }

    file.seekg(0, std::ios::beg);
    doc.rawBuffer.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(doc.rawBuffer.data()), fileSize))
    {
        doc.rawBuffer.clear();
        return false;
    }

    // 1. Copy Header
    std::memcpy(&doc.header, doc.rawBuffer.data(), sizeof(VHLFileHeader));

    // 2. Compute CRC-32
    doc.computedCrc32 = Backend::CRC32::computeWithZeroedField(doc.rawBuffer.data(), doc.rawBuffer.size(), 64);
    doc.crcValid = (doc.header.crc32 == doc.computedCrc32);

    size_t totalSize = doc.rawBuffer.size();

    // 3. Unpack Instrument Table
    size_t instOffset = doc.header.offset_instrument_table;
    size_t instCount  = doc.header.num_instruments;
    if (instOffset + instCount * sizeof(VHLInstrumentEntry) <= totalSize)
    {
        doc.instruments.resize(instCount);
        std::memcpy(doc.instruments.data(), doc.rawBuffer.data() + instOffset, instCount * sizeof(VHLInstrumentEntry));
    }

    // 4. Unpack Region Table
    size_t regOffset = doc.header.offset_region_table;
    size_t regCount  = doc.header.total_region_count;
    if (regOffset + regCount * sizeof(VHLRegion) <= totalSize)
    {
        doc.regions.resize(regCount);
        std::memcpy(doc.regions.data(), doc.rawBuffer.data() + regOffset, regCount * sizeof(VHLRegion));
    }

    // 5. Unpack Sample Directory
    size_t sampOffset = doc.header.offset_sample_directory;
    size_t sampCount  = doc.header.total_sample_count;
    if (sampOffset + sampCount * sizeof(VHLSampleEntry) <= totalSize)
    {
        doc.samples.resize(sampCount);
        std::memcpy(doc.samples.data(), doc.rawBuffer.data() + sampOffset, sampCount * sizeof(VHLSampleEntry));
    }

    // 6. Unpack Lookup Tables
    doc.lookupTables.resize(doc.instruments.size());
    for (size_t i = 0; i < doc.instruments.size(); ++i)
    {
        size_t lkOffset = doc.instruments[i].lookup_table_offset;
        size_t blockSize = sizeof(VHLLookupHeader) + 128 * sizeof(VHLLookupEntry);
        if (lkOffset + blockSize <= totalSize)
        {
            doc.lookupTables[i].resize(128);
            std::memcpy(doc.lookupTables[i].data(), doc.rawBuffer.data() + lkOffset + sizeof(VHLLookupHeader), 128 * sizeof(VHLLookupEntry));
        }
    }

    // 7. Calculate stats
    doc.isLoaded = true;
    doc.computeStatistics();

    return true;
}

} // namespace Core
} // namespace Tools
} // namespace Vhl
