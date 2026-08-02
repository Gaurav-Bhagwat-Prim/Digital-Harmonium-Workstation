# Phase 2D — Professional VHL Development Tools: Architecture Reference

**Version:** 2.0  
**Status:** Complete & Verified  
**Executable:** `build/bin/vhl_tools.exe`

---

## 1. Overview

Phase 2D introduces a unified professional toolkit for the VHL binary instrument format. It is entirely separate from the VHL Builder and does not share compilation units with Phase 2A/2B/2C. Its sole purpose is to help developers understand, verify, and compare `.vhl` binary files at the decoded object level.

The toolkit compiles into a single executable with three subcommands:

```text
vhl_tools inspect  <file.vhl> [--all] [--regions] [--samples]
vhl_tools validate <file.vhl> [--strict]
vhl_tools diff     <old.vhl> <new.vhl> [--detailed]
```

---

## 2. Directory Structure

```text
Builder/
  Tools/
    Core/                     ← Shared VhlCore Library
      VhlDocument.h/.cpp      — Decoded object model + computed statistics
      VhlLoader.h/.cpp        — Binary loader (std::ifstream → VhlDocument)
      VhlFormatter.h/.cpp     — Output formatting helpers

    Commands/                 ← Subcommand Implementations
      InspectCommand.h/.cpp   — Instrument inspector
      ValidateCommand.h/.cpp  — Logical integrity validator
      DiffCommand.h/.cpp      — Structure-level binary diff

    CLI/
      main.cpp                — Subcommand dispatcher

    VhlReader/                ← Baseline verification prototype (Phase 2C)
      VhlReader.h/.cpp
      main.cpp

  phase2d_architecture.md     ← This document
```

---

## 3. Shared Core Library (`Tools/Core`)

Every Phase 2D tool is built on three shared components. No binary parsing logic exists outside the Core library.

### 3.1 `VhlDocument`

The central in-memory object model for a decoded `.vhl` file.

| Field | Type | Description |
|---|---|---|
| `rawBuffer` | `vector<uint8_t>` | Complete raw binary file bytes |
| `header` | `VHLFileHeader` | Unpacked file header struct |
| `instruments` | `vector<VHLInstrumentEntry>` | All instrument entries |
| `regions` | `vector<VHLRegion>` | All region entries |
| `samples` | `vector<VHLSampleEntry>` | All sample entries |
| `lookupTables` | `vector<vector<VHLLookupEntry>>` | Per-instrument key lookup tables |
| `crcValid` | `bool` | CRC-32 check result |
| `stats` | `ComputedStats` | Pre-computed instrument statistics |

`computeStatistics()` runs automatically after loading and calculates:
- Total PCM bytes & duration
- Per-sample PCM byte sizes & loop flags
- Ranked list of largest samples (top 5)
- Averaged envelope timings, filter cutoff, sample length
- Active MIDI key count from lookup tables

### 3.2 `VhlLoader`

Single static method: `VhlLoader::loadFromFile(path, doc)`

**Load sequence:**
1. `std::ifstream` opens file in binary mode, reads entire buffer
2. `VHLFileHeader` copied via `std::memcpy`
3. CRC-32 computed via `CRC32::computeWithZeroedField()` (header CRC field treated as 0)
4. Instruments, Regions, Samples unpacked with offset + bounds checks
5. Lookup tables extracted per instrument from `lookup_table_offset`
6. `doc.computeStatistics()` called

All struct unpacking uses `std::memcpy` into the `VhlDocument` vectors, preserving exact binary layout. No parsing of text or SFZ is performed.

### 3.3 `VhlFormatter`

Static formatting utilities consumed by all commands:

| Method | Output |
|---|---|
| `formatBytes(n)` | `"960.0 KB"`, `"1.2 MB"`, `"450 B"` |
| `formatBar(%, width)` | `"[########..] 80.0%"` |
| `formatKeyCoverageMap(doc)` | 11-octave ASCII key grid |
| `noteName(midi)` | `"C4"`, `"A#3"`, `"G9"` |

---

## 4. Subcommand Reference

### 4.1 `inspect` — Instrument Inspector

**Purpose:** Rich human-readable analysis of an instrument. Think of it as the `readelf` of the VHL ecosystem.

**Usage:**
```
vhl_tools inspect <file.vhl> [--all] [--regions] [--samples]
```

**Output Sections:**
1. **Header & Build Metadata** — Magic, version, build tag, CRC status, file size
2. **Memory Footprint Distribution** — ASCII bar chart of section sizes as % of total file
3. **Computed Instrument Statistics** — Total PCM, avg envelopes, avg cutoff, key coverage
4. **MIDI Key Coverage Map** — Octave-by-octave ASCII grid (`#` = covered, `.` = gap)
5. **Top Largest Samples** — Ranked table by PCM byte size
6. **Instrument Directory** — MIDI bank/program, region slice
7. **Region Details** *(with `--regions` or `--all`)* — Per-region compact parameter line
8. **Sample Directory Details** *(with `--samples` or `--all`)* — Per-sample format & loop info

**Example output:**
```text
=============================================================
VHL INSPECTOR: Harmonium.vhl
=============================================================

--- Memory Footprint Distribution ---
  Header           (    128 B) : [..........] 0.0%
  PCM Sample Data  (960.0 KB) : [##########] 99.9%

--- Computed Instrument Statistics ---
  Total PCM Memory      : 960.0 KB
  Total PCM Audio Length: 11.15 seconds
  Looped vs One-Shot    : 1 looped, 0 one-shot
  Average Vol Attack    : 40.0 ms
  Average Filter Cutoff : 4978 Hz
  Active MIDI Keys      : 128 / 128
```

