// =============================================================================
// CRC32.cpp — VHL Builder Backend: CRC-32 Implementation
// =============================================================================

#include "CRC32.h"
#include <cstring>

namespace Vhl {
namespace Backend {

// ---------------------------------------------------------------------------
// Lookup table (256 entries, generated at program start)
// ---------------------------------------------------------------------------

const uint32_t* CRC32::lookupTable()
{
    static uint32_t table[256] = {};
    static bool     initialized = false;

    if (!initialized)
    {
        // Standard CRC-32 lookup table generation.
        // Polynomial: 0xEDB88320 (reversed form of 0x04C11DB7)
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        initialized = true;
    }
    return table;
}

uint32_t CRC32::table(uint8_t index)
{
    return lookupTable()[index];
}

// ---------------------------------------------------------------------------
// Constructor and reset
// ---------------------------------------------------------------------------

CRC32::CRC32() : _state(0xFFFFFFFFu) {}

void CRC32::reset()
{
    _state = 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

void CRC32::update(uint8_t byte)
{
    _state = (_state >> 8) ^ table(static_cast<uint8_t>(_state ^ byte));
}

void CRC32::update(const void* data, size_t length)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint32_t* lut = lookupTable();

    while (length >= 8)
    {
        // Unrolled 8-byte inner loop for speed
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[0])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[1])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[2])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[3])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[4])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[5])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[6])];
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ p[7])];
        p += 8;
        length -= 8;
    }
    while (length--)
        _state = (_state >> 8) ^ lut[static_cast<uint8_t>(_state ^ *p++)];
}

// ---------------------------------------------------------------------------
// finalize
// ---------------------------------------------------------------------------

uint32_t CRC32::finalize()
{
    uint32_t result = _state ^ 0xFFFFFFFFu;
    reset();
    return result;
}

// ---------------------------------------------------------------------------
// Static: compute — one-shot CRC over a buffer
// ---------------------------------------------------------------------------

uint32_t CRC32::compute(const void* data, size_t length)
{
    CRC32 crc;
    crc.update(data, length);
    return crc.finalize();
}

// ---------------------------------------------------------------------------
// Static: computeWithZeroedField
//
// Computes CRC as if the 4-byte field at crcFieldOffset were all zeros.
// Used to produce the VHLFileHeader.crc32 value:
//   1. Feed bytes [0, crcFieldOffset)
//   2. Feed 4 zero bytes (in place of the CRC field)
//   3. Feed bytes [crcFieldOffset+4, length)
// ---------------------------------------------------------------------------

uint32_t CRC32::computeWithZeroedField(const void* data, size_t length,
                                        size_t crcFieldOffset)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    CRC32 crc;

    // Feed bytes before the CRC field
    if (crcFieldOffset > length) crcFieldOffset = length;
    crc.update(p, crcFieldOffset);

    // Feed 4 zero bytes (placeholder for CRC field)
    const uint8_t zeros[4] = {0, 0, 0, 0};
    crc.update(zeros, 4);

    // Feed bytes after the CRC field
    size_t afterOffset = crcFieldOffset + 4;
    if (afterOffset < length)
        crc.update(p + afterOffset, length - afterOffset);

    return crc.finalize();
}

} // namespace Backend
} // namespace Vhl
