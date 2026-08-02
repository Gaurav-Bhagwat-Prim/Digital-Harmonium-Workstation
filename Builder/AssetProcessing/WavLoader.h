// =============================================================================
// WavLoader.h — VHL Builder Asset Processing: WAV File Loader
//
// Reads PCM WAV files exported by Polyphone and produces ProcessedSample objects.
//
// Supported WAV formats:
//   - PCM (format tag 1) — always; ADPCM and other compressed formats rejected
//   - 16-bit and 24-bit sample depths
//   - Mono and stereo (Polyphone exports mono or stereo pairs as one file)
//   - smpl chunk: loop points extracted if present
//
// All PCM data is normalized to int16_t regardless of source depth.
// 24-bit samples are truncated to 16-bit (upper 16 bits of the 24-bit word).
// =============================================================================

#pragma once

#include <string>
#include <optional>
#include "../BuilderObjectModel/ProcessedSample.h"
#include "../Frontend/ParseError.h"

namespace Vhl {
namespace AssetProcessing {

class WavLoader
{
public:
    /// Load one WAV file and return a ProcessedSample.
    /// The caller must set sampleId and name before use.
    /// Returns nullopt on fatal error; check diag for details.
    std::optional<Processed::ProcessedSample>
    load(const std::string& absolutePath, Frontend::DiagnosticCollector& diag);

private:
    // -------------------------------------------------------------------------
    // RIFF chunk reader helpers
    // -------------------------------------------------------------------------

    struct RiffChunk
    {
        char     id[4];
        uint32_t size;
    };

    /// Read a 4-byte chunk header from the stream position.
    static bool readChunkHeader(std::istream& f, RiffChunk& out);

    /// Parse the fmt  sub-chunk. Returns false on invalid format.
    static bool parseFmt(std::istream& f, uint32_t chunkSize,
                          uint16_t& audioFormat, uint16_t& numChannels,
                          uint32_t& sampleRate,  uint16_t& bitsPerSample,
                          Frontend::DiagnosticCollector& diag, const std::string& file);

    /// Parse the smpl sub-chunk (loop points). Does not abort on error.
    static void parseSmpl(std::istream& f, uint32_t chunkSize,
                           Processed::LoopPoints& out,
                           Frontend::DiagnosticCollector& diag, const std::string& file);

    /// Read PCM data, normalizing 24-bit to 16-bit.
    static bool readPcm(std::istream& f, uint32_t chunkSize,
                         uint16_t bitsPerSample, uint16_t numChannels,
                         std::vector<int16_t>& out,
                         Frontend::DiagnosticCollector& diag, const std::string& file);

    /// Validate loop points against the loaded frame count.
    static bool validateLoopPoints(Processed::LoopPoints& lp, uint32_t totalFrames,
                                    Frontend::DiagnosticCollector& diag, const std::string& file);
};

} // namespace AssetProcessing
} // namespace Vhl
