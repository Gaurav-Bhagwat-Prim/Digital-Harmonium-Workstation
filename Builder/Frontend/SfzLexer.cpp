// =============================================================================
// SfzLexer.cpp — VHL Builder Frontend: SFZ File Tokenizer Implementation
// =============================================================================

#include "SfzLexer.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Vhl {
namespace Frontend {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string SfzLexer::trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string SfzLexer::toLower(const std::string& s)
{
    std::string out = s;
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// ---------------------------------------------------------------------------
// Public: tokenize from file path
// ---------------------------------------------------------------------------

bool SfzLexer::tokenize(const std::string& filePath)
{
    _sourceFile = filePath;
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        _hadError  = true;
        _errorMsg  = "Cannot open file: " + filePath;
        return false;
    }
    tokenize(file, filePath);
    return !_hadError;
}

// ---------------------------------------------------------------------------
// Public: tokenize from stream
// ---------------------------------------------------------------------------

void SfzLexer::tokenize(std::istream& stream, const std::string& sourceLabel)
{
    _sourceFile = sourceLabel;
    _tokens.clear();
    _hadError = false;
    _errorMsg.clear();

    std::string rawLine;
    int lineNumber = 0;
    while (std::getline(stream, rawLine))
    {
        ++lineNumber;
        processLine(rawLine, lineNumber);
    }

    Token eof;
    eof.type = TokenType::EndOfFile;
    eof.line = lineNumber + 1;
    _tokens.push_back(eof);
}

// ---------------------------------------------------------------------------
// processLine — classify and tokenize one source line
// ---------------------------------------------------------------------------

void SfzLexer::processLine(const std::string& rawLine, int lineNumber)
{
    // -----------------------------------------------------------------------
    // 1. Strip trailing Windows line endings and trim
    // -----------------------------------------------------------------------
    std::string line = trim(rawLine);
    if (line.empty())
        return; // blank line — skip

    // -----------------------------------------------------------------------
    // 2. Comment-only lines ("//")
    //    Polyphone writes: "// Name      : Harmonium"
    //    We preserve these for metadata extraction.
    // -----------------------------------------------------------------------
    if (line.size() >= 2 && line[0] == '/' && line[1] == '/')
    {
        Token t;
        t.type        = TokenType::CommentLine;
        t.line        = lineNumber;
        t.commentText = trim(line.substr(2));
        _tokens.push_back(t);
        return;
    }

    // -----------------------------------------------------------------------
    // 3. Detect "<group>" or "<region>" header.
    //    Polyphone format: "<region> sample=path\to\file.wav"
    //    The block name may be followed by opcode=value pairs on the same line.
    // -----------------------------------------------------------------------
    if (line[0] == '<')
    {
        size_t closeAngle = line.find('>');
        if (closeAngle == std::string::npos)
        {
            // Malformed — treat rest of line as skipped
            return;
        }
        std::string blockName = toLower(trim(line.substr(1, closeAngle - 1)));

        Token header;
        header.type      = TokenType::BlockHeader;
        header.line      = lineNumber;
        header.blockName = blockName;
        _tokens.push_back(header);

        // Everything after ">" on the same line may contain opcode=value pairs
        std::string rest = line.substr(closeAngle + 1);
        if (!trim(rest).empty())
            extractOpcodes(trim(rest), lineNumber);

        return;
    }

    // -----------------------------------------------------------------------
    // 4. Everything else: opcode=value pairs.
    //    Strip any trailing "// ..." comment first.
    // -----------------------------------------------------------------------
    {
        // Find inline comment: " // " (with spaces) or at start of token
        // Polyphone writes e.g.: "ampeg_holdcc133=0.5 // sfz v2"
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos)
            line = trim(line.substr(0, commentPos));
        if (line.empty())
            return;

        extractOpcodes(line, lineNumber);
    }
}

// ---------------------------------------------------------------------------
// extractOpcodes — parse zero or more "opcode=value" pairs from a string
//
// A single string may contain multiple pairs separated by whitespace:
//   "lokey=C4 hikey=C5"
//   "lovel=0 hivel=127"
//   "sample=samples\Reed_A4.wav"
//   "ampeg_attack=0.010"
//
// The challenge: values may not contain whitespace (SFZ spec), EXCEPT for
// the 'sample' opcode which may contain spaces in pathnames (Windows paths).
// Polyphone does NOT write spaces in paths, so we can treat all values as
// whitespace-delimited for our purpose.
// ---------------------------------------------------------------------------

void SfzLexer::extractOpcodes(const std::string& fragment, int lineNumber)
{
    // Split the fragment into whitespace-delimited tokens, then reassemble
    // opcode=value pairs. Any token containing '=' is an opcode=value pair.
    // Tokens without '=' are continuations of the previous value (e.g., file paths
    // with spaces — rare in Polyphone output but handled gracefully).

    std::istringstream ss(fragment);
    std::string word;

    Token pending;
    pending.type = TokenType::OpcodeValue;
    pending.line = lineNumber;
    bool hasPending = false;

    while (ss >> word)
    {
        size_t eqPos = word.find('=');
        if (eqPos != std::string::npos)
        {
            // Commit any pending token (this handles path continuation correctly)
            if (hasPending)
                _tokens.push_back(pending);

            // Start new opcode token
            pending.opcode = toLower(trim(word.substr(0, eqPos)));
            pending.value  = trim(word.substr(eqPos + 1));
            hasPending     = true;
        }
        else
        {
            // No '=' — this is a continuation of the current value (e.g. a path with spaces)
            if (hasPending)
            {
                pending.value += " ";
                pending.value += word;
            }
            // else: stray word with no opcode context — ignore
        }
    }

    if (hasPending)
        _tokens.push_back(pending);
}

} // namespace Frontend
} // namespace Vhl
