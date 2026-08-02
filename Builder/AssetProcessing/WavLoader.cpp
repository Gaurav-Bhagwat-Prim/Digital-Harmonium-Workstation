// =============================================================================
// WavLoader.cpp — VHL Builder Asset Processing: WAV File Loader Implementation
// =============================================================================

#include "WavLoader.h"

#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Vhl {
namespace AssetProcessing {

using Frontend::DiagnosticCollector;
using Frontend::Severity;

// ---------------------------------------------------------------------------
// Internal byte-reading helpers (always little-endian)
// ---------------------------------------------------------------------------

static uint16_t readU16LE(std::istream& f)
{
    uint8_t b[2];
    f.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

static uint32_t readU32LE(std::istream& f)
{
    uint8_t b[4];
    f.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

// ---------------------------------------------------------------------------
// readChunkHeader
// ---------------------------------------------------------------------------

bool WavLoader::readChunkHeader(std::istream& f, RiffChunk& out)
{
    f.read(out.id, 4);
    if (f.gcount() < 4) return false;
    out.size = readU32LE(f);
    return f.good();
}

// ---------------------------------------------------------------------------
// parseFmt
// ---------------------------------------------------------------------------

bool WavLoader::parseFmt(std::istream& f, uint32_t chunkSize,
                          uint16_t& audioFormat, uint16_t& numChannels,
                          uint32_t& sampleRate,  uint16_t& bitsPerSample,
                          DiagnosticCollector& diag, [[maybe_unused]] const std::string& file)
{
    if (chunkSize < 16)
    {
        diag.error(-1, "fmt ", "Chunk too small (" + std::to_string(chunkSize) + " bytes)");
        return false;
    }

    audioFormat  = readU16LE(f); // 1=PCM, 3=IEEE float, 0xFFFE=extensible
    numChannels  = readU16LE(f);
    sampleRate   = readU32LE(f);
    readU32LE(f); // byteRate (ignored)
    readU16LE(f); // blockAlign (ignored)
    bitsPerSample = readU16LE(f);

    // Skip any extra fmt bytes
    if (chunkSize > 16)
        f.seekg(chunkSize - 16, std::ios::cur);

    // Validate
    if (audioFormat != 1 && audioFormat != 0xFFFE)
    {
        diag.error(-1, "fmt ", "Unsupported audio format: " + std::to_string(audioFormat)
                   + " (only PCM format 1 or extensible 0xFFFE is supported)");
        return false;
    }
    if (numChannels < 1 || numChannels > 2)
    {
        diag.error(-1, "fmt ", "Unsupported channel count: " + std::to_string(numChannels));
        return false;
    }
    if (bitsPerSample != 16 && bitsPerSample != 24)
    {
        diag.error(-1, "fmt ", "Unsupported bit depth: " + std::to_string(bitsPerSample)
                   + " (only 16-bit and 24-bit PCM supported)");
        return false;
    }
    if (sampleRate < 8000 || sampleRate > 192000)
    {
        diag.warning(-1, "fmt ", "Unusual sample rate: " + std::to_string(sampleRate) + " Hz");
    }

    return true;
}

// ---------------------------------------------------------------------------
// parseSmpl — extract loop points from the smpl chunk
//
// smpl chunk layout (all little-endian uint32_t):
//   [0]  manufacturer
//   [1]  product
//   [2]  sample_period
//   [3]  MIDI unity note
//   [4]  MIDI pitch fraction
//   [5]  SMPTE format
//   [6]  SMPTE offset
//   [7]  sample_loops (count)
//   [8]  sampler_data size
//   then sample_loops × SampleLoop structs (6 × uint32_t each):
//     [0] cue_point_id
//     [1] type (0=forward, 1=ping-pong, 2=backward)
//     [2] start frame
//     [3] end frame (inclusive)
//     [4] fraction
//     [5] play_count
// ---------------------------------------------------------------------------

void WavLoader::parseSmpl(std::istream& f, uint32_t chunkSize,
                            Processed::LoopPoints& out,
                            DiagnosticCollector& diag, [[maybe_unused]] const std::string& file)
{
    if (chunkSize < 36)
    {
        diag.warning(-1, "smpl", "Chunk too small, ignoring");
        f.seekg(chunkSize, std::ios::cur);
        return;
    }

    // Read fixed header (9 × uint32_t = 36 bytes)
    readU32LE(f); // manufacturer
    readU32LE(f); // product
    readU32LE(f); // sample_period
    readU32LE(f); // MIDI unity note (we use SFZ pitch_keycenter instead)
    readU32LE(f); // MIDI pitch fraction
    readU32LE(f); // SMPTE format
    readU32LE(f); // SMPTE offset
    uint32_t numLoops    = readU32LE(f);
    readU32LE(f); // sampler data size

    if (numLoops == 0 || chunkSize < 36 + 24)
    {
        f.seekg(chunkSize - 36, std::ios::cur);
        return;
    }

    // Read only the first loop (multiple loops are not supported by VHL)
    readU32LE(f); // cue_point_id
    uint32_t loopType  = readU32LE(f);
    uint32_t loopStart = readU32LE(f);
    uint32_t loopEnd   = readU32LE(f) + 1; // smpl is inclusive; VHL is exclusive
    readU32LE(f); // fraction
    readU32LE(f); // play_count

    if (loopType > 1)
    {
        diag.warning(-1, "smpl", "Unsupported loop type " + std::to_string(loopType)
                     + " — only forward loop supported");
    }
    else
    {
        out.start = loopStart;
        out.end   = loopEnd;
        out.valid = true;
    }

    // Skip any remaining loops or extra data
    uint32_t consumed = 36 + 24;
    if (chunkSize > consumed)
        f.seekg(chunkSize - consumed, std::ios::cur);
}

// ---------------------------------------------------------------------------
// readPcm — read PCM data, normalize to int16
// ---------------------------------------------------------------------------

bool WavLoader::readPcm(std::istream& f, uint32_t chunkSize,
                         uint16_t bitsPerSample, [[maybe_unused]] uint16_t numChannels,
                         std::vector<int16_t>& out,
                         DiagnosticCollector& diag, [[maybe_unused]] const std::string& file)
{
    if (chunkSize == 0)
    {
        diag.error(-1, "data", "Empty data chunk");
        return false;
    }

    if (bitsPerSample == 16)
    {
        // Direct read — already int16_t
        size_t numSamples = chunkSize / 2;
        out.resize(numSamples);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(numSamples * 2));
        if (static_cast<size_t>(f.gcount()) < numSamples * 2)
        {
            diag.warning(-1, "data", "PCM data truncated — short read");
        }
    }
    else if (bitsPerSample == 24)
    {
        // 24-bit to 16-bit: read 3 bytes per sample, take top 16 bits
        uint32_t numSamples24 = chunkSize / 3;
        out.resize(numSamples24);

        std::vector<uint8_t> buf(chunkSize);
        f.read(reinterpret_cast<char*>(buf.data()), chunkSize);
        size_t bytesRead = static_cast<size_t>(f.gcount());

        uint32_t samplesRead = static_cast<uint32_t>(std::min(bytesRead / 3, (size_t)numSamples24));
        for (uint32_t i = 0; i < samplesRead; ++i)
        {
            // Little-endian 24-bit signed → int32_t → shift to 16-bit
            int32_t raw = static_cast<int32_t>(
                buf[i*3 + 0]
              | (buf[i*3 + 1] << 8)
              | (static_cast<int8_t>(buf[i*3 + 2]) << 16)  // sign-extend the MSB
            );
            out[i] = static_cast<int16_t>(raw >> 8); // take top 16 bits
        }

        diag.info(-1, "data", "24-bit → 16-bit truncation applied ("
                  + std::to_string(samplesRead) + " samples)");
    }
    else
    {
        diag.error(-1, "data", "Unsupported bit depth for PCM conversion");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// validateLoopPoints
// ---------------------------------------------------------------------------

bool WavLoader::validateLoopPoints(Processed::LoopPoints& lp, uint32_t totalFrames,
                                    DiagnosticCollector& diag, [[maybe_unused]] const std::string& file)
{
    if (!lp.valid) return true; // no loop — nothing to validate

    if (lp.start >= totalFrames || lp.end > totalFrames)
    {
        diag.warning(-1, "loop", "Loop points exceed file length ("
                     + std::to_string(lp.start) + ".." + std::to_string(lp.end)
                     + " vs " + std::to_string(totalFrames) + " frames) — loop disabled");
        lp.valid = false;
        return false;
    }
    if (lp.start >= lp.end)
    {
        diag.warning(-1, "loop", "Loop start >= loop end — loop disabled");
        lp.valid = false;
        return false;
    }
    if (lp.end - lp.start < 4)
    {
        diag.warning(-1, "loop", "Loop length < 4 frames ("
                     + std::to_string(lp.end - lp.start) + ") — may cause artifacts");
    }
    return true;
}

// ---------------------------------------------------------------------------
// load — main entry point
// ---------------------------------------------------------------------------

std::optional<Processed::ProcessedSample>
WavLoader::load(const std::string& absolutePath, DiagnosticCollector& diag)
{
    std::ifstream f(absolutePath, std::ios::binary);
    if (!f.is_open())
    {
        diag.fatal(-1, "open", "Cannot open WAV file: " + absolutePath);
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // 1. Read RIFF header
    // -----------------------------------------------------------------------
    char riffId[4];
    f.read(riffId, 4);
    if (std::strncmp(riffId, "RIFF", 4) != 0)
    {
        diag.error(-1, "RIFF", "Not a RIFF file: " + absolutePath);
        return std::nullopt;
    }
    readU32LE(f); // RIFF chunk size (ignored — we scan chunks individually)
    char waveId[4];
    f.read(waveId, 4);
    if (std::strncmp(waveId, "WAVE", 4) != 0)
    {
        diag.error(-1, "WAVE", "Not a WAVE file: " + absolutePath);
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // 2. Scan sub-chunks
    // -----------------------------------------------------------------------
    uint16_t audioFormat  = 0;
    uint16_t numChannels  = 0;
    uint32_t sampleRate   = 0;
    uint16_t bitsPerSample = 0;
    bool     fmtParsed    = false;

    std::vector<int16_t> pcmData;
    bool     dataParsed   = false;

    Processed::LoopPoints loopPoints;

    while (f.good() && !f.eof())
    {
        RiffChunk chunk;
        if (!readChunkHeader(f, chunk)) break;

        auto nextChunkPos = f.tellg();
        nextChunkPos += static_cast<std::streamoff>(chunk.size);
        // Pad to even byte boundary (RIFF chunks are 2-byte aligned)
        if (chunk.size & 1) nextChunkPos += static_cast<std::streamoff>(1);

        if (std::strncmp(chunk.id, "fmt ", 4) == 0)
        {
            fmtParsed = parseFmt(f, chunk.size, audioFormat, numChannels,
                                  sampleRate, bitsPerSample, diag, absolutePath);
        }
        else if (std::strncmp(chunk.id, "data", 4) == 0)
        {
            if (!fmtParsed)
            {
                diag.error(-1, "data", "data chunk encountered before fmt  chunk");
                return std::nullopt;
            }
            dataParsed = readPcm(f, chunk.size, bitsPerSample, numChannels,
                                  pcmData, diag, absolutePath);
        }
        else if (std::strncmp(chunk.id, "smpl", 4) == 0)
        {
            parseSmpl(f, chunk.size, loopPoints, diag, absolutePath);
        }
        // All other chunks (LIST, bext, etc.) are silently skipped

        f.seekg(nextChunkPos);
    }

    if (!fmtParsed)
    {
        diag.error(-1, "fmt ", "No fmt  chunk found in: " + absolutePath);
        return std::nullopt;
    }
    if (!dataParsed)
    {
        diag.error(-1, "data", "No data chunk found in: " + absolutePath);
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // 3. Compute frame count and validate loop
    // -----------------------------------------------------------------------
    uint32_t totalFrames = static_cast<uint32_t>(pcmData.size()) / numChannels;
    validateLoopPoints(loopPoints, totalFrames, diag, absolutePath);

    // -----------------------------------------------------------------------
    // 4. Build ProcessedSample (caller will set sampleId and name)
    // -----------------------------------------------------------------------
    Processed::ProcessedSample s;
    s.absolutePath  = absolutePath;
    s.sampleRate    = sampleRate;
    s.bitDepth      = static_cast<uint8_t>(bitsPerSample);
    s.channels      = static_cast<uint8_t>(numChannels);
    s.totalFrames   = totalFrames;
    s.loopPoints    = loopPoints;
    s.pcm           = std::move(pcmData);

    // VHL sample flags
    if (loopPoints.valid)
        s.vhlFlags |= 0x01; // VHL_SAMPLE_FLAG_LOOP_VALID
    if (numChannels == 2)
    {
        // Stereo: encoded in channels field; no dedicated flag in VHL v1
    }

    return s;
}

} // namespace AssetProcessing
} // namespace Vhl
