// =============================================================================
// BinaryWriter.h — VHL Builder Backend: Binary Output Abstraction
//
// All binary output from the Serializer goes through BinaryWriter.
// No other Backend component touches std::ostream or file handles directly.
//
// Design rules:
//   - All multi-byte integers written as little-endian (VHL spec requirement).
//   - All writes are tracked for position (tell) and CRC accumulation.
//   - Pad bytes are always zero.
//   - The writer owns an internal byte buffer; the final buffer is flushed
//     to a file via flush(). This allows the Serializer to seek back and
//     patch fields (e.g., the header CRC) without re-opening the file.
//
// Rationale for buffer-then-flush vs direct file writing:
//   VHL requires seek-and-patch (the header CRC can only be computed after
//   all bytes are written). On a file stream this would require seekp() which
//   is unreliable across platforms for binary files created from scratch.
//   Using an in-memory buffer makes seek/patch perfectly reliable, and the
//   entire file is flushed to disk in one write — simpler and faster.
// =============================================================================

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace Vhl {
namespace Backend {

class BinaryWriter
{
public:
    BinaryWriter();

    // =========================================================================
    // Position and state
    // =========================================================================

    /// Current write position (byte offset from beginning of buffer).
    size_t tell() const { return _pos; }

    /// Total bytes written (may exceed tell() if seek was used).
    size_t size() const { return _buf.size(); }

    /// True if no errors have occurred.
    bool ok() const { return _ok; }

    // =========================================================================
    // Primitive writes (little-endian)
    // =========================================================================

    void writeU8 (uint8_t  v);
    void writeU16(uint16_t v);  ///< Little-endian uint16
    void writeU32(uint32_t v);  ///< Little-endian uint32
    void writeI8 (int8_t   v);
    void writeI16(int16_t  v);  ///< Little-endian int16
    void writeI32(int32_t  v);  ///< Little-endian int32

    // =========================================================================
    // Block writes
    // =========================================================================

    /// Write a raw byte buffer of exactly `length` bytes.
    void writeBytes(const void* data, size_t length);

    /// Write `count` zero bytes.
    void writePadding(size_t count);

    /// Write a fixed-length char field. Writes exactly `fieldLen` bytes:
    ///   - copies min(str.size(), fieldLen-1) characters
    ///   - null-terminates
    ///   - pads remaining bytes with zero
    void writeStringField(const std::string& str, size_t fieldLen);

    // =========================================================================
    // Seek (patch previously written bytes)
    // =========================================================================

    /// Move write position to an absolute byte offset within the buffer.
    /// Does NOT extend the buffer — use to patch already-written sections.
    void seekTo(size_t absoluteOffset);

    /// Move write position back to the end of the buffer (resume appending).
    void seekToEnd();

    // =========================================================================
    // Alignment
    // =========================================================================

    /// Advance position to the next multiple of `alignment` (1, 2, or 4 bytes).
    /// Writes zero padding bytes to fill the gap.
    void alignTo(size_t alignment);

    // =========================================================================
    // Reserve space (for later patching)
    // =========================================================================

    /// Write `count` zero bytes and return the position of the first byte.
    /// Caller saves this position and patches later via seekTo().
    size_t reserve(size_t count);

    // =========================================================================
    // Access raw buffer
    // =========================================================================

    const std::vector<uint8_t>& buffer() const { return _buf; }

    // =========================================================================
    // Flush to file
    // =========================================================================

    /// Write the complete in-memory buffer to a file at the given path.
    /// Returns true on success.
    bool flush(const std::string& filePath) const;

private:
    std::vector<uint8_t> _buf;
    size_t               _pos;  ///< Current write position
    bool                 _ok;

    /// Ensure the buffer is large enough for `_pos + n` bytes,
    /// extending with zeros if necessary.
    void ensureCapacity(size_t n);
};

} // namespace Backend
} // namespace Vhl