---

### 4.2 `validate` — Logical Integrity Validator

**Purpose:** Automated structural and logical correctness verification. Suitable for CI/CD build pipelines.

**Usage:**
```
vhl_tools validate <file.vhl> [--strict]
```

**Checks performed (categorized by severity):**

| Category | Check | Level |
|---|---|---|
| Header | Magic bytes `VHL1` | INFO/ERROR |
| Header | `total_file_size` matches buffer | ERROR |
| CRC | CRC-32 checksum | INFO/ERROR |
| Layout | All section offsets within file | ERROR |
| Layout | Section non-overlapping order | ERROR |
| Layout | PCM data 4-byte alignment per sample | WARNING |
| Instrument | Duplicate `instrument_id` | WARNING |
| Instrument | Region slice within total regions | ERROR |
| Instrument | Lookup table offset within file | ERROR |
| Region | `key_lo <= key_hi` | ERROR |
| Region | `vel_lo <= vel_hi` | ERROR |
| Region | `sample_id < total_sample_count` | ERROR |
| Region | Unusual release time (> 30s) | WARNING |
| Region | Filter cutoff > Nyquist | WARNING |
| Sample | `total_frames > 0` | WARNING |
| Sample | PCM data within file boundary | ERROR |
| Sample | Loop `start < end <= total_frames` | ERROR |

**Quality Score:** `100% − (Errors × 25%) − (Warnings × 5%)`, clamped 0–100%.

**Exit codes:**
- `0` — Zero errors (PASS)
- `1` — One or more errors (FAIL)

In `--strict` mode, warnings also trigger exit code `1`.

**Example output:**
```text
Validation Summary
------------------------------
Errors    : 0
Warnings  : 0
Quality   : 100%
Status    : PASS
==============================
```

---

### 4.3 `diff` — Structure-Level Binary Diff

**Purpose:** Compare two `.vhl` instruments at the decoded object level. Does not compare raw bytes.

**Usage:**
```
vhl_tools diff <old.vhl> <new.vhl> [--detailed]
```

**Comparison scope:**

| Area | What is compared |
|---|---|
| Header | File size, CRC |
| Regions | Key/vel ranges, tuning, attenuation, pan, envelopes, filter, LFOs, sample assignments |
| Samples | Frame count, sample rate, loop points |
| Counts | Added/removed regions or samples |
| PCM | Total PCM byte delta |

**Summary mode (default):**
```text
--- High-Level Diff Summary ---
  File A Size       : 960.8 KB
  File B Size       : 985.0 KB
  Changed Regions   : 3
  Changed Samples   : 1
  PCM Delta         : +24.2 KB

Modified Parameter Categories:
  • Volume Envelope
  • Filter Cutoff / Resonance
  • Loop Points
```

**Detailed mode (`--detailed`):**  
Adds per-region parameter deltas (old → new values for attack, release, cutoff, attenuation, etc.)

**Exit codes:**
- `0` — Files are identical at the decoded structure level
- `1` — Differences detected

---

## 5. Interaction Between Tools

```
.sfz + .wav
    │
    ▼
vhl_builder     ← Phase 2C: compiles .vhl binary
    │
    ▼
vhl_tools validate      ← structural integrity check (CI gate)
    │
    ├── PASS → vhl_tools inspect   ← developer analysis
    │
    └── next rebuild →
        vhl_tools diff old.vhl new.vhl   ← regression tracking
```

**Recommended workflow for developers:**

1. Export SFZ from Polyphone
2. `vhl_builder` → compile `.vhl`
3. `vhl_tools validate` → verify before deployment
4. `vhl_tools inspect --all` → analyze instrument characteristics
5. On re-build: `vhl_tools diff` → catch unexpected parameter regressions

---

## 6. Build Information

All Phase 2D tools are built as a single executable target `vhl_tools` in [`CMakeLists.txt`](file:///d:/TYBSC/TYBSC%20Arduino%20Project/Esp32%20Digital%20Musical%20Workstation/Builder/CMakeLists.txt).

**Targets compiled:**
- `vhl_builder.exe` — Full SFZ→VHL compiler pipeline
- `vhl_tools.exe` — Phase 2D unified toolkit
- `vhl_reader.exe` — Phase 2C baseline verification prototype

All targets output to `build/bin/`.

**Dependencies:** Only `vhl_structs.h` (binary spec), `Backend/CRC32.h/.cpp`, and C++17 stdlib. No external libraries.

---

## 7. Future Extension Possibilities

| Feature | Description |
|---|---|
| `vhl_tools extract` | Export PCM samples from `.vhl` to `.wav` files |
| `vhl_tools stats --csv` | Machine-readable stats output for tooling pipelines |
| `vhl_tools merge` | Combine multiple single-instrument `.vhl` files into one multi-instrument file |
| `vhl_tools roundtrip` | Verify that a `.vhl` rebuilds identically from the original SFZ (regression testing) |
| Phase 3 Runtime | ESP32 playback engine uses `VhlLoader`-equivalent C code to load instruments at boot |

The Phase 3 ESP32 runtime will implement an equivalent of `VhlLoader` in C, reading sections in the same sequence the loader uses here — making the Core library's traversal order a direct specification for the embedded implementation.
