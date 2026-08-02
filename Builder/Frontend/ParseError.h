// =============================================================================
// ParseError.h — VHL Builder Frontend: Error and Warning Types
//
// The Frontend uses ParseResult<T> to propagate errors without exceptions.
// Warnings do not stop parsing. Errors are collected and reported at the end.
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace Vhl {
namespace Frontend {

// =============================================================================
// Severity
// =============================================================================

enum class Severity
{
    Info,    ///< Informational only
    Warning, ///< Opcode skipped or defaulted; parse continues
    Error,   ///< Invalid structure; region or instrument skipped
    Fatal,   ///< Cannot continue (e.g., file not found)
};

// =============================================================================
// ParseDiagnostic — one issue found during parsing
// =============================================================================

struct ParseDiagnostic
{
    Severity    severity;
    std::string sfzFile;   ///< Source SFZ filename (basename)
    int         line;      ///< Line number in the SFZ file (1-based), or −1
    std::string opcode;    ///< Opcode name where the issue occurred, or ""
    std::string message;   ///< Human-readable description

    /// Format for display: "[WARNING] file.sfz:42 (tune): message"
    std::string format() const
    {
        std::string prefix;
        switch (severity)
        {
            case Severity::Info:    prefix = "[INFO]   "; break;
            case Severity::Warning: prefix = "[WARNING]"; break;
            case Severity::Error:   prefix = "[ERROR]  "; break;
            case Severity::Fatal:   prefix = "[FATAL]  "; break;
        }
        std::string loc = sfzFile;
        if (line > 0) loc += ":" + std::to_string(line);
        if (!opcode.empty()) loc += " (" + opcode + ")";
        return prefix + " " + loc + ": " + message;
    }
};

// =============================================================================
// ParseResult<T> — either a value or a fatal error
// =============================================================================

/// Wraps a return value together with any diagnostics generated during parsing.
/// A ParseResult is valid if the fatal flag is false.
template<typename T>
struct ParseResult
{
    T                            value;
    std::vector<ParseDiagnostic> diagnostics;
    bool                         fatal = false;

    bool ok() const { return !fatal; }

    /// Count diagnostics by severity.
    int countWarnings() const
    {
        int n = 0;
        for (const auto& d : diagnostics)
            if (d.severity == Severity::Warning) n++;
        return n;
    }
    int countErrors() const
    {
        int n = 0;
        for (const auto& d : diagnostics)
            if (d.severity == Severity::Error || d.severity == Severity::Fatal) n++;
        return n;
    }
};

// =============================================================================
// DiagnosticCollector — accumulated during one parse session
// =============================================================================

class DiagnosticCollector
{
public:
    explicit DiagnosticCollector(const std::string& sfzFile)
        : _sfzFile(sfzFile)
    {}

    void info   (int line, const std::string& opcode, const std::string& msg)
    { add(Severity::Info,    line, opcode, msg); }

    void warning(int line, const std::string& opcode, const std::string& msg)
    { add(Severity::Warning, line, opcode, msg); }

    void error  (int line, const std::string& opcode, const std::string& msg)
    { add(Severity::Error,   line, opcode, msg); }

    void fatal  (int line, const std::string& opcode, const std::string& msg)
    { add(Severity::Fatal,   line, opcode, msg); _hasFatal = true; }

    bool hasFatal() const { return _hasFatal; }

    bool hasErrors() const
    {
        for (const auto& d : _diag)
            if (d.severity >= Severity::Error) return true;
        return false;
    }

    const std::vector<ParseDiagnostic>& diagnostics() const { return _diag; }

    int warningCount() const
    {
        int n = 0;
        for (const auto& d : _diag)
            if (d.severity == Severity::Warning) n++;
        return n;
    }

    int errorCount() const
    {
        int n = 0;
        for (const auto& d : _diag)
            if (d.severity >= Severity::Error) n++;
        return n;
    }

private:
    void add(Severity sev, int line, const std::string& opcode, const std::string& msg)
    {
        _diag.push_back({sev, _sfzFile, line, opcode, msg});
    }

    std::string                  _sfzFile;
    std::vector<ParseDiagnostic> _diag;
    bool                         _hasFatal = false;
};

} // namespace Frontend
} // namespace Vhl
