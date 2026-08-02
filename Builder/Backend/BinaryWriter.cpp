// =============================================================================
// BinaryWriter.cpp — VHL Builder Backend: Binary Output Implementation
// =============================================================================

#include "BinaryWriter.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace Vhl {
namespace Backend {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BinaryWriter::BinaryWriter()
    : _pos(0), _ok(true)
{
    _buf.reserve(4 * 1024 * 1024); // pre-allocate 4 MB (typical instrument size)
}

// ---------------------------------------------------------------------------
// ensureCapacity: extend buffer with zeros up to _pos + n
// ---------------------------------------------------------------------------

void BinaryWriter::ensureCapacity(size_t n)
{
    size_t needed = _pos + n;
    if (needed > _buf.size())
        _buf.resize(needed, 0);
}

// ---------------------------------------------------------------------------
// Primitive writes (little-endian)
// ---------------------------------------------------------------------------

void BinaryWriter::writeU8(uint8_t v)
{
    ensureCapacity(1);
    _buf[_pos++] = v;
}

void BinaryWriter::writeI8(int8_t v)
{
    writeU8(static_cast<uint8_t>(v));
}

void BinaryWriter::writeU16(uint16_t v)
{
    ensureCapacity(2);
    _buf[_pos + 0] = static_cast<uint8_t>(v & 0xFF);
    _buf[_pos + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    _pos += 2;
}

void BinaryWriter::writeI16(int16_t v)
{
    writeU16(static_cast<uint16_t>(v));
}

void BinaryWriter::writeU32(uint32_t v)
{
    ensureCapacity(4);
    _buf[_pos + 0] = static_cast<uint8_t>(v & 0xFF);
    _buf[_pos + 1] = static_cast<uint8_t>((v >>  8) & 0xFF);
    _buf[_pos + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    _buf[_pos + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    _pos += 4;
}

void BinaryWriter::writeI32(int32_t v)
{
    writeU32(static_cast<uint32_t>(v));
}

// ---------------------------------------------------------------------------
// Block writes
// ---------------------------------------------------------------------------

void BinaryWriter::writeBytes(const void* data, size_t length)
{
    if (length == 0) return;
    ensureCapacity(length);
    std::memcpy(_buf.data() + _pos, data, length);
    _pos += length;
}

void BinaryWriter::writePadding(size_t count)
{
    if (count == 0) return;
    ensureCapacity(count);
    std::memset(_buf.data() + _pos, 0, count);
    _pos += count;
}

void BinaryWriter::writeStringField(const std::string& str, size_t fieldLen)
{
    if (fieldLen == 0) return;
    ensureCapacity(fieldLen);
    std::memset(_buf.data() + _pos, 0, fieldLen); // zero the field first
    size_t copyLen = std::min(str.size(), fieldLen - 1); // -1 for null terminator
    if (copyLen > 0)
        std::memcpy(_buf.data() + _pos, str.data(), copyLen);
    _pos += fieldLen;
}

// ---------------------------------------------------------------------------
// Seek / patch
// ---------------------------------------------------------------------------

void BinaryWriter::seekTo(size_t absoluteOffset)
{
    // Buffer must already be large enough — we only patch written bytes
    if (absoluteOffset > _buf.size())
    {
        // Extend with zeros (should not normally happen — indicates a bug)
        _buf.resize(absoluteOffset, 0);
    }
    _pos = absoluteOffset;
}

void BinaryWriter::seekToEnd()
{
    _pos = _buf.size();
}

// ---------------------------------------------------------------------------
// Alignment
// ---------------------------------------------------------------------------

void BinaryWriter::alignTo(size_t alignment)
{
    if (alignment <= 1) return;
    size_t rem = _pos % alignment;
    if (rem != 0)
        writePadding(alignment - rem);
}

// ---------------------------------------------------------------------------
// reserve
// ---------------------------------------------------------------------------

size_t BinaryWriter::reserve(size_t count)
{
    size_t startPos = _pos;
    writePadding(count);
    return startPos;
}

// ---------------------------------------------------------------------------
// flush to file
// ---------------------------------------------------------------------------

bool BinaryWriter::flush(const std::string& filePath) const
{
    std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        return false;
    if (!_buf.empty())
        f.write(reinterpret_cast<const char*>(_buf.data()),
                static_cast<std::streamsize>(_buf.size()));
    return f.good();
}

} // namespace Backend
} // namespace Vhl
