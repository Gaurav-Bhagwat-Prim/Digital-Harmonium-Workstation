// =============================================================================
// SfzLexer.h — VHL Builder Frontend: SFZ File Tokenizer
//
// The SfzLexer reads one SFZ file and produces a sequence of tokens.
// It does not interpret semantics — it only identifies structural elements.
//
// Token types:
//   BlockHeader  — "<group>" or "<region>" (possibly with trailing opcodes)
//   Opcode       — "name=value" pair
//   Comment      — "// text" line (used for metadata extraction)
//   Blank        — empty/whitespace-only line (ignored by parser)
//
// Tokenization rules matching Polyphone's SFZ output format:
//   - "<region> sample=path/to/file.wav" — header + opcode on same line
//   - "lokey=C4 hikey=C5"               — multiple opcodes on one line
//   - "ampeg_attack=0.1"                — single opcode per line
//   - "ampeg_holdcc133=0.5 // sfz v2"  — opcode followed by comment suffix
//   - "// Name : Harmonium"             — pure comment line
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <istream>

namespace Vhl {
namespace Frontend {

// =============================================================================
// Token types
// =============================================================================

enum class TokenType
{
    BlockHeader,  ///< e.g. "<group>" or "<region>"
    OpcodeValue,  ///< e.g. "tune" with value "−50" (pre-split)
    CommentLine,  ///< Line starting with "//" — used for metadata
    EndOfFile,
};

struct Token
{
    TokenType   type;
    int         line;   ///< 1-based line number in the source file

    // For BlockHeader:
    std::string blockName; ///< "group" or "region"

    // For OpcodeValue:
    std::string opcode; ///< lower-cased opcode name
    std::string value;  ///< raw value string (not yet interpreted)

    // For CommentLine:
    std::string commentText; ///< text after "//"
};

// =============================================================================
// SfzLexer
// =============================================================================

/// Tokenizes one SFZ file into a flat list of tokens.
/// Does not allocate any BOM objects. Does not interpret values.
class SfzLexer
{
public:
    explicit SfzLexer() = default;

    /// Tokenize the SFZ file at the given path.
    /// Returns false if the file could not be opened.
    bool tokenize(const std::string& filePath);

    /// Tokenize from an already-open stream (for unit testing).
    void tokenize(std::istream& stream, const std::string& sourceLabel);

    const std::vector<Token>& tokens() const { return _tokens; }
    const std::string& sourceFile()    const { return _sourceFile; }
    bool hadError()                    const { return _hadError; }
    const std::string& errorMessage()  const { return _errorMsg; }

private:
    // Process one source line, adding zero or more tokens to _tokens.
    void processLine(const std::string& rawLine, int lineNumber);

    // Extract all opcode=value pairs from a string fragment.
    // The fragment must NOT contain the block header portion.
    void extractOpcodes(const std::string& fragment, int lineNumber);

    // Trim leading and trailing whitespace.
    static std::string trim(const std::string& s);

    // Lower-case a string.
    static std::string toLower(const std::string& s);

    std::vector<Token> _tokens;
    std::string        _sourceFile;
    bool               _hadError  = false;
    std::string        _errorMsg;
};

} // namespace Frontend
} // namespace Vhl
