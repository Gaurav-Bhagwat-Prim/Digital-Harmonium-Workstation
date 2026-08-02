// =============================================================================
// SfzParser.cpp — VHL Builder Frontend: SFZ → Builder Object Model
// =============================================================================

#include "SfzParser.h"
#include "NoteParser.h"

#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cassert>

namespace fs = std::filesystem;

namespace Vhl {
namespace Frontend {

// =============================================================================
// Public API
// =============================================================================

ParseResult<Bom::BomInstrument> SfzParser::parse(const std::string& sfzFilePath)
{
    SfzLexer lexer;
    if (!lexer.tokenize(sfzFilePath))
    {
        ParseResult<Bom::BomInstrument> r;
        r.fatal = true;
        r.diagnostics.push_back({
            Severity::Fatal,
            fs::path(sfzFilePath).filename().string(),
            -1, "", lexer.errorMessage()
        });
        return r;
    }
    return parse(lexer.tokens(), sfzFilePath);
}

// ---------------------------------------------------------------------------

ParseResult<Bom::BomInstrument> SfzParser::parse(
    const std::vector<Token>& tokens,
    const std::string& sfzFilePath)
{
    ParseResult<Bom::BomInstrument> result;
    Bom::BomInstrument instrument;

    std::string sfzDir  = fs::path(sfzFilePath).parent_path().string();
    std::string sfzBase = fs::path(sfzFilePath).filename().string();
    DiagnosticCollector diag(sfzBase);

    // -------------------------------------------------------------------------
    // 1. Extract metadata from comment lines
    // -------------------------------------------------------------------------
    instrument.metadata = extractMetadata(tokens, sfzFilePath);

    // -------------------------------------------------------------------------
    // 2. Main pass: accumulate group opcodes, build regions on <region> blocks
    // -------------------------------------------------------------------------
    OpcodeMap groupOpcodes;   // current <group> defaults
    OpcodeMap regionOpcodes;  // current <region> opcodes (before merge)
    bool      inRegion   = false;
    int       regionIdx  = 0;
    int       regionLine = 0;

    auto commitRegion = [&]()
    {
        if (!inRegion) return;

        // Merge: group provides defaults, region overrides
        OpcodeMap merged = groupOpcodes;
        for (const auto& kv : regionOpcodes)
            merged[kv.first] = kv.second;

        // Build the BomRegion
        Bom::BomRegion r = buildRegion(merged, regionIdx, regionLine, sfzDir, diag);
        instrument.regions.push_back(std::move(r));
        ++regionIdx;

        regionOpcodes.clear();
        inRegion = false;
    };

    for (const auto& tok : tokens)
    {
        switch (tok.type)
        {
        // -------------------------------------------------------------------
        case TokenType::BlockHeader:
        {
            if (tok.blockName == "region")
            {
                // Commit previous region (if any), start a new one
                commitRegion();
                inRegion   = true;
                regionLine = tok.line;
            }
            else if (tok.blockName == "group")
            {
                // Commit previous region (if any), reset group opcodes
                commitRegion();
                groupOpcodes.clear();
            }
            // Other block names (control, curve, effect, etc.) are ignored
            break;
        }

        // -------------------------------------------------------------------
        case TokenType::OpcodeValue:
        {
            // Route opcode to current context
            if (inRegion)
                regionOpcodes[tok.opcode] = tok.value;
            else
                // Before any <region>, opcodes belong to the current group
                groupOpcodes[tok.opcode] = tok.value;
            break;
        }

        // -------------------------------------------------------------------
        case TokenType::CommentLine:
        case TokenType::EndOfFile:
        default:
            if (tok.type == TokenType::EndOfFile)
                commitRegion();
            break;
        }
    }

    // Commit any trailing region not followed by another block
    commitRegion();

    // -------------------------------------------------------------------------
    // 3. Sort regions by key_lo ascending (Polyphone already does this, but
    //    we enforce it to guarantee Backend lookup-table correctness).
    // -------------------------------------------------------------------------
    std::sort(instrument.regions.begin(), instrument.regions.end(),
              [](const Bom::BomRegion& a, const Bom::BomRegion& b)
              { return a.keyRange.lo < b.keyRange.lo; });

    // Re-assign sequential indices after sort
    for (size_t i = 0; i < instrument.regions.size(); ++i)
        instrument.regions[i].regionIndex = static_cast<int>(i);

    // -------------------------------------------------------------------------
    // 4. Collect results
    // -------------------------------------------------------------------------
    result.value        = std::move(instrument);
    result.diagnostics  = diag.diagnostics();
    result.fatal        = diag.hasFatal();
    return result;
}

// =============================================================================
// Metadata extraction
// =============================================================================

Bom::BomMetadata SfzParser::extractMetadata(const std::vector<Token>& tokens,
                                               const std::string& sfzPath)
{
    Bom::BomMetadata meta;
    meta.sfzPath = sfzPath;

    // Polyphone writes:
    //   "// Sfz exported from a sf2 file with Polyphone"
    //   "// Name      : Harmonium"
    //   "// Author    : ..."
    //   "// Copyright : ..."
    //   "// Date      : 2024/01/15"
    //   "// Comment   : ..."

    for (const auto& tok : tokens)
    {
        if (tok.type != TokenType::CommentLine) continue;
        const std::string& text = tok.commentText;

        auto extractAfterColon = [&](const std::string& prefix) -> std::string
        {
            if (text.size() <= prefix.size()) return {};
            if (text.compare(0, prefix.size(), prefix) != 0) return {};
            std::string val = text.substr(prefix.size());
            // Trim leading whitespace
            size_t s = val.find_first_not_of(" \t");
            return (s == std::string::npos) ? "" : val.substr(s);
        };

        std::string v;
        if      (!(v = extractAfterColon("Name      :")).empty()) meta.name      = v;
        else if (!(v = extractAfterColon("Author    :")).empty()) meta.author    = v;
        else if (!(v = extractAfterColon("Copyright :")).empty()) meta.copyright = v;
        else if (!(v = extractAfterColon("Date      :")).empty()) meta.date      = v;
        else if (!(v = extractAfterColon("Comment   :")).empty()) meta.comment   = v;
    }

    // If no name from comments, use the SFZ filename (without extension)
    if (meta.name.empty())
    {
        fs::path p(sfzPath);
        meta.name = p.stem().string();
    }

    return meta;
}

// =============================================================================
// buildRegion — assemble a BomRegion from a merged opcode map
// =============================================================================

Bom::BomRegion SfzParser::buildRegion(const OpcodeMap& opcodes,
                                        int regionIndex,
                                        int sourceLine,
                                        const std::string& sfzDir,
                                        DiagnosticCollector& diag)
{
    Bom::BomRegion r;
    r.regionIndex = regionIndex;
    r.sourceLine  = sourceLine;

    applySample  (r, opcodes, sfzDir, diag, sourceLine);
    applyKeyRange(r, opcodes, diag, sourceLine);
    applyVelRange(r, opcodes, diag, sourceLine);
    applyPitch   (r, opcodes, diag, sourceLine);
    applyVolume  (r, opcodes, diag, sourceLine);
    applyLoop    (r, opcodes, diag, sourceLine);
    applyVolEnv  (r, opcodes, diag, sourceLine);
    applyModEnv  (r, opcodes, diag, sourceLine);
    applyFilter  (r, opcodes, diag, sourceLine);
    applyModLFO  (r, opcodes, diag, sourceLine);
    applyVibLFO  (r, opcodes, diag, sourceLine);
    applyExclusive(r, opcodes, diag, sourceLine);
    applyEffects (r, opcodes, diag, sourceLine);
    applyVelocity(r, opcodes, diag, sourceLine);
    applyChannel (r, opcodes, diag, sourceLine);

    return r;
}

// =============================================================================
// Per-opcode setters
// =============================================================================

// ---------------------------------------------------------------------------
// Sample path
// ---------------------------------------------------------------------------

void SfzParser::applySample(Bom::BomRegion& r, const OpcodeMap& m,
                              const std::string& sfzDir,
                              DiagnosticCollector& diag, int line)
{
    auto it = m.find("sample");
    if (it == m.end())
    {
        diag.warning(line, "sample", "Region has no sample= opcode — will produce silence");
        return;
    }
    r.sample.sfzRelativePath = it->second;
    r.sample.absolutePath    = resolveSamplePath(it->second, sfzDir);
}

// ---------------------------------------------------------------------------
// Key range
// ---------------------------------------------------------------------------

void SfzParser::applyKeyRange(Bom::BomRegion& r, const OpcodeMap& m,
                               DiagnosticCollector& diag, int line)
{
    // "key=" sets both lo and hi to same value
    if (auto it = m.find("key"); it != m.end())
    {
        auto note = toMidiNote(it->second, diag, line, "key");
        if (note) { r.keyRange.lo = *note; r.keyRange.hi = *note; }
    }
    if (auto it = m.find("lokey"); it != m.end())
    {
        auto note = toMidiNote(it->second, diag, line, "lokey");
        if (note) r.keyRange.lo = *note;
    }
    if (auto it = m.find("hikey"); it != m.end())
    {
        auto note = toMidiNote(it->second, diag, line, "hikey");
        if (note) r.keyRange.hi = *note;
    }
    if (r.keyRange.lo > r.keyRange.hi)
    {
        diag.warning(line, "keyRange", "lokey > hikey — swapping");
        std::swap(r.keyRange.lo, r.keyRange.hi);
    }
}

// ---------------------------------------------------------------------------
// Velocity range
// ---------------------------------------------------------------------------

void SfzParser::applyVelRange(Bom::BomRegion& r, const OpcodeMap& m,
                               DiagnosticCollector& diag, int line)
{
    if (auto it = m.find("lovel"); it != m.end())
        if (auto v = toInt(it->second)) r.velRange.lo = static_cast<uint8_t>(std::clamp(*v, 0, 127));
    if (auto it = m.find("hivel"); it != m.end())
        if (auto v = toInt(it->second)) r.velRange.hi = static_cast<uint8_t>(std::clamp(*v, 0, 127));
    if (r.velRange.lo > r.velRange.hi)
    {
        diag.warning(line, "velRange", "lovel > hivel — swapping");
        std::swap(r.velRange.lo, r.velRange.hi);
    }
}

// ---------------------------------------------------------------------------
// Pitch / tuning
// ---------------------------------------------------------------------------

void SfzParser::applyPitch(Bom::BomRegion& r, const OpcodeMap& m,
                             DiagnosticCollector& diag, int line)
{
    if (auto it = m.find("pitch_keycenter"); it != m.end())
    {
        auto note = toMidiNote(it->second, diag, line, "pitch_keycenter");
        if (note) r.pitch_keycenter = static_cast<uint8_t>(*note);
    }
    if (auto it = m.find("tune"); it != m.end())
        if (auto v = toInt(it->second)) r.tune_cents = v;
    if (auto it = m.find("transpose"); it != m.end())
        if (auto v = toInt(it->second)) r.transpose_semitones = v;
    if (auto it = m.find("pitch_keytrack"); it != m.end())
        if (auto v = toInt(it->second)) r.pitch_keytrack = v;
}

// ---------------------------------------------------------------------------
// Volume and pan
// ---------------------------------------------------------------------------

void SfzParser::applyVolume(Bom::BomRegion& r, const OpcodeMap& m,
                              [[maybe_unused]] DiagnosticCollector& diag,
                              [[maybe_unused]] int line)
{
    if (auto it = m.find("volume"); it != m.end())
        if (auto v = toFloat(it->second)) r.volume_db = v;
    if (auto it = m.find("pan"); it != m.end())
        if (auto v = toFloat(it->second)) r.pan = v;
    if (auto it = m.find("width"); it != m.end())
        if (auto v = toFloat(it->second)) r.width = v;
    if (auto it = m.find("position"); it != m.end())
        if (auto v = toFloat(it->second)) r.position = v;
}

// ---------------------------------------------------------------------------
// Loop and sample playback
// ---------------------------------------------------------------------------

void SfzParser::applyLoop(Bom::BomRegion& r, const OpcodeMap& m,
                            DiagnosticCollector& diag, int line)
{
    // loop_mode
    if (auto it = m.find("loop_mode"); it != m.end())
    {
        const std::string& v = it->second;
        if      (v == "no_loop")          r.loop_mode = Bom::LoopMode::NoLoop;
        else if (v == "loop_continuous")  r.loop_mode = Bom::LoopMode::LoopContinuous;
        else if (v == "loop_sustain")     r.loop_mode = Bom::LoopMode::LoopSustain;
        else if (v == "one_shot")         r.loop_mode = Bom::LoopMode::OneShot;
        else
            diag.warning(line, "loop_mode", "Unknown value: " + v + " — defaulting to no_loop");
    }

    // trigger (release samples from Polyphone's sampleModes==2 export)
    if (auto it = m.find("trigger"); it != m.end())
    {
        const std::string& v = it->second;
        if      (v == "attack")  r.trigger = Bom::TriggerMode::Attack;
        else if (v == "release") r.trigger = Bom::TriggerMode::Release;
        else if (v == "first")   r.trigger = Bom::TriggerMode::First;
        else if (v == "legato")  r.trigger = Bom::TriggerMode::Legato;
        else
            diag.warning(line, "trigger", "Unknown value: " + v + " — defaulting to attack");
    }

    // loop_start, loop_end
    if (auto it = m.find("loop_start"); it != m.end())
        if (auto v = toUInt32(it->second)) r.loop_start_frame = v;
    if (auto it = m.find("loop_end"); it != m.end())
        if (auto v = toUInt32(it->second)) r.loop_end_frame = v;

    // offset, end
    if (auto it = m.find("offset"); it != m.end())
        if (auto v = toUInt32(it->second)) r.start_offset_frames = v;
    if (auto it = m.find("end"); it != m.end())
        if (auto v = toUInt32(it->second)) r.end_frame = v;
}

// ---------------------------------------------------------------------------
// Volume Envelope (ampeg_*)
// ---------------------------------------------------------------------------

void SfzParser::applyVolEnv(Bom::BomRegion& r, const OpcodeMap& m,
                              [[maybe_unused]] DiagnosticCollector& diag,
                              [[maybe_unused]] int line)
{
    auto& e = r.vol_env;
    if (auto it = m.find("ampeg_delay");   it != m.end()) if (auto v = toFloat(it->second)) e.delay_s    = v;
    if (auto it = m.find("ampeg_attack");  it != m.end()) if (auto v = toFloat(it->second)) e.attack_s   = v;
    if (auto it = m.find("ampeg_hold");    it != m.end()) if (auto v = toFloat(it->second)) e.hold_s     = v;
    if (auto it = m.find("ampeg_decay");   it != m.end()) if (auto v = toFloat(it->second)) e.decay_s    = v;
    if (auto it = m.find("ampeg_sustain"); it != m.end()) if (auto v = toFloat(it->second)) e.sustain_pct = v;
    if (auto it = m.find("ampeg_release"); it != m.end()) if (auto v = toFloat(it->second)) e.release_s  = v;
    // SFZ v2 key-scaling extensions
    if (auto it = m.find("ampeg_holdcc133");  it != m.end()) if (auto v = toFloat(it->second)) e.key_to_hold_s  = v;
    if (auto it = m.find("ampeg_decaycc133"); it != m.end()) if (auto v = toFloat(it->second)) e.key_to_decay_s = v;
}

// ---------------------------------------------------------------------------
// Modulation Envelope (pitcheg_* / fileg_*)
// Polyphone writes the same DAHDSR timing to both prefixes simultaneously.
// We read pitcheg_* as primary and fileg_* as fallback for each time stage.
// The two depth parameters (pitcheg_depth, fileg_depth) are stored separately.
// ---------------------------------------------------------------------------

void SfzParser::applyModEnv(Bom::BomRegion& r, const OpcodeMap& m,
                              [[maybe_unused]] DiagnosticCollector& diag,
                              [[maybe_unused]] int line)
{
    auto& e = r.mod_env;

    // Helper: read pitcheg_X, fallback to fileg_X
    auto readTime = [&](const std::string& stem) -> std::optional<float>
    {
        auto it = m.find("pitcheg_" + stem);
        if (it != m.end()) return toFloat(it->second);
        it = m.find("fileg_" + stem);
        if (it != m.end()) return toFloat(it->second);
        return std::nullopt;
    };

    if (auto v = readTime("delay"))   e.delay_s     = v;
    if (auto v = readTime("attack"))  e.attack_s    = v;
    if (auto v = readTime("hold"))    e.hold_s      = v;
    if (auto v = readTime("decay"))   e.decay_s     = v;
    if (auto v = readTime("sustain")) e.sustain_pct = v;
    if (auto v = readTime("release")) e.release_s   = v;

    // Key scaling
    auto readKeyScale = [&](const std::string& stem) -> std::optional<float>
    {
        auto it = m.find("pitcheg_" + stem);
        if (it != m.end()) return toFloat(it->second);
        it = m.find("fileg_" + stem);
        if (it != m.end()) return toFloat(it->second);
        return std::nullopt;
    };
    if (auto v = readKeyScale("holdcc133"))  e.key_to_hold_s  = v;
    if (auto v = readKeyScale("decaycc133")) e.key_to_decay_s = v;

    // Modulation depths
    if (auto it = m.find("pitcheg_depth"); it != m.end())
        if (auto v = toInt(it->second)) r.mod_env_to_pitch_cents  = v;
    if (auto it = m.find("fileg_depth");   it != m.end())
        if (auto v = toInt(it->second)) r.mod_env_to_filter_cents = v;
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

void SfzParser::applyFilter(Bom::BomRegion& r, const OpcodeMap& m,
                              DiagnosticCollector& diag, int line)
{
    auto& f = r.filter;

    if (auto it = m.find("fil_type"); it != m.end())
    {
        if      (it->second == "lpf_2p") f.type = Bom::FilterType::LowPass2P;
        else if (it->second == "hpf_2p") f.type = Bom::FilterType::HighPass2P;
        else if (it->second == "bpf_2p") f.type = Bom::FilterType::BandPass2P;
        else
            diag.warning(line, "fil_type", "Unsupported filter type: " + it->second + " — treating as lpf_2p");
    }

    if (auto it = m.find("cutoff"); it != m.end())
        if (auto v = toFloat(it->second)) f.cutoff_hz = v;
    if (auto it = m.find("resonance"); it != m.end())
        if (auto v = toFloat(it->second)) f.resonance_db = v;
    if (auto it = m.find("fil_veltrack"); it != m.end())
        if (auto v = toInt(it->second)) f.vel_track_cents = v;
    if (auto it = m.find("fil_keytrack"); it != m.end())
        if (auto v = toInt(it->second)) f.key_track_cents = v;
    if (auto it = m.find("fil_keycenter"); it != m.end())
    {
        auto note = toMidiNote(it->second, diag, line, "fil_keycenter");
        if (note) f.key_center = static_cast<uint8_t>(*note);
    }
}

// ---------------------------------------------------------------------------
// Modulation LFO (amplfo_* / fillfo_*)
// ---------------------------------------------------------------------------

void SfzParser::applyModLFO(Bom::BomRegion& r, const OpcodeMap& m,
                              [[maybe_unused]] DiagnosticCollector& diag,
                              [[maybe_unused]] int line)
{
    auto& lfo = r.mod_lfo;

    // Delay — prefer amplfo, fallback fillfo
    {
        auto it = m.find("amplfo_delay");
        if (it == m.end()) it = m.find("fillfo_delay");
        if (it != m.end()) if (auto v = toFloat(it->second)) lfo.delay_s = v;
    }
    // Freq — prefer amplfo, fallback fillfo
    {
        auto it = m.find("amplfo_freq");
        if (it == m.end()) it = m.find("fillfo_freq");
        if (it != m.end()) if (auto v = toFloat(it->second)) lfo.freq_hz = v;
    }
    // Depths
    if (auto it = m.find("amplfo_depth"); it != m.end())
        if (auto v = toFloat(it->second)) lfo.depth_volume_db = v;
    if (auto it = m.find("fillfo_depth"); it != m.end())
        if (auto v = toFloat(it->second)) lfo.depth_filter_cents = v;
}

// ---------------------------------------------------------------------------
// Vibrato LFO (pitchlfo_*)
// ---------------------------------------------------------------------------

void SfzParser::applyVibLFO(Bom::BomRegion& r, const OpcodeMap& m,
                               [[maybe_unused]] DiagnosticCollector& diag,
                               [[maybe_unused]] int line)
{
    auto& lfo = r.vib_lfo;
    if (auto it = m.find("pitchlfo_delay"); it != m.end())
        if (auto v = toFloat(it->second)) lfo.delay_s = v;
    if (auto it = m.find("pitchlfo_freq"); it != m.end())
        if (auto v = toFloat(it->second)) lfo.freq_hz = v;
    if (auto it = m.find("pitchlfo_depth"); it != m.end())
        if (auto v = toInt(it->second)) lfo.depth_cents = v;
}

// ---------------------------------------------------------------------------
// Exclusive class / choke
// ---------------------------------------------------------------------------

void SfzParser::applyExclusive(Bom::BomRegion& r, const OpcodeMap& m,
                                 [[maybe_unused]] DiagnosticCollector& diag,
                                 [[maybe_unused]] int line)
{
    if (auto it = m.find("group"); it != m.end())
        if (auto v = toInt(it->second)) r.exclusive_group = v;
    if (auto it = m.find("off_by"); it != m.end())
        if (auto v = toInt(it->second)) r.off_by = v;
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

void SfzParser::applyEffects(Bom::BomRegion& r, const OpcodeMap& m,
                               [[maybe_unused]] DiagnosticCollector& diag,
                               [[maybe_unused]] int line)
{
    if (auto it = m.find("effect1"); it != m.end())
        if (auto v = toFloat(it->second)) r.reverb_send = v;
    if (auto it = m.find("effect2"); it != m.end())
        if (auto v = toFloat(it->second)) r.chorus_send = v;
}

// ---------------------------------------------------------------------------
// Velocity tracking
// ---------------------------------------------------------------------------

void SfzParser::applyVelocity(Bom::BomRegion& r, const OpcodeMap& m,
                                [[maybe_unused]] DiagnosticCollector& diag,
                                [[maybe_unused]] int line)
{
    if (auto it = m.find("amp_veltrack"); it != m.end())
        if (auto v = toFloat(it->second)) r.amp_veltrack = v;
    if (auto it = m.find("amp_velcurve_1"); it != m.end())
        if (auto v = toFloat(it->second)) r.amp_velcurve_1 = v;
    if (auto it = m.find("amp_velcurve_127"); it != m.end())
        if (auto v = toFloat(it->second)) r.amp_velcurve_127 = v;
}

// ---------------------------------------------------------------------------
// MIDI channel filter
// ---------------------------------------------------------------------------

void SfzParser::applyChannel(Bom::BomRegion& r, const OpcodeMap& m,
                               [[maybe_unused]] DiagnosticCollector& diag,
                               [[maybe_unused]] int line)
{
    if (auto it = m.find("lochan"); it != m.end())
        if (auto v = toInt(it->second)) r.lo_chan = v;
    if (auto it = m.find("hichan"); it != m.end())
        if (auto v = toInt(it->second)) r.hi_chan = v;
}

// =============================================================================
// Value parsing helpers
// =============================================================================

std::optional<float> SfzParser::toFloat(const std::string& s)
{
    try { return std::stof(s); }
    catch (...) { return std::nullopt; }
}

std::optional<int> SfzParser::toInt(const std::string& s)
{
    try { return std::stoi(s); }
    catch (...) { return std::nullopt; }
}

std::optional<uint32_t> SfzParser::toUInt32(const std::string& s)
{
    try
    {
        long long v = std::stoll(s);
        if (v < 0 || v > UINT32_MAX) return std::nullopt;
        return static_cast<uint32_t>(v);
    }
    catch (...) { return std::nullopt; }
}

std::optional<uint8_t> SfzParser::toMidiNote(const std::string& s,
                                               DiagnosticCollector& diag,
                                               int line, const std::string& opcode)
{
    auto v = NoteParser::parse(s);
    if (!v.has_value())
        diag.warning(line, opcode, "Cannot parse note: \"" + s + "\" — opcode ignored");
    return v.has_value() ? std::make_optional(static_cast<uint8_t>(*v)) : std::nullopt;
}

// =============================================================================
// Sample path resolution
// =============================================================================

std::string SfzParser::resolveSamplePath(const std::string& sfzValue,
                                          const std::string& sfzDir)
{
    // Polyphone writes backslash separators on Windows.
    // Normalize to the platform separator.
    std::string normalized = sfzValue;
    for (auto& c : normalized)
        if (c == '\\') c = '/';

    // If already absolute, return as-is
    fs::path p(normalized);
    if (p.is_absolute())
        return p.string();

    // Resolve relative to the SFZ file's directory
    fs::path resolved = fs::path(sfzDir) / p;
    return resolved.lexically_normal().string();
}

} // namespace Frontend
} // namespace Vhl
