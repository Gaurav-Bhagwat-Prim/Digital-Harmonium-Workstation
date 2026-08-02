// =============================================================================
// CRC32.h — VHL Builder Backend: CRC-32 Computation
//
// Computes the CRC-32 checksum used to verify VHL file integrity.
//
// Algorithm: ISO 3309 / ITU-T V.42 CRC-32 (same as zlib, Ethernet, PKZip).
// Polynomial: 0xEDB88320 (reversed representation of 0x04C11DB7).
//
// VHL convention (from vhl_structs.h):
//   The CRC field in VHLFileHeader is computed over the ENTIRE file,
//   with the crc32 field itself set to 0 during computation.
//
// Usage:
//   // Incremental (for streaming writes)
//   CRC32 crc;
//   crc.update(buffer, length);
//   crc.update(more_data, more_length);
//   uint32_t result = crc.finalize();
//
//   // One-shot
//   uint32_t checksum = CRC32::compute(data, length);
// =============================================================================

#pragma once

#include <cstdint>
#include <cstddef>

namespace Vhl {
namespace Backend {

class CRC32
{
public:
    CRC32();

    /// Feed a block of bytes into the running CRC state.
    void update(const void* data, size_t length);

    /// Feed a single byte.
    void update(uint8_t byte);

    /// Return the final CRC-32 value and reset internal state.
    uint32_t finalize();

    /// Reset internal state (equivalent to constructing a new CRC32 object).
    void reset();

    // -------------------------------------------------------------------------
    // Static helpers
    // -------------------------------------------------------------------------

    /// One-shot: compute the CRC-32 of a contiguous memory block.
    static uint32_t compute(const void* data, size_t length);

    /// One-shot: compute the CRC-32 of a buffer with the CRC field zeroed.
    /// crcFieldOffset is the byte offset of the 4-byte CRC field within data.
    static uint32_t computeWithZeroedField(const void* data, size_t length,
                                            size_t crcFieldOffset);

private:
    uint32_t _state;

    static uint32_t table(uint8_t index);
    static const uint32_t* lookupTable();
};

} // namespace Backend
} // namespace Vhl
