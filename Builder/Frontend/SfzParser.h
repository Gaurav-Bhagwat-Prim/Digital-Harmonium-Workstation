// =============================================================================
// SfzParser.h — VHL Builder Frontend: SFZ → Builder Object Model
//
// The SfzParser converts the token stream produced by SfzLexer into a
// BomInstrument populated with fully-resolved BomRegions.
//
// Key responsibilities:
//   1. Track current <group> opcode accumulator
//   2. On <region>: merge group opcodes into the region (group = defaults,
//      region opcodes override — exactly per the SFZ specification)
//   3. Dispatch each opcode to the correct BomRegion field
//   4. Resolve sample paths relative to the SFZ file's directory
//   5. Extract instrument metadata from comment lines
//   6. Report diagnostics without throwing exceptions
// =============================================================================

#pragma once

#include <string>
#include <map>
#include "../BuilderObjectModel/BomInstrument.h"
#include "ParseError.h"
#include "SfzLexer.h"

namespace Vhl {
namespace Frontend {

class SfzParser
{
public:
    SfzParser() = default;

    /// Parse one SFZ file and return a BomInstrument.
    /// The instrument may be empty if the file has no regions.
    /// Check result.ok() and result.diagnostics for errors/warnings.
    ParseResult<Bom::BomInstrument> parse(const std::string& sfzFilePath);

    /// Parse from a pre-built token list (for testing).
    ParseResult<Bom::BomInstrument> parse(const std::vector<Token>& tokens,
                                           const std::string& sfzFilePath);

private:
    // -------------------------------------------------------------------------
    // Opcode accumulator type: maps lower-cased opcode name → raw value string
    // -------------------------------------------------------------------------
    using OpcodeMap = std::map<std::string, std::string>;

    // -------------------------------------------------------------------------
    // Internal parsing passes
    // -------------------------------------------------------------------------

    /// Extract metadata from comment lines at the top of the token stream.
    Bom::BomMetadata extractMetadata(const std::vector<Token>& tokens,
                                      const std::string& sfzPath);

    /// Build a BomRegion from a merged opcode map (group + region).
    /// The sfzDir is used to resolve relative sample paths.
    Bom::BomRegion buildRegion(const OpcodeMap& opcodes,
                                int regionIndex,
                                int sourceLine,
                                const std::string& sfzDir,
                                DiagnosticCollector& diag);

    // -------------------------------------------------------------------------
    // Per-opcode setters
    // Each function reads from the OpcodeMap and writes to the BomRegion.
    // -------------------------------------------------------------------------

    void applyKeyRange    (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyVelRange    (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyPitch       (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyVolume      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applySample      (Bom::BomRegion& r, const OpcodeMap& m, const std::string& sfzDir,
                           DiagnosticCollector& d, int line);
    void applyLoop        (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyVolEnv      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyModEnv      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyFilter      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyModLFO      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyVibLFO      (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyExclusive   (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyEffects     (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyVelocity    (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);
    void applyChannel     (Bom::BomRegion& r, const OpcodeMap& m, DiagnosticCollector& d, int line);

    // -------------------------------------------------------------------------
    // Value-parsing helpers
    // -------------------------------------------------------------------------

    static std::optional<float>    toFloat (const std::string& s);
    static std::optional<int>      toInt   (const std::string& s);
    static std::optional<uint32_t> toUInt32(const std::string& s);
    static std::optional<uint8_t>  toMidiNote(const std::string& s,
                                               DiagnosticCollector& diag,
                                               int line, const std::string& opcode);

    // -------------------------------------------------------------------------
    // Sample path resolution
    // -------------------------------------------------------------------------

    /// Resolve a SFZ-relative path (backslash or forward slash) to an absolute path.
    static std::string resolveSamplePath(const std::string& sfzValue,
                                          const std::string& sfzDir);
};

} // namespace Frontend
} // namespace Vhl
