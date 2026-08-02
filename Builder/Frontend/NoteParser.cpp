// =============================================================================
// NoteParser.cpp — VHL Builder Frontend: MIDI Note Name → Integer
// =============================================================================

#include "NoteParser.h"
#include <cctype>
#include <stdexcept>

namespace Vhl {
namespace Frontend {

// ---------------------------------------------------------------------------
// noteLetterToIndex
// Maps A–G (case-insensitive) to chromatic index where C=0.
// ---------------------------------------------------------------------------

std::optional<int> NoteParser::noteLetterToIndex(char c)
{
    switch (std::toupper(static_cast<unsigned char>(c)))
    {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default:  return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// parse
// Accepts:
//   Plain integers:  "60", "0", "127"
//   Note names:      "C4", "c4", "C#4", "Db4", "A#-1", "Bb0", "G9"
// ---------------------------------------------------------------------------

std::optional<int> NoteParser::parse(const std::string& rawStr)
{
    if (rawStr.empty())
        return std::nullopt;

    // -----------------------------------------------------------------------
    // 1. Try plain integer first
    // -----------------------------------------------------------------------
    {
        bool allDigits = true;
        size_t start = 0;
        if (rawStr[0] == '-' || rawStr[0] == '+')
            start = 1;
        for (size_t i = start; i < rawStr.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(rawStr[i])))
            {
                allDigits = false;
                break;
            }
        }
        if (allDigits && rawStr.size() > start)
        {
            try
            {
                int v = std::stoi(rawStr);
                if (v >= 0 && v <= 127) return v;
                return std::nullopt; // out of MIDI range
            }
            catch (...) { return std::nullopt; }
        }
    }

    // -----------------------------------------------------------------------
    // 2. Parse note name
    // Format: <NoteLetter>[<Accidental>][<Octave>]
    //   NoteLetter : A–G (case insensitive)
    //   Accidental : '#' (sharp), 'b' (flat), 's' (sharp alias), or none
    //   Octave     : decimal integer, possibly negative (e.g. -1)
    // -----------------------------------------------------------------------

    size_t pos = 0;

    // 2a. Note letter
    auto chromIdx = noteLetterToIndex(rawStr[pos]);
    if (!chromIdx.has_value())
        return std::nullopt;
    int note = *chromIdx;
    ++pos;

    // 2b. Optional accidental
    if (pos < rawStr.size())
    {
        char ac = rawStr[pos];
        if (ac == '#' || ac == 's')  { note += 1; ++pos; }
        else if (ac == 'b')
        {
            // Could be Bb, Cb, etc. — but also the start of octave number if letter is B
            // "b" is always a flat when following a note letter
            note -= 1;
            ++pos;
        }
    }

    // 2c. Octave (required, possibly negative)
    if (pos >= rawStr.size())
        return std::nullopt; // no octave

    std::string octStr = rawStr.substr(pos);
    int octave = 0;
    try
    {
        octave = std::stoi(octStr);
    }
    catch (...)
    {
        return std::nullopt;
    }

    // 2d. Compute MIDI note: MIDI = (octave + 1) * 12 + chromatic
    //     C-1 = 0, C4 = 60, G9 = 127
    int midi = (octave + 1) * 12 + note;

    // Handle accidental wrap-around
    if (midi < 0) midi += 12;
    if (midi > 127) midi -= 12;

    if (midi < 0 || midi > 127)
        return std::nullopt;

    return midi;
}

// ---------------------------------------------------------------------------
// toName
// Converts MIDI note number to a canonical sharp note name (e.g. 60 → "C4").
// ---------------------------------------------------------------------------

std::string NoteParser::toName(int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return "?";

    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    int octave = (midiNote / 12) - 1;
    int index  = midiNote % 12;
    return std::string(names[index]) + std::to_string(octave);
}

} // namespace Frontend
} // namespace Vhl
