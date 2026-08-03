# Polyphone Internal Architecture
## Pre-Phase 2 Engineering Reference — VHL Builder Design

**Source studied:** Polyphone 2024 (GPL-3.0, Davy Triponney)
**Purpose:** Extract the exact internal architecture of Polyphone's instrument representation and SFZ export pipeline, to be used as the technical blueprint for implementing the VHL Builder.
**Date:** 2026-07-29
**Status:** Complete

---

## Table of Contents

1. [Internal Object Model](#1-internal-object-model)
2. [Instrument Editing Pipeline](#2-instrument-editing-pipeline)
3. [SFZ Export Pipeline](#3-sfz-export-pipeline)
4. [Parameter Resolution](#4-parameter-resolution)
5. [Sample Handling](#5-sample-handling)
6. [Envelope Handling](#6-envelope-handling)
7. [Filters](#7-filters)
8. [LFOs](#8-lfos)
9. [Velocity Layers](#9-velocity-layers)
10. [Region Construction](#10-region-construction)
11. [Unit Conversion Reference](#11-unit-conversion-reference)
12. [VHL Builder Implications](#12-vhl-builder-implications)

---

## 1. Internal Object Model

### Overview

Polyphone's data model maps directly onto the SF2 specification hierarchy. Every entity in a soundfont is represented as a C++ object and addressed by a compound identifier called `EltID`.

### Class Hierarchy

```
SoundfontManager  (singleton, global access gateway)
    │
    └── Soundfonts
            │
            └── Soundfont [one per open .sf2 file]
                    │
                    ├── IndexedElementList<Smpl *>        _smpl[]     (samples)
                    │
                    ├── IndexedElementList<InstPrst *>    _inst[]     (instruments)
                    │   Each InstPrst:
                    │     ├── Division * _globalDivision              (global zone)
                    │     └── IndexedElementList<Division *> _divisions[]  (zones)
                    │
                    └── IndexedElementList<InstPrst *>    _prst[]     (presets)
                        Each InstPrst:
                          ├── Division * _globalDivision
                          └── IndexedElementList<Division *> _divisions[]
```

### The EltID Addressing System

**File:** `sources/core/types/eltid.h`

Every object in the model is addressed by a 4-tuple `EltID`:

```cpp
struct EltID {
    ElementType typeElement;  // What kind of object
    int indexSf2;             // Which soundfont file (0-based)
    int indexElt;             // Which inst/prst/sample
    int indexElt2;            // Which division within inst/prst
    int indexMod;             // Which modulator within a division
};
```

The `ElementType` enum defines every addressable level:

| ElementType | What It Addresses |
|---|---|
| `elementSf2` | The soundfont file itself |
| `elementSmpl` | A sample |
| `elementInst` | An instrument |
| `elementPrst` | A preset |
| `elementInstSmpl` | An instrument zone (inst division containing a sample) |
| `elementPrstInst` | A preset zone (prst division containing an instrument) |
| `elementInstGen` | The global generator zone of an instrument |
| `elementPrstGen` | The global generator zone of a preset |
| `elementInstSmplGen` | Generators of a specific instrument zone |
| `elementPrstInstGen` | Generators of a specific preset zone |
| `elementInstMod` | Modulators of the instrument global zone |
| `elementPrstMod` | Modulators of the preset global zone |
| `elementInstSmplMod` | Modulators of a specific instrument zone |
| `elementPrstInstMod` | Modulators of a specific preset zone |

This addressing system means every piece of data — from a sample name to a single generator value in a specific zone of a specific instrument — is uniquely addressable by one `EltID`.

### Soundfont Class

**File:** `sources/core/model/soundfont.h`

```
Soundfont {
    SfVersionTag _IFIL       // SF2 format version (e.g. 2.01)
    QString _ISNG            // Target sound engine
    QString _INAM            // Bank name
    QString _IENG, _IPRD, _ICOP, _ICMT, _ISFT   // Metadata strings
    QString _fileNameInitial // Original file path
    quint16 _wBpsInit        // Bit depth when opened (16 or 24)
    quint16 _wBpsSave        // Bit depth for saving (16 or 24)

    IndexedElementList<Smpl *>    _smpl    // All samples
    IndexedElementList<InstPrst*> _inst    // All instruments
    IndexedElementList<InstPrst*> _prst    // All presets
}
```

The `Soundfont` class owns everything. There is no separate "SoundFont" parser class at runtime — the manager reads the file and populates these lists directly.

### InstPrst Class (Instrument & Preset)

**File:** `sources/core/model/instprst.h`

```
InstPrst {
    QString _name                          // Display name (max 20 chars)
    Division * _globalDivision             // Global zone (no sample ref)
    IndexedElementList<Division*> _divisions  // Zones (each with sample/inst ref)
    int _extraFields[5]                    // Preset-only: wPreset, wBank, etc.
}
```

The same `InstPrst` class is used for both instruments and presets. The `ElementType` in the EltID distinguishes them at runtime.

- **Instrument**: `_divisions` are `elementInstSmpl` zones, each referencing a `Smpl`
- **Preset**: `_divisions` are `elementPrstInst` zones, each referencing an `InstPrst` instrument

### Division Class (Zone)

**File:** `sources/core/model/division.h`

The `Division` is where individual generator values are stored. It is the lowest-level editable unit.

```
Division {
    AttributeValue * _attributeValues   // Array of 60 AttributeValue slots
    bool * _attributeSet                // Parallel bool array: is each slot set?
    IndexedElementList<Modulator*> _modulators  // Modulator list for this zone
}
```

Key design: `_attributeSet[i]` is `false` by default. When a generator is set, `_attributeSet[i]` becomes `true`. The `SoundfontManager::isSet(id, champ)` check uses this flag to distinguish "explicitly set" from "using default."

This is how Polyphone implements inheritance: only explicitly-set generators override the inherited value.

### Smpl Class (Sample)

**File:** `sources/core/model/smpl.h`

```
Smpl {
    Sound _sound                  // The actual audio data wrapper
    quint16 _wSampleLink          // Index of the linked stereo partner (or 0)
    SFSampleLink _sfSampleType    // monoSample / leftSample / rightSample / linkedSample
    QString _name                 // Sample name (max 20 chars)
}
```

The `Sound` object (from the sound engine) stores PCM data and metadata:
- `dwLength`: total frame count
- `dwStartLoop`: loop start frame
- `dwEndLoop`: loop end frame
- `dwSampleRate`: original sample rate
- `byOriginalPitch`: root MIDI key (0–127)
- `chPitchCorrection`: fine tune in cents (−99 to +99)

### Modulator Structure

**File:** `sources/core/types/modulatordata.h`

```
ModulatorData {
    SFModulator srcOper      // Primary source (general controller or CC)
    quint16     destOper     // Destination generator (AttributeType index)
    SFModulator amtSrcOper   // Secondary source (amount scaling)
    SFTransform transOper    // Transform: linear or absolute_value
    qint16      amount       // Modulation amount
    quint16     index        // Global index (for cross-modulator referencing)
}

SFModulator {
    quint8 Type  : 6          // typeLinear / typeConcave / typeConvex / typeSwitch
    bool isBipolar : 1        // false=unipolar (0..1), true=bipolar (-1..+1)
    bool isDescending : 1     // false=ascending, true=descending
    bool CC : 1               // false=GeneralController, true=MIDI CC number
    quint8 Index : 7          // GeneralController enum or CC number (0–127)
}
```

The 10 SF2 mandatory default modulators are stored as a static array in `ModulatorData::s_defaultMods[10]`. They include: velocity→attenuation, velocity→filter cutoff, pitch wheel→pitch, CC7→attenuation, CC10→pan, CC11→attenuation, CC91→reverb send, CC93→chorus send, poly pressure→filter, key number→filter.

### Object Relationship Diagram

```
SoundfontManager (singleton)
         │
         │ manages N soundfonts
         ▼
    Soundfont
         │
    ┌────┴─────────────────┐
    │                      │
    ▼                      ▼
Smpl[]                InstPrst[]  (instruments)
(audio data)               │
                      ┌────┴──────────┐
                      │               │
                      ▼               ▼
               GlobalDivision    Division[] (zones)
               (no sample ref)    │
                                  │ stores
                                  ▼
                            AttributeValue[60]  (generators)
                            bool[60]            (isSet flags)
                            Modulator[]

    InstPrst[]  (presets)
         │
    ┌────┴──────────┐
    │               │
    ▼               ▼
GlobalDivision   Division[] (zones)
                  │ references →  InstPrst (instrument)
                  stores:
                  AttributeValue[60]  (additive preset generators)
                  bool[60]
                  Modulator[]
```

---

## 2. Instrument Editing Pipeline

### How GUI Changes Reach the Data Model

When a user changes any parameter (attack time, filter cutoff, key range, etc.) in the Polyphone editor, the change flows through this path:

```
User action in editor widget
        │
        ▼
Editor widget calls SoundfontManager::set(EltID, AttributeType, AttributeValue)
        │
        ▼
SoundfontManager resolves EltID to the specific Division
        │
        ▼
Division::setGen(AttributeType champ, AttributeValue value)
        │
        ├─ _attributeValues[champ] = value    (store the raw value)
        └─ _attributeSet[champ] = true         (mark as explicitly set)
        │
        ▼
ActionManager records the change as an Action (for undo/redo)
        │
        ▼
SoundfontManager emits signals → editor views update
```

The `SoundfontManager::set()` call is the single write point. There is no direct access to `Division` from the GUI — everything goes through the manager.

To **reset** a generator back to default (clear the override):
```
SoundfontManager::reset(EltID, AttributeType)
    → Division::resetGen(champ)
    → _attributeSet[champ] = false
```

To **read** a generator:
```
SoundfontManager::get(EltID, AttributeType) → AttributeValue
    → Division::getGen(champ)
    → returns _attributeValues[champ]
    (the caller must check isSet() first if inheritance matters)
```

To **check if set**:
```
SoundfontManager::isSet(EltID, AttributeType) → bool
    → Division::isSet(champ)
    → returns _attributeSet[champ]
```

### What "Set" Means

A generator being "set" means the user has explicitly assigned it a value. An "unset" generator uses the SF2 default value. This distinction is critical for inheritance: only set generators participate in the zone's contribution to the merged parameter list.

When editing attack time to 0.5 seconds:
1. The GUI calls `sf2->set(idInstSmpl, champ_attackVolEnv, value)` where `value` is the timecent representation of 0.5 s
2. `_attributeValues[34] = value` (generator 34 = `champ_attackVolEnv`)
3. `_attributeSet[34] = true`

The Attribute class translates between "real values" (seconds, Hz, dB) and "stored values" (SF2 binary representation: timecents, absolute cents, centibels) bidirectionally:

```cpp
// Stored → Real:
Attribute::toRealValue(champ_attackVolEnv, false, storedValue)
    → FastMaths::fastPow2(storedValue.shValue / 1200.0)  // seconds

// Real → Stored:
Attribute::fromRealValue(champ_attackVolEnv, false, realSeconds)
    → storedValue.shValue = round(1200 * log2(realSeconds))  // timecents
```

The GUI always works in real units. The binary file stores SF2 units. The `Attribute` class is the boundary.

---

## 3. SFZ Export Pipeline

### Classes Involved

| Class | File | Role |
|---|---|---|
| `ConversionSfz` | `output/sfz/conversion_sfz.cpp` | Top-level orchestrator |
| `SfzParamList` | `output/sfz/sfzparamlist.cpp` | Inheritance resolution + parameter merging |
| `BalanceParameters` | `output/sfz/balanceparameters.cpp` | Pan and attenuation computation for stereo |
| `SfzWriter` | `output/sfz/sfzwriter.cpp` | Low-level SFZ text file writer with factorization |

### Complete Export Pipeline

```
ConversionSfz::convert(dirPath, idSf2)
    │
    ├─ Create output directory + samples/ subdirectory
    ├─ Detect bank diversity (for directory structure)
    │
    └─ For each preset in the soundfont:
            │
            ConversionSfz::exportPrst(dir, presetId)
                │
                ├─ Create SfzWriter (one .sfz file per preset)
                ├─ writeEntete() → write comment header with SF2 metadata
                │
                └─ For each preset zone (elementPrstInst):
                        │
                        ├─ STAGE 1: Build preset parameter list
                        │   SfzParamList paramPrst(sf2, presetZoneId)
                        │   Loads: preset zone generators + preset global generators
                        │   (zone values take priority — first-loaded wins)
                        │
                        ├─ STAGE 2: Resolve instrument reference
                        │   idInst = preset_zone.get(champ_instrument)
                        │   (the EltID of the referenced instrument)
                        │
                        ├─ STAGE 3: Build instrument global parameter list
                        │   SfzParamList paramInst(sf2, &paramPrst, idInst)
                        │   Loads: inst global generators
                        │   Merges: preset params additively
                        │
                        ├─ writeGroup(paramInst)
                        │   → <group> block with shared parameters
                        │
                        └─ Sort instrument zones by keyRange.byLo (ascending)
                           For each instrument zone (elementInstSmpl):
                                │
                                ├─ Skip if key/vel range outside preset range
                                ├─ Skip if already processed (stereo partner)
                                │
                                ├─ STAGE 4: Get or export sample
                                │   getSamplePath(idSmpl) →
                                │     Detect mono/stereo from sfSampleType
                                │     Find stereo partner via wSampleLink
                                │     Export WAV file to samples/ directory
                                │     Return relative path string
                                │
                                ├─ STAGE 5: Build zone parameter list
                                │   SfzParamList paramInstSmpl(sf2, &paramPrst, idInstSmpl)
                                │   Loads: zone generators + inst global generators
                                │   Merges: preset params (additive where applicable)
                                │   Performs: offset merging, root key injection,
                                │             fine tune summing, LFO adaptation,
                                │             keynum2 adaptation, modLFO→vibLFO promotion
                                │
                                ├─ BalanceParameters: compute SFZ volume + pan + width
                                │
                                └─ writeRegion(paramInstSmpl, samplePath, &balance)
                                   → <region> block with zone-specific parameters
    │
    SfzWriter::write()
        → factorizeOpCodes(): move common parameters up to <group>
        → cleanOpCodes(): remove orphaned LFO frequency opcodes
        → write all blocks to disk
```

### writeElement() — The SF2→SFZ Opcode Translation Table

**File:** `conversion_sfz.cpp` lines 330–469

This function is the complete SF2 generator → SFZ opcode conversion. Every generator is explicitly handled:

| SF2 Generator | SFZ Opcode(s) | Conversion |
|---|---|---|
| `champ_fineTune` | `tune` | rounded integer (cents) |
| `champ_coarseTune` | `transpose` | rounded integer (semitones) |
| `champ_scaleTuning` | `pitch_keytrack` | rounded integer (0–100) |
| `champ_startloopAddrsOffset` | `loop_start` | rounded integer (frames) |
| `champ_endloopAddrsOffset` | `loop_end` | value - 1 (exclusive→inclusive) |
| `champ_startAddrsOffset` | `offset` | rounded integer (frames) |
| `champ_endAddrsOffset` | `end` | value - 1 |
| `champ_pan` | `pan` | value × 2 (SF2: ±500 → SFZ: ±1000) |
| `champ_initialAttenuation` | `volume` | −value × DB_SF2_TO_REAL_DB |
| `champ_initialFilterQ` | `resonance` | direct (dB) |
| `champ_sustainModEnv` | `fileg_sustain` + `pitcheg_sustain` | 100 − value (SF2: 0=max → SFZ: 100=max) |
| `champ_delayModEnv` | `pitcheg_delay` + `fileg_delay` | direct (seconds) |
| `champ_attackModEnv` | `pitcheg_attack` + `fileg_attack` | direct (seconds) |
| `champ_holdModEnv` | `pitcheg_hold` + `fileg_hold` | direct (seconds) |
| `champ_decayModEnv` | `pitcheg_decay` + `fileg_decay` | direct (seconds) |
| `champ_releaseModEnv` | `pitcheg_release` + `fileg_release` | direct (seconds) |
| `champ_modEnvToPitch` | `pitcheg_depth` | rounded (cents) |
| `champ_modEnvToFilterFc` | `fileg_depth` | rounded (cents) |
| `champ_keynumToModEnvHold` | `pitcheg_holdcc133` + `fileg_holdcc133` | direct |
| `champ_keynumToModEnvDecay` | `pitcheg_decaycc133` + `fileg_decaycc133` | direct |
| `champ_delayModLFO` | `amplfo_delay` + `fillfo_delay` | direct (seconds) |
| `champ_freqModLFO` | `amplfo_freq` + `fillfo_freq` | direct (Hz) |
| `champ_modLfoToVolume` | `amplfo_depth` | direct (dB) |
| `champ_modLfoToFilterFc` | `fillfo_depth` | direct (cents) |
| `champ_modLfoToPitch` | **NOT EXPORTED** (promoted to vibLFO instead) | see Section 8 |
| `champ_delayVibLFO` | `pitchlfo_delay` | direct (seconds) |
| `champ_freqVibLFO` | `pitchlfo_freq` | direct (Hz) |
| `champ_vibLfoToPitch` | `pitchlfo_depth` | rounded (cents) |
| `champ_reverbEffectsSend` | `effect1` | direct (0–100) |
| `champ_chorusEffectsSend` | `effect2` | direct (0–100) |
| `champ_delayVolEnv` | `ampeg_delay` | direct (seconds) |
| `champ_attackVolEnv` | `ampeg_attack` | direct (seconds) |
| `champ_sustainVolEnv` | `ampeg_sustain` | `100 × pow10(−dB/20)` (centibels→percent) |
| `champ_holdVolEnv` | `ampeg_hold` | direct (seconds) |
| `champ_decayVolEnv` | `ampeg_decay` | direct (seconds) |
| `champ_releaseVolEnv` | `ampeg_release` | direct (seconds) |
| `champ_keynumToVolEnvHold` | `ampeg_holdcc133` | direct |
| `champ_keynumToVolEnvDecay` | `ampeg_decaycc133` | direct |
| `champ_overridingRootKey` | `pitch_keycenter` | MIDI note name string |
| `champ_keynum` | `pitch_keycenter` + `pitch_keytrack=0` | Fixed pitch: root = played key, scale = 0 |
| `champ_exclusiveClass` | `group` + `off_by` | same integer value for both |
| `champ_initialFilterFc` | `fil_type=lpf_2p` + `cutoff` | rounded (Hz) |
| `champ_keyRange` | `lokey` + `hikey` (or `key` if lo=hi) | MIDI note names |
| `champ_velRange` | `lovel` + `hivel` | integers 0–127 |
| `champ_sampleModes=0` | `loop_mode=no_loop` | |
| `champ_sampleModes=1` | `loop_mode=loop_continuous` | |
| `champ_sampleModes=2` | `loop_mode=one_shot` + `trigger=release` | Release sample |
| `champ_sampleModes=3` | `loop_mode=loop_sustain` | |
| `champ_velocity` | `amp_velcurve_1` + `amp_velcurve_127` | Fixed velocity: both = value/127 |

### The SfzWriter Factorization Step

**File:** `sfzwriter.cpp`

Before writing to disk, `SfzWriter::write()` performs two optimizations:

**1. `factorizeOpCodes(group, regions)`:**
Scans all regions in a group. Any opcode that has the **same value in every region** (including those that would inherit the group default) is moved up to the `<group>` block and removed from all `<region>` blocks. This produces compact SFZ files where shared parameters appear only once.

**2. `cleanOpCodes(group, regions)`:**
Removes orphaned LFO frequency opcodes. If `amplfo_freq` is in the group but no region has `amplfo_depth`, the frequency is meaningless and is deleted. Same for `fillfo_freq`/`fillfo_depth` and `pitchlfo_freq`/`pitchlfo_depth`.

**Special writes:** When writing blocks, `lochan`/`hichan`, `lokey`/`hikey`, and `lovel`/`hivel` are always written as pairs on one line.

---

## 4. Parameter Resolution

### The Core Algorithm

Parameter resolution is performed by `SfzParamList`. There are three constructor paths, one for each context:

**Constructor 1: Preset zone parameters**
```
SfzParamList(sf2, presetZoneId)   [id.typeElement == elementPrstInst]
    1. loadAttributes(sf2, elementPrstInstGen)   // zone generators (highest priority)
    2. loadAttributes(sf2, elementPrstGen)        // global generators (lower priority, only if not yet loaded)
    3. loadModulators(sf2, elementPrstInstMod)
    4. loadModulators(sf2, elementPrstMod)
```

**Constructor 2: Instrument global parameters (merged with preset)**
```
SfzParamList(sf2, &paramPrst, idInst)   [idInst.typeElement == elementInst]
    1. loadAttributes(sf2, elementInstGen)   // inst global generators
    2. loadModulators(sf2, elementInstMod)   // inst global modulators (+ default modulators)
    3. merge(paramPrst): for every preset parameter, merge additively
    4. Add default values for: attackVolEnv, freqModLFO, freqVibLFO, initialFilterFc, sampleModes
    5. Apply limits to all values
```

**Constructor 3: Instrument zone parameters (merged with preset + inst global)**
```
SfzParamList(sf2, &paramPrst, idInstSmpl)   [idInstSmpl.typeElement == elementInstSmpl]
    1. loadAttributes(sf2, elementInstSmplGen)    // zone generators (highest priority)
    2. loadModulators(sf2, elementInstSmplMod)
    3. loadAttributes(sf2, elementInstGen)         // inst global generators (fill gaps)
    4. Inject root key from sample: champ_overridingRootKey = sample.byOriginalPitch
    5. Sum fine tune from sample: champ_fineTune += sample.chPitchCorrection
    6. mix(coarseOffset, fineOffset):
       Merge coarse+fine sample offset generators into single absolute values
    7. If loop_start == loop_end: remove both (no loop)
    8. If start==0 and end==total_frames: remove both (full sample used)
    9. adaptKeynum2(): resolve key-number-to-time-scaling into effective times
    10. adaptLfo(): propagate global division LFO parameters to zone if needed
    11. merge(paramPrst, zone-only): merge preset params only where zone already has the key
    12. processDescendingVelocityModulators(): fix ascending/descending polarity
    13. Apply limits
    14. Volume correction for modLfoToVolume
    15. Prepend ordering: keyRange, overridingRootKey, keynum, velRange
```

### The `loadAttributes()` Function — First-Loaded-Wins

**File:** `sfzparamlist.cpp` lines 434–503

```pseudocode
loadAttributes(sf2, id):
    for each generator set in this element:
        if generator NOT already in _attributes:   // first-loaded wins
            _attributes << generator
            _attributeValues << toRealValue(generator, sf2.get(parent, generator))
```

This "first loaded wins" rule is what implements zone-over-global priority. The zone generators are loaded first. When global generators are loaded next, any generator already present is silently ignored.

**Important exception:** Global division offset generators (`startAddrsOffset`, `startloopAddrsOffset`, etc.) are explicitly excluded from the global-division load. They are only loaded separately via `getGlobalValue()` because they must be **added** to zone offsets, not override them.

### The Three Merge Behaviors

When `merge(attributeType, value)` is called to fold preset parameters into instrument parameters, the behavior depends on the generator type:

| Merge Type | Generators | Rule |
|---|---|---|
| **Additive** | fineTune, coarseTune, initialAttenuation, pan, scaleTuning, initialFilterQ, reverbEffectsSend, chorusEffectsSend, all sustain levels, all keynumTo... values, all modulation depths and envelope modulation routing | `existing += preset_value` |
| **Override** | keynum, overridingRootKey, velocity, exclusiveClass, sampleModes | `existing = preset_value` |
| **Multiplicative** | initialFilterFc, all envelope times (delay/attack/hold/decay/release), all LFO delays and frequencies | `existing *= preset_value` |
| **Intersection** | keyRange, velRange | `[max(lo1,lo2), min(hi1,hi2)]` (narrowest overlap) |

**Source:** `sfzparamlist.cpp` lines 588–651, the `merge()` function's switch statement.

### Priority Hierarchy Summary

```
For any generator G in an instrument zone:

Priority order (highest to lowest):
  1. Instrument zone generator (elementInstSmplGen)
  2. Instrument global generator (elementInstGen)        ← fills gaps only
  3. Sample properties (byOriginalPitch, chPitchCorrection)  ← injected after load
  4. SF2 defaults                                        ← never stored, used when absent

Then merged with preset:
  5. Preset zone generator   (additive/override/multiplicative/intersection)
  6. Preset global generator (additive/override/multiplicative/intersection)
```

### The Preset Zone Inclusion Check

Before generating a region for an instrument zone, Polyphone checks that the zone's key/vel range overlaps the preset zone's range (`isIncluded()`):

```pseudocode
isIncluded(paramPrst, idInstSmpl):
    prstMinKey, prstMaxKey = preset key range (default 0–127)
    prstMinVel, prstMaxVel = preset vel range (default 0–127)
    instMinKey, instMaxKey = inst zone key range (if set)
    instMinVel, instMaxVel = inst zone vel range (if set)

    return (instMinKey <= prstMaxKey AND instMaxKey >= prstMinKey AND
            instMinVel <= prstMaxVel AND instMaxVel >= prstMinVel)
```

Zones completely outside the preset's range are silently excluded.

---

## 5. Sample Handling

### Internal Representation

**File:** `sources/core/model/smpl.h`

Each sample is a `Smpl` object with:
- `Sound _sound`: wraps the PCM data and all SF2 sample header fields
- `quint16 _wSampleLink`: index of the linked sample (for stereo pairs, or 0 for mono)
- `SFSampleLink _sfSampleType`:
  - `monoSample (1)` — standalone mono
  - `leftSample (4)` — left channel of a stereo pair
  - `rightSample (2)` — right channel of a stereo pair
  - `linkedSample (8)` — linked to another sample (not commonly used)
  - `RomMonoSample (0x8001)`, `RomLeftSample (0x8004)`, `RomRightSample (0x8002)` — ROM samples

**Key metadata fields** (from `Sound`):

| Field | Type | Description |
|---|---|---|
| `dwLength` | uint32_t | Total PCM frame count |
| `dwStartLoop` | uint32_t | Loop start frame (absolute) |
| `dwEndLoop` | uint32_t | Loop end frame (absolute, exclusive) |
| `dwSampleRate` | uint32_t | Original sample rate in Hz |
| `byOriginalPitch` | uint8_t | Root MIDI key (0–127) |
| `chPitchCorrection` | int8_t | Fine tune offset in cents (−99 to +99) |

### Stereo Pair Detection

**File:** `conversion_sfz.cpp` lines 529–631

During SFZ export, `getSamplePath()` detects and handles stereo pairs:

```pseudocode
getSamplePath(idSmpl):
    typeSample = sf2.get(idSmpl, champ_sfSampleType)

    if typeSample == leftSample or linkedSample:
        sampleChannel = -1   // Left
    else if typeSample == rightSample:
        sampleChannel = 1    // Right
    else:
        sampleChannel = 0    // Mono

    if sampleChannel != 0:
        linkedSampleId = findOtherChannel(idSmpl)
        if linkedSampleId == -1:
            sampleChannel = 0   // Partner broken, treat as mono

    if stereo:
        // Export both channels as a single stereo WAV file
        // Left channel first: writer.write(leftSound, rightSound)
        // Name = common prefix of both sample names (trim trailing L/R suffix)
    else:
        // Export mono WAV
        writer.write(monoSound)
```

**Stereo validity check** (`findOtherChannel()`):
Two samples are only exported as a genuine stereo pair if all of the following match:
- `dwStartLoop`
- `dwEndLoop`
- `dwLength`
- `chPitchCorrection`
- `byOriginalPitch`
- `dwSampleRate`

If any field differs, the two channels are treated as independent mono samples.

### Sample Name Generation

The stereo WAV filename is derived from both sample names by removing their trailing L/R suffix:
- `"ReedA4L"` + `"ReedA4R"` → `"ReedA4"` (trimmed 1 char)
- `"Violin_L"` + `"Violin_R"` → `"Violin_"` (trimmed 2 chars including separator)
- `"Sample(L)"` + `"Sample(R)"` → `"Sample"` (trimmed 3 chars including parens)

### What Is Exported Into SFZ

From each sample, SFZ receives:
- `sample=path/to/file.wav` — the WAV file path (relative)
- `pitch_keycenter=C4` — from `byOriginalPitch`
- `loop_start=N` — from `dwStartLoop` (if loop enabled in zone)
- `loop_end=N-1` — from `dwEndLoop - 1` (exclusive to inclusive)
- `offset=N` — from `startAddrsOffset` (if non-zero, non-full-sample)
- `end=N-1` — from `endAddrsOffset - 1` (if non-zero, non-full-sample)
- Implicit: sample rate baked into WAV file header

---

## 6. Envelope Handling

### Storage Format

All envelope time parameters are stored in **timecents** (SF2 unit) as `int16_t` in the division's generator array. The sustain levels use different units:
- `champ_sustainVolEnv`: centibels of attenuation (0 = full level, 1440 = silence)
- `champ_sustainModEnv`: percentage (0 = 0%, 1000 = 100% in tenths)

### The `toRealValue()` Conversion

**File:** `attribute.cpp` lines 131–137

```cpp
// For all time generators:
case champ_delayModEnv: case champ_attackModEnv: ... case champ_delayVibLFO:
    realValue = FastMaths::fastPow2(0.000833333f * storedValue.shValue);
    // 0.000833333 = 1/1200
    // Result: seconds, e.g. stored=0 → 1s, stored=-12000 → 0.001s (1ms), stored=5000 → 18.4s
```

**Timecent to seconds formula:**
```
seconds = 2^(timecents / 1200)
timecents = 1200 × log2(seconds)
```

Default timecent values and their second equivalents:
| Stored (timecents) | Real (seconds) |
|---|---|
| −12000 | 0.001 s (1 ms) |
| −7200 | 0.0156 s |
| 0 | 1 s |
| 5000 | 18.4 s |
| −32768 (default for delays) | ~0.001 ms (effectively 0) |

### Volume Envelope — All 6 Generators

| SF2 Generator | Storage | SFZ Opcode | Unit in SFZ |
|---|---|---|---|
| `champ_delayVolEnv` | timecents (int16) | `ampeg_delay` | seconds |
| `champ_attackVolEnv` | timecents (int16) | `ampeg_attack` | seconds |
| `champ_holdVolEnv` | timecents (int16) | `ampeg_hold` | seconds |
| `champ_decayVolEnv` | timecents (int16) | `ampeg_decay` | seconds |
| `champ_sustainVolEnv` | centibels (int16, /10) | `ampeg_sustain` | percent (0–100, via `100×pow10(-dB/20)`) |
| `champ_releaseVolEnv` | timecents (int16) | `ampeg_release` | seconds |

**Sustain conversion** (`conversion_sfz.cpp` line 397):
```cpp
case champ_sustainVolEnv:
    _sfzWriter->addLine("ampeg_sustain", dbToPercent(value));
    // dbToPercent(dB) = 100 × pow(10, -dB / 20)
    // Meaning: SF2 attenuation in centibels/10 dB → SFZ linear percent 0–100
    // 0 centibels → 100% sustain (full level)
    // 144 centibels (14.4 dB) → ~20% sustain
    // 1440 centibels (144 dB) → 0% sustain (silence)
```

### Modulation Envelope — All 6 Generators

| SF2 Generator | SFZ Opcode | Notes |
|---|---|---|
| `champ_delayModEnv` | `pitcheg_delay` + `fileg_delay` | One generator → two SFZ opcodes |
| `champ_attackModEnv` | `pitcheg_attack` + `fileg_attack` | |
| `champ_holdModEnv` | `pitcheg_hold` + `fileg_hold` | |
| `champ_decayModEnv` | `pitcheg_decay` + `fileg_decay` | |
| `champ_sustainModEnv` | `pitcheg_sustain` + `fileg_sustain` | SF2 is 0=max; SFZ is 100=max: `100-value` |
| `champ_releaseModEnv` | `pitcheg_release` + `fileg_release` | |

SF2 has a single modulation envelope. SFZ has separate pitch and filter envelopes. Polyphone exports the single SF2 mod env to both SFZ destinations simultaneously.

### Key Scaling

`champ_keynumToVolEnvHold` / `champ_keynumToVolEnvDecay` are exported as:
```
ampeg_holdcc133=value    // SFZ v2 extension — cc133 = MIDI key number pseudo-CC
ampeg_decaycc133=value
```

The suffix `// sfz v2` is added to these lines in the output (see `sfzwriter.cpp` line 211).

However, `adaptKeynum2()` may convert the key scaling into effective absolute times if the zone has only a single key (minKey == maxKey), eliminating the key scaling entirely and replacing it with a single time value.

---

## 7. Filters

### Storage Format

| Generator | Stored Unit | Real Unit |
|---|---|---|
| `champ_initialFilterFc` | absolute cents (int16) | Hz |
| `champ_initialFilterQ` | tenths of dB (int16, /10) | dB |

### The Absolute Cents → Hz Conversion

**File:** `attribute.cpp` lines 165–170

```cpp
case champ_initialFilterFc:
    if (isPrst)
        realValue = FastMaths::fastPow2(storedValue.shValue / 1200.0);
        // For presets: result is a dimensionless ratio (multiplicative merge)
    else
        realValue = FastMaths::fastPow2(storedValue.shValue / 1200.0) * 8.176;
        // For instruments: actual Hz value
        // 8.176 Hz is the frequency of MIDI note 0 in equal temperament
        // So: Hz = 8.176 × 2^(absoluteCents / 1200)
```

**Important observation:** The filter cutoff conversion treats instrument and preset values differently. For instruments it produces Hz. For presets it produces a ratio. When the preset value is merged multiplicatively with the instrument value, the result is `instrumentHz × presetRatio` which is still in Hz. This is the correct SF2 additive-by-multiplication behavior for time-domain and frequency-domain parameters.

### SFZ Export

```cpp
case champ_initialFilterFc:
    _sfzWriter->addLine("fil_type", "lpf_2p");   // Always exported as 2-pole low-pass
    _sfzWriter->addLine("cutoff", qRound(value)); // Hz, integer
    break;
```

Polyphone always exports as `lpf_2p`. The SF2 spec only defines a low-pass filter. No other filter types are supported in the SF2 format.

### Filter Resonance

```cpp
case champ_initialFilterQ:
    _sfzWriter->addLine("resonance", value);  // dB, float (not rounded)
```

Stored as `int16_t` in tenths of dB. `toRealValue()` divides by 10 to get dB. No conversion needed for SFZ (both use dB).

---

## 8. LFOs

### Two LFO Types in SF2

SF2 has two LFO sources:
- **Modulation LFO** (`champ_modLFO*`): can modulate pitch, filter cutoff, and volume simultaneously
- **Vibrato LFO** (`champ_vibLFO*`): can only modulate pitch

Each has:
- Delay: time before the LFO starts oscillating
- Frequency: oscillation rate in Hz

### Storage and Conversion

| Generator | Stored | Real |
|---|---|---|
| `champ_freqModLFO` | absolute cents (int16) | Hz (same formula as filter: 8.176 × 2^(cents/1200)) |
| `champ_freqVibLFO` | absolute cents (int16) | Hz |
| `champ_delayModLFO` | timecents (int16) | seconds |
| `champ_delayVibLFO` | timecents (int16) | seconds |
| `champ_modLfoToPitch` | cents (int16) | cents (direct) |
| `champ_modLfoToFilterFc` | cents (int16) | cents (direct) |
| `champ_modLfoToVolume` | centibels (int16, /10) | dB |
| `champ_vibLfoToPitch` | cents (int16) | cents (direct) |

### The modLfoToPitch → vibLFO Promotion Algorithm

**File:** `sfzparamlist.cpp` lines 194–243

This is a critical transformation during parameter resolution. SFZ has no direct equivalent of SF2's `modLfoToPitch`. Polyphone resolves this by **promoting the mod LFO to the vib LFO** when `modLfoToPitch` is set and `vibLfoToPitch` is not:

```pseudocode
if _attributes contains champ_modLfoToPitch AND NOT champ_vibLfoToPitch:
    // Copy modLfoToPitch depth to vibLfoToPitch
    _attributes << champ_vibLfoToPitch
    _attributeValues << modLfoToPitch_value

    // Copy mod LFO frequency and delay to vib LFO (if not already set)
    if delayModLFO is set: copy to delayVibLFO
    if freqModLFO is set:  copy to freqVibLFO

    // Remove modLfoToPitch from the list
    remove champ_modLfoToPitch

    // If mod LFO now has no remaining destinations (no volume, no filter):
    if NOT modLfoToVolume AND NOT modLfoToFilterFc:
        also remove delayModLFO and freqModLFO
        // The mod LFO is completely gone, replaced by vib LFO
```

**Why this exists:** SFZ's `pitchlfo_depth` maps to Polyphone's `vibLfoToPitch`. There is no SFZ opcode for "modLFO→pitch." When a SF2 instrument uses the modLFO to modulate pitch (without also modulating volume or filter), Polyphone converts it to a vibLFO in the SFZ export.

**Consequence for the VHL Builder:** When parsing SFZ, the `pitchlfo_*` opcodes may originate from either the original SF2 vibLFO or a promoted modLFO. They are always treated identically once in SFZ.

### SFZ Export Mapping

| SF2 Source | SFZ Opcode |
|---|---|
| `modLfoToVolume` | `amplfo_depth` |
| `modLfoToFilterFc` | `fillfo_depth` |
| `modLfoToPitch` | NOT exported (promoted to vibLFO, see above) |
| `delayModLFO` | `amplfo_delay` + `fillfo_delay` |
| `freqModLFO` | `amplfo_freq` + `fillfo_freq` |
| `vibLfoToPitch` | `pitchlfo_depth` |
| `delayVibLFO` | `pitchlfo_delay` |
| `freqVibLFO` | `pitchlfo_freq` |

---

## 9. Velocity Layers

### How Velocity Ranges Enter the SFZ

Velocity ranges come from the `champ_velRange` generator in instrument zones. The generator stores both bounds packed into one `int16_t` value using a custom encoding:

```
realValue = byLo × 1000 + byHi
```

Extraction in `writeElement()`:
```cpp
case champ_velRange:
    int lovel = qRound(value / 1000.);
    int hivel = qRound(value - 1000. * lovel);
    if (lovel > hivel): swap them
    _sfzWriter->addLine("lovel", lovel);
    _sfzWriter->addLine("hivel", hivel);
```

### Zone Sort Order

Before processing zones, `exportPrst()` sorts instrument zones by `keyRange.byLo` ascending:

```cpp
QMultiMap<int, EltID> map;
foreach (int j, sf2->getSiblings(idInstSmpl))
{
    idInstSmpl.indexElt2 = j;
    map.insert(sf2->get(idInstSmpl, champ_keyRange).rValue.byLo, idInstSmpl);
}
listInstSmpl = map.values();
```

This means that within a group, zones appear in key-ascending order. Multiple velocity layers on the same key group naturally cluster together in the SFZ output.

### Example: Three Velocity Layers

A harmonium with soft/medium/loud samples for each note would have:
- Zone 1: key C4–C4, vel 0–48 → sample "C4_soft.wav"
- Zone 2: key C4–C4, vel 49–96 → sample "C4_medium.wav"
- Zone 3: key C4–C4, vel 97–127 → sample "C4_loud.wav"

These produce three `<region>` entries under the same `<group>`, all sharing the same key range but with different `lovel`/`hivel` and `sample=` opcodes.

### Default Velocity Range

If `champ_velRange` is not explicitly set in an instrument zone, the default range is 0–127 (full range). The generator will not appear in the zone's generator list (`isSet()` returns false), so no `lovel`/`hivel` opcodes appear in the SFZ region — the SFZ default of full velocity range applies.

---

## 10. Region Construction

### The Decision Algorithm

A new `<region>` entry is created for each instrument zone that passes all five of the following tests:

**Test 1: Not already processed (stereo partner skip)**
```
if listProcessedInstSmpl.contains(idInstSmpl): skip
```
When a left-channel sample is processed, its right-channel partner is added to `listProcessedInstSmpl` and skipped in the next iteration.

**Test 2: Range overlap with preset zone**
```
if NOT isIncluded(paramPrst, idInstSmpl): skip
```
The zone's key/vel range must overlap the preset zone's key/vel range. Zones entirely outside the preset's scope are excluded.

**Test 3: Zone not muted**
Implicit: the `SoundfontManager::getSiblings()` call returns only non-deleted zones. Muted zones are returned but carry a `champ_mute` flag that is not currently checked during SFZ export — muted zones ARE exported.

**Test 4: Stereo partner search (for stereo samples)**
If the sample is `leftSample` or `rightSample`, search forward in the sorted zone list for a zone referencing the partner sample with matching parameters.

```
If stereo zone found at index `k`:
    listProcessedInstSmpl << listInstSmpl[k]   // Mark partner as processed
    balance.storeAttenuationAndPan(partner)    // Include partner's pan/atten
```

**Test 5: Parameter list construction**
The `SfzParamList` constructor resolves and merges all parameters. If any critical value is invalid (e.g., sample not found), the constructor may produce an empty or invalid list. In practice, the export does not explicitly abort — it produces a region with `sample=` pointing to an exported WAV.

### The One-SFZ-Region-per-Instrument-Zone Rule

Each non-skipped instrument zone produces exactly one `<region>` entry. There is no case where one zone produces zero regions (after passing the checks) or multiple regions. The zone-to-region ratio is exactly 1:1 for zones that pass the checks.

### Complete Region Construction Sequence

```pseudocode
function constructRegion(idInstSmpl, paramPrst, paramInst):

    1. getSamplePath(idSmpl):
       → Export WAV (or reuse if already exported)
       → Detect stereo type (mono=0, left=-1, right=+1)
       → Find and mark stereo partner

    2. BalanceParameters(isMono):
       → storeAttenuationAndPan(left channel values)
       → storeAttenuationAndPan(right channel values, if stereo)
       → computeSfzParameters():
          * Mono: pan = inst_pan, volume = -attenuation_dB
          * Stereo both channels: derive position/width/volume from L+R coefficients

    3. SfzParamList paramInstSmpl(sf2, &paramPrst, idInstSmpl):
       → Full inheritance resolution (see Section 4)
       → Injects root key, fine tune, loop offsets, LFO adaptations

    4. sfzWriter.newRegion()
       sfzWriter.addLine("sample", samplePath)
       if stereo: addLine("width", ...) + addLine("position", ...)
       addLine("volume", balance.volume)
       addLine("pan", balance.pan)
       for each parameter in paramInstSmpl:
           writeElement(parameter, value)
       for each modulator in paramInstSmpl:
           writeModulator(modulator)
```

---

## 11. Unit Conversion Reference

This table maps every SF2 generator through its complete transformation chain from storage to SFZ.

| Generator | SF2 Stored Unit | Polyphone Real Unit | SFZ Opcode Unit | Conversion |
|---|---|---|---|---|
| `attackVolEnv` | int16 timecents | seconds | `ampeg_attack` seconds | `2^(tc/1200)` |
| `holdVolEnv` | int16 timecents | seconds | `ampeg_hold` seconds | `2^(tc/1200)` |
| `decayVolEnv` | int16 timecents | seconds | `ampeg_decay` seconds | `2^(tc/1200)` |
| `releaseVolEnv` | int16 timecents | seconds | `ampeg_release` seconds | `2^(tc/1200)` |
| `delayVolEnv` | int16 timecents | seconds | `ampeg_delay` seconds | `2^(tc/1200)` |
| `sustainVolEnv` | int16 centibels/10 | dB | `ampeg_sustain` percent | `100×10^(−dB/20)` |
| `initialFilterFc` | int16 absolute cents | Hz | `cutoff` Hz | `8.176×2^(cents/1200)` |
| `initialFilterQ` | int16 tenths dB | dB | `resonance` dB | `/10` |
| `freqModLFO` | int16 absolute cents | Hz | `amplfo_freq`/`fillfo_freq` Hz | `8.176×2^(cents/1200)` |
| `freqVibLFO` | int16 absolute cents | Hz | `pitchlfo_freq` Hz | `8.176×2^(cents/1200)` |
| `delayModLFO` | int16 timecents | seconds | `amplfo_delay`/`fillfo_delay` seconds | `2^(tc/1200)` |
| `delayVibLFO` | int16 timecents | seconds | `pitchlfo_delay` seconds | `2^(tc/1200)` |
| `modLfoToVolume` | int16 centibels/10 | dB | `amplfo_depth` dB | `/10` |
| `modLfoToFilterFc` | int16 cents | cents | `fillfo_depth` cents | direct |
| `vibLfoToPitch` | int16 cents | cents | `pitchlfo_depth` cents | direct |
| `initialAttenuation` | int16 centibels | dB (with coeff) | `volume` dB (negative) | `0.1×DB_SF2_TO_REAL_DB×stored` |
| `pan` | int16 (−500 to +500, /10) | −50.0 to +50.0 | `pan` (−100 to +100) | `× 2` |
| `coarseTune` | int16 semitones | semitones | `transpose` semitones | direct |
| `fineTune` | int16 cents | cents | `tune` cents | direct |
| `scaleTuning` | int16 0–100 | 0–100 | `pitch_keytrack` 0–100 | direct |
| `exclusiveClass` | uint16 | uint16 | `group` + `off_by` same value | direct |
| `keyRange` | 2×uint8 byLo/byHi | `byLo*1000+byHi` | `lokey`/`hikey` note names | decoded + name lookup |
| `velRange` | 2×uint8 byLo/byHi | `byLo*1000+byHi` | `lovel`/`hivel` 0–127 | decoded |
| `byOriginalPitch` | uint8 MIDI note | MIDI note | `pitch_keycenter` note name | name lookup |
| `chPitchCorrection` | int8 cents | cents | summed into `tune` | added to fineTune |
| `dwStartLoop` | uint32 frames | frames | `loop_start` frames | direct |
| `dwEndLoop` | uint32 frames | frames | `loop_end` frames | `−1` (exclusive→inclusive) |

---

## 12. VHL Builder Implications

### What the Builder Must Replicate

This table maps each Polyphone mechanism to the corresponding required Builder behavior.

| Polyphone Mechanism | Builder Must Do |
|---|---|
| `SfzParamList` constructor 1 (preset) | Parse `[group]` and `[region]` blocks from SFZ; apply SFZ push-down inheritance |
| `SfzParamList` constructor 3 (inst zone) | For each `[region]`, this work is already done by Polyphone before export — the SFZ file contains fully resolved values |
| `loadAttributes` first-loaded-wins | SFZ regions already have resolved values — no re-resolution needed |
| `merge()` additive rule | **Not needed** — all additive merging is already done during SFZ export |
| `merge()` multiplicative rule | **Not needed** — time/frequency multiplicative merging already done |
| Offset mixing (coarse+fine) | **Not needed** — `mix()` already produces absolute frame positions |
| Root key injection | **Not needed** — `pitch_keycenter` is explicit in every SFZ region |
| Fine tune sum | **Not needed** — `tune` in SFZ includes sample correction |
| modLfoToPitch→vibLFO promotion | **Not needed** — `pitchlfo_depth` in SFZ is the final result |
| `adaptKeynum2()` | **Partially done** — but Builder must handle `ampeg_holdcc133` / `ampeg_decaycc133` opcodes |
| `processDescendingVelocityModulators()` | **Not needed** — polarity is corrected in SFZ |
| `sustainVolEnv` centibels → percent | **Needed in reverse**: SFZ `ampeg_sustain` is percent; Builder must convert back to VHL centibels |
| `sustainModEnv` % → SFZ (100-%) | **Needed in reverse**: SFZ `pitcheg_sustain`/`fileg_sustain` are (100-%) of SF2 value |
| Timecent→seconds (all time params) | **Done by Polyphone** — SFZ values are already in seconds |
| `freqModLFO` absolute cents → Hz | **Done by Polyphone** — SFZ values are in Hz |
| Stereo sample pairing | **Not needed** — Builder uses mono WAV files (stereo exported as single file, handled by WAV reader) |
| WAV file export | **Needed** — Builder must read the WAV files Polyphone exported |
| `isIncluded()` range clip | **Not needed** — only included zones appear in SFZ |
| `factorizeOpCodes()` | **Need to reconstruct** — group opcodes apply to all regions; Builder must merge group + region opcodes |
| `cleanOpCodes()` | **Awareness only** — if `pitchlfo_freq` is in group but `pitchlfo_depth` is absent, ignore the freq |

### What Is Trivial to Parse from the SFZ Output

The SFZ produced by Polyphone is clean and explicit. Because Polyphone's `SfzParamList` has fully resolved all inheritance, the VHL Builder's SFZ parser can be very simple:

1. Parse `<group>` block: collect all opcodes as group defaults
2. Parse `<region>` block: collect all opcodes; merge with group defaults (region overrides group)
3. The merged opcode set for each region is the complete, final parameter set
4. Convert each SFZ opcode to VHL units (mostly: seconds remain seconds; Hz remains Hz; just need to convert to VHL int types)

There is **no inheritance to resolve**. Polyphone already resolved it.

### The Builder's SFZ Parser — Required Opcode Support

| SFZ Opcode | Maps To |
|---|---|
| `sample=` | WAV file path → VHLSampleEntry |
| `lokey=`, `hikey=`, `key=` | VHLRegion.key_lo, key_hi |
| `lovel=`, `hivel=` | VHLRegion.vel_lo, vel_hi |
| `pitch_keycenter=` | VHLSampleEntry.root_key |
| `tune=` | VHLRegion.fine_tune |
| `transpose=` | VHLRegion.coarse_tune |
| `pitch_keytrack=` | VHLRegion.scale_tuning |
| `volume=` | VHLRegion.attenuation_cb (convert dB → centibels) |
| `pan=` | VHLRegion.pan (SFZ ±100 → VHL ±500: multiply by 5) |
| `ampeg_delay/attack/hold/decay/sustain/release=` | VHLRegion.vol_env (convert seconds → ms; sustain % → centibels) |
| `fileg_delay/attack/hold/decay/sustain/release=` | VHLRegion.mod_env |
| `pitcheg_depth=` | VHLRegion.mod_env_to_pitch |
| `fileg_depth=` | VHLRegion.mod_env_to_filter |
| `ampeg_holdcc133=` | VHLRegion.vol_env.key_to_hold |
| `ampeg_decaycc133=` | VHLRegion.vol_env.key_to_decay |
| `cutoff=` | VHLRegion.filter.cutoff_hz |
| `fil_type=` | Validated (must be `lpf_2p`; others → warning) |
| `resonance=` | VHLRegion.filter.resonance_cb |
| `amplfo_delay/freq/depth=` | VHLRegion.lfo_mod.delay_ms/freq_mHz/to_volume_cb |
| `fillfo_delay/freq/depth=` | VHLRegion.lfo_mod.delay_ms/freq_mHz/to_filter_cb |
| `pitchlfo_delay/freq/depth=` | VHLRegion.lfo_vib.delay_ms/freq_mHz/to_pitch_cb |
| `loop_mode=` | VHLRegion.loop_mode |
| `loop_start=` | VHLSampleEntry.loop_start_frame |
| `loop_end=` | VHLSampleEntry.loop_end_frame (+1 for inclusive→exclusive) |
| `offset=` | VHLSampleEntry.data_offset adjustment |
| `group=` + `off_by=` | VHLRegion.exclusive_class |
| `effect1=` | VHLRegion.reverb_send |
| `effect2=` | VHLRegion.chorus_send |
| `amp_veltrack=` | VHLRegion.flags (VHL_REGION_FLAG_VEL_ATTEN + curve depth) |
| `fil_veltrack=` | Future (velocity→filter modulation) |
| `fil_keytrack=` + `fil_keycenter=` | Future |
| `amp_velcurve_1=` + `amp_velcurve_127=` | Ignore (fixed velocity — percussion) |

### Critical Insight: Polyphone's SFZ Is Already Flat

The single most important finding of this study is that **Polyphone's SFZ export produces fully-resolved, flat regions**. Every `<region>` in the output is self-sufficient. Group opcodes need only be merged with region opcodes (a simple dictionary merge), not traversed through a hierarchy.

The VHL Builder does not need to implement SF2 hierarchy resolution, global-zone inheritance, or modulator graph evaluation. Polyphone's `SfzParamList` has already done all of that work.

The Builder's SFZ parser can therefore be:
1. A simple line-by-line text parser
2. A two-level opcode merger (group + region)
3. A unit converter (seconds→ms, Hz→mHz, percent→centibels)
4. A WAV file reader
5. A binary packer

All the complex algorithmic work shown in Sections 3–9 of this document has been done by Polyphone and is captured in the output SFZ file. The Builder consumes the result, not the algorithm.

---

*End of Polyphone Internal Architecture — Pre-Phase 2 Engineering Reference*
*This document should be read in conjunction with:*
- *`module1_instrument_architecture.md` — SF2 data model from the runtime perspective*
- *`module2_playback_engine.md` — Runtime DSP pipeline*
- *`module3_architecture.md` — VHL embedded system design*
- *`architecture.md` (phase1_vhl/) — VHL binary format specification*
