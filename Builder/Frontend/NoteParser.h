// =============================================================================
// NoteParser.h — VHL Builder Frontend: MIDI Note Name → Integer
//
// Polyphone writes note names in SFZ using formats like:
//   C4, C#4, Db4, c4, c#4, db4, a#-1, ...
//
// This parser converts any such string to a MIDI integer (0–127).
// It also accepts plain decimal integers (e.g., "60") directly.
//
// Convention used (matching the SFZ specification and Polyphone):
//   C4  = MIDI note 60 (middle C)
//   C-1 = MIDI note 0
//   G9  = MIDI note 127
//
// Formula: MIDI = (octave + 1) * 12 + chromatic_index
//   where chromatic_index: C=0, C#/Db=1, D=2, D#/Eb=3, E=4, F=5,
//                           F#/Gb=6, G=7, G#/Ab=8, A=9, A#/Bb=10, B=11
// =============================================================================

#pragma once

#include <string>
#include <optional>

namespace Vhl {
namespace Frontend {

class NoteParser
{
public:
    /// Parse a MIDI note name or integer string to a MIDI note number.
    /// Returns std::nullopt if the string is not a valid note or integer.
    /// Examples:
    ///   "C4"  → 60      "c4"  → 60
    ///   "C#4" → 61      "Db4" → 61
    ///   "60"  → 60      "127" → 127
    ///   "G9"  → 127     "C-1" → 0
    static std::optional<int> parse(const std::string& s);

    /// Convert a MIDI note number back to a canonical name (e.g., 60 → "C4").
    static std::string toName(int midiNote);

private:
    /// Chromatic index of a note letter (A=9, B=11, C=0, D=2, E=4, F=5, G=7).
    static std::optional<int> noteLetterToIndex(char c);
};

} // namespace Frontend
} // namespace Vhl
