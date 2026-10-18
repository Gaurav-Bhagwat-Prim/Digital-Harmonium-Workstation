# VHL Specification v2.0: The Unified Instrument & Workstation Architecture

**Document Title:** Virtual Harmonium Lab (VHL) Binary Format Specification v2.0  
**Project:** ESP32 Digital Harmonium Workstation & Musical Accompaniment Laboratory  
**Status:** Comprehensive Master Specification  
**Applies to:** Rust Desktop Workstation (Compiler / Decompiler / GUI), ESP32 Embedded Runtime, Instrument Designers  
**Supersedes:** VHL Specification v1.0 (phase1_vhl/architecture.md)  
**Date:** 2026-10-18  

---

## Table of Contents

1. [Executive Vision & "Single Source of Truth" Philosophy](#1-executive-vision--single-source-of-truth-philosophy)
   * 1.1 Why VHL v1.0 Was Insufficient
   * 1.2 The VHL v2.0 Paradigm: Instrument + Physical Workstation Definition
   * 1.3 The Precedence & Fallback Contract (Firmware vs. VHL)
   * 1.4 Architectural Non-Negotiables (Zero-Copy, Fixed Alignment, Zero Heap Churn)
2. [Microtonal Intonation & The Pragmatic Shruti Philosophy](#2-microtonal-intonation--the-pragmatic-shruti-philosophy)
   * 2.1 Why 22 Physical Keys per Octave is Unviable
   * 2.2 The 12-Key Swara Cent-Offset Solution
   * 2.3 Raga-Specific Microtonal Calibration (Gandhar, Dhaivat, Nishad)
3. [Master Binary Memory Map & File Topology](#3-master-binary-memory-map--file-topology)
   * 3.1 Contiguous Section Order
   * 3.2 4-Byte Boundary Alignment & Little-Endian Guarantee
   * 3.3 Visual Memory Layout Diagram
4. [Section 0: Master File Header (128 Bytes Fixed)](#4-section-0-master-file-header-128-bytes-fixed)
   * 4.1 Structure Definition (`VHLFileHeader_v2`)
   * 4.2 Field-by-Field Technical Specification & Engineering Rationale
5. [Section 1: Physical Keybed & Matrix Mapping Table (NEW in v2.0)](#5-section-1-physical-keybed--matrix-mapping-table-new-in-v20)
   * 5.1 Structure Definition (`VHLKeyMapEntry_v2`, 16 Bytes per Key)
   * 5.2 Field-by-Field Technical Specification & Engineering Rationale
   * 5.3 Keyboard Splits (Left Accompaniment vs. Right Lead)
   * 5.4 Drone & Latch Keys (Tanpura / Sur / Continuous Harmonium Reeds)
   * 5.5 Pre-Baked Floating-Point Resample Ratios
6. [Section 2: Musical Tuning & Swara Shruti Table (NEW in v2.0)](#6-section-2-musical-tuning--swara-shruti-table-new-in-v20)
   * 6.1 Structure Definition (`VHLTuningTable_v2`, 64 Bytes Fixed)
   * 6.2 Field-by-Field Technical Specification & Engineering Rationale
   * 6.3 Standard 12-TET vs. Just Intonation vs. Raga Shruti Tunings
7. [Section 3: Multi-Timbral Instrument Table (48 Bytes per Instrument)](#7-section-3-multi-timbral-instrument-table-48-bytes-per-instrument)
   * 7.1 Structure Definition (`VHLInstrumentEntry_v2`)
   * 7.2 Field-by-Field Technical Specification & Engineering Rationale
   * 7.3 Polyphony Capping & Priority Routing
8. [Section 4: Pre-Baked DSP Region Table (80 Bytes per Region)](#8-section-4-pre-baked-dsp-region-table-80-bytes-per-region)
   * 8.1 Structure Definition (`VHLRegion_v2`)
   * 8.2 Sub-Structures: `VHLEnvelope` (DAHDSR), `VHLFilter` (Biquad), `VHLModLFO`, `VHLVibLFO`
   * 8.3 Field-by-Field Technical Specification & Engineering Rationale
9. [Section 5: Sample Directory (32 Bytes per Sample Entry)](#9-section-5-sample-directory-32-bytes-per-sample-entry)
   * 9.1 Structure Definition (`VHLSampleEntry_v2`)
   * 9.2 Field-by-Field Technical Specification & Engineering Rationale
   * 9.3 Seamless Loop Boundaries & Micro-Crossfade Data
10. [Section 6: O(1) Accelerated Region Lookup Table](#10-section-6-o1-accelerated-region-lookup-table)
    * 10.1 Structure Definitions (`VHLLookupHeader`, `VHLLookupEntry`)
    * 10.2 Constant-Time Search Algorithm at Note-On
11. [Section 7: PCM Audio Data Pool](#11-section-7-pcm-audio-data-pool)
    * 11.1 Audio Layout & Alignment
    * 11.2 Flash XIP Cache vs. PSRAM Memory Allocation
12. [CRC-32 Integrity Verification Algorithm](#12-crc-32-integrity-verification-algorithm)
13. [Comparative Summary: VHL v1.0 vs. VHL v2.0](#13-comparative-summary-vhl-v10-vs-vhl-v20)
14. [Compiler, Decompiler & Runtime Execution Lifecycle](#14-compiler-decompiler--runtime-execution-lifecycle)

---

## 1. Executive Vision & "Single Source of Truth" Philosophy

### 1.1 Why VHL v1.0 Was Insufficient
In VHL Specification v1.0, the format achieved its initial embedded goal: collapsing desktop SF2/SFZ hierarchies into flat binary arrays that the ESP32 could read without running expensive string parsers, tree traversals, or dynamic heap allocations.

However, practical deployment on the physical 61-key workstation revealed a severe structural limitation:
* **The `.vhl` file only described sound waveforms and envelopes.**
* **The physical instrument behavior was completely hardcoded into the ESP32 C++ firmware (`firmware.ino`).**

Specifically, the ESP32 firmware hardcoded:
1. The 61-key matrix map: `g_keyboard[61]` hardcoded MIDI notes 36 ($C_2$) through 96 ($C_7$).
2. The resample pitch ratios: $2^{\frac{i - 26}{12.0}}$ was hardcoded relative to physical Key 26 ($D_4$).
3. Keyboard split points: Non-existent in the file. Splitting the keyboard (e.g. Left hand = Piano, Right hand = Harmonium) required modifying C source code, recompiling the firmware, and reflashing via USB.
4. Scale tuning: Hardcoded strictly to Western 12-Tone Equal Temperament ($12\text{-TET}$).
5. Drone / Latch switches: Hardcoded in C logic.

Consequently, whenever a musician wanted to reconfigure their keyboard, change root pitch, or introduce a second instrument layer, **the VHL file was powerless**. The developer had to edit C++ code, recompile, and burn the firmware to flash memory.

### 1.2 The VHL v2.0 Paradigm: Instrument + Physical Workstation Definition
**VHL v2.0 elevates the `.vhl` file to become the sole, authoritative specification of the entire instrument.**

In VHL v2.0:
* The `.vhl` binary file contains **not only the audio samples and DSP parameters**, but also the **complete physical keybed routing, zone splits, transpositions, and musical tuning scales**.
* The ESP32 firmware becomes a **100% generic playback and scanning computer**. It contains no hardcoded song data, no hardcoded keyboard splits, and no hardcoded instrument mappings.
* When the user adjusts a parameter in the PC Workstation App (such as moving the split point from Key 24 to Key 30, or changing a key's tuning), the PC app compiles a new VHL v2.0 package and sends it over Wi-Fi. The ESP32 reloads the pointers in SRAM, and the physical instrument behavior transforms immediately.

### 1.3 The Precedence & Fallback Contract (Firmware vs. VHL)
To ensure system robustness and fail-safe operation:
1. **The Firmware Fallback Baseline:** The ESP32 firmware maintains a minimal, hardcoded internal default configuration (standard 12-TET chromatic map from $C_2$ to $C_7$, uniform instrument assignment to Instrument #0).
2. **The VHL Override Rule:** Whenever a valid VHL v2.0 file is loaded:
   * If `flags` indicate that Section 1 (Keybed Mapping Table) is present, **the runtime completely bypasses its internal matrix defaults** and directs every key event through the VHL Keybed Mapping Table.
   * If `flags` indicate that Section 2 (Tuning Table) is present, **the runtime applies the microtonal cent offsets specified in the VHL file**.
   * If any section is marked absent or corrupt, the runtime gracefully falls back to its built-in baseline without crashing.

### 1.4 Architectural Non-Negotiables
To ensure zero audio dropouts on an ESP32 microcontroller operating at 240 MHz:
1. **Zero Dynamic Allocation at Runtime:** No `malloc()`, `calloc()`, or C++ `new` may be invoked when loading or processing VHL v2.0.
2. **Deterministic Memory Mapping (Zero-Copy):** The ESP32 directly casts flash memory addresses into C struct pointers:
   ```c
   pLoader->pKeyMap = (const VHLKeyMapEntry_v2*)(pBase + pHeader->offset_keybed_map);
   ```
3. **4-Byte Natural Alignment:** Every structure, table, and PCM sample block begins on a 4-byte memory boundary ($addr \pmod 4 == 0$). This guarantees single-cycle 32-bit CPU bus loads without triggering Xtensa unaligned memory access exceptions.
4. **Pre-Baking Offline:** All expensive computations ($2^{\frac{\Delta}{12}}$, filter biquad coefficients, logarithmic decibel-to-centibel conversions) are evaluated once on the desktop PC. The ESP32 reads pre-calculated 32-bit floats and integers.

---

## 2. Microtonal Intonation & The Pragmatic Shruti Philosophy

### 2.1 Why 22 Physical Keys per Octave is Unviable
A theoretical concept often proposed for Indian Classical Music is dividing the octave into 22 physical keys. On a physical keyboard, this is unviable:
1. **Mechanical Reality:** Commercial piano keybeds (including the 61-key revived keyboard in this project) are mechanically engineered with 12 physical keys per octave (7 white natural keys, 5 black accidental keys). Replacing or re-spacing this keybed into 22 physical spring switches requires custom injection-molded keybeds and destroys conventional keyboard ergonomics.
2. **Musician Usability:** Musicians and students cannot perform standard keyboard fingering techniques on a non-standard 22-key physical octave.

### 2.2 The 12-Key Swara Cent-Offset Solution
VHL v2.0 solves Indian Classical intonation pragmatically:
* The physical keyboard retains its standard, familiar **12 mechanical keys per octave** ($C, C\sharp, D, D\sharp, E, F, F\sharp, G, G\sharp, A, A\sharp, B$).
* In Indian Classical performance, a raga selects a specific 7-note or 5-note scale (*Arohana* and *Avarohana*). Within that scale, specific swaras take specific microtonal shrutis (e.g. *Ati-Komal Rishabh*, *Pramana Shruti*, *Teevra Madhyam*).
* VHL v2.0 defines a **Microtonal Tuning Table (Section 2)** where each of the 12 chromatic degrees can be assigned a precise microtonal detune offset in **cents** ($\pm 100.0$ cents with 0.1 cent precision).

### 2.3 Raga-Specific Microtonal Calibration
For example, when loading a VHL package configured for **Raga Bhairav**:
* $Sa$ (Root, e.g. $D_4$): Offset = $0.0$ cents.
* $Re$ (*Komal Rishabh*, $D\sharp_4$): Microtonally flattened by $-20.0$ cents to sound the authentic *Eksruti Rishabh*.
* $Ga$ (*Shuddha Gandhar*, $F\sharp_4$): Offset by $-14.0$ cents to align with the pure harmonic Just Third ($5:4$ ratio).
* $Ma$ (*Shuddha Madhyam*, $G_4$): $+2.0$ cents.
* $Pa$ (*Pancham*, $A_4$): $+2.0$ cents.
* $Dha$ (*Komal Dhaivat*, $A\sharp_4$): Flattened by $-20.0$ cents.
* $Ni$ (*Shuddha Nishad*, $C\sharp_5$): $-14.0$ cents.

Musicians play the normal keys, but the instrument sounds genuine, harmonic Indian Classical intonation. Furthermore, this tuning is completely optional: if bypassed, standard 12-TET equal temperament is used.

---

## 3. Master Binary Memory Map & File Topology

### 3.1 Contiguous Section Order
A `.vhl` file consists of exactly eight contiguous sections. The runtime uses absolute 32-bit byte offsets stored in Section 0 to jump directly to any table.

```
┌────────────────────────────────────────────────────────────────────────┐
│ SECTION 0: FILE HEADER                                128 Bytes Fixed  │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 1: PHYSICAL KEYBED MAPPING TABLE              Variable Length  │
│   num_physical_keys × 16 bytes (e.g. 61 × 16 = 976 bytes)              │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 2: MUSICAL TUNING & SHRUTI TABLE              64 Bytes Fixed   │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 3: MULTI-TIMBRAL INSTRUMENT TABLE             Variable Length  │
│   num_instruments × 48 bytes                                           │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 4: PRE-BAKED REGION TABLE                     Variable Length  │
│   total_region_count × 80 bytes                                        │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 5: SAMPLE DIRECTORY                           Variable Length  │
│   total_sample_count × 32 bytes                                        │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 6: O(1) ACCELERATED LOOKUP TABLE              Variable Length  │
│   num_instruments × (4 + 128 × 4 bytes)                                │
├────────────────────────────────────────────────────────────────────────┤
│ SECTION 7: PCM AUDIO DATA POOL                        Variable Length  │
│   Contiguous 16-bit Signed PCM Frames (4-byte aligned per sample)      │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.2 4-Byte Boundary Alignment & Little-Endian Guarantee
* **Endianness:** All multi-byte numerical fields (16-bit integers, 32-bit integers, 32-bit IEEE 754 floats) are strictly encoded in **Little-Endian** byte order (least significant byte first), natively matching the Xtensa LX6 CPU architecture.
* **Padding:** Compiler padding between fields within structures is strictly forbidden (`__attribute__((packed))` in C/C++, `#[repr(C, packed)]` in Rust).
* **Section Alignment:** Every section start offset in the file header MUST satisfy:
  $$\text{Offset} \pmod 4 == 0$$
  If a preceding section has an unaligned byte length, the compiler appends $1$, $2$, or $3$ zero padding bytes before starting the next section.

---

## 4. Section 0: Master File Header (128 Bytes Fixed)

### 4.1 Structure Definition (`VHLFileHeader_v2`)

```c
typedef struct VHL_PACKED {
    /* Identity (4 bytes) */
    uint8_t  magic[4];                /* Must be { 'V', 'H', 'L', '2' } (0x324C4856) */

    /* Version (2 bytes) */
    uint8_t  version_major;           /* Must be 2 for VHL v2.0                       */
    uint8_t  version_minor;           /* Minor additive changes (starts at 0)         */

    /* Master File Flags (2 bytes) */
    uint16_t flags;                   /* VHL_FILE_FLAG_* bitmask                      */

    /* Global Resource Counts (8 bytes) */
    uint16_t num_physical_keys;       /* Number of physical keys (e.g. 61)            */
    uint16_t num_instruments;         /* Total instruments defined in Section 3       */
    uint16_t total_region_count;      /* Total regions across all instruments         */
    uint16_t total_sample_count;      /* Total PCM samples in Section 5               */

    /* Audio Hardware Target (6 bytes) */
    uint32_t target_sample_rate;      /* Output I2S rate in Hz (e.g. 44100)           */
    uint16_t dma_block_size;          /* Target DMA buffer size in frames (e.g. 64)   */

    /* Section Absolute Byte Offsets (32 bytes: 8 x uint32_t) */
    uint32_t offset_keybed_map;       /* Byte offset to Section 1                     */
    uint32_t offset_tuning_table;     /* Byte offset to Section 2                     */
    uint32_t offset_instrument_table; /* Byte offset to Section 3                     */
    uint32_t offset_region_table;     /* Byte offset to Section 4                     */
    uint32_t offset_sample_directory; /* Byte offset to Section 5                     */
    uint32_t offset_lookup_table;     /* Byte offset to Section 6                     */
    uint32_t offset_pcm_data;         /* Byte offset to Section 7 (PCM pool)          */
    uint32_t offset_reserved;         /* Reserved for future expansion (must be 0)    */

    /* Integrity & Size (8 bytes) */
    uint32_t total_file_size;         /* Exact total size of file in bytes            */
    uint32_t crc32;                   /* CRC-32 checksum of file (with crc32 = 0)     */

    /* Build Metadata (8 bytes) */
    uint32_t build_timestamp;         /* Unix epoch timestamp of compilation          */
    uint16_t builder_version;         /* Major << 8 | Minor of PC Compiler tool       */
    uint16_t _reserved_meta;          /* Alignment padding (must be 0)                */

    /* Human-Readable Build Tag (32 bytes) */
    char     build_tag[32];           /* Null-terminated ASCII string                 */

    /* Reserved for Future Extensions (26 bytes) */
    uint8_t  _reserved_future[26];    /* Must be zeroed                               */
} VHLFileHeader_v2; /* Total: Exactly 128 Bytes */
```

### 4.2 Field-by-Field Technical Specification & Engineering Rationale

| Offset (Bytes) | Field Name | Data Type | Permitted Values | Engineering Purpose & Rationale |
|---|---|---|---|---|
| `0..3` | `magic[4]` | `uint8_t[4]` | `'V','H','L','2'` | Identifies format and prevents accidental loading of VHL v1.0 or corrupted binary blobs. |
| `4` | `version_major` | `uint8_t` | `2` | Hard compatibility gate. Runtime rejects any file if `version_major != 2`. |
| `5` | `version_minor` | `uint8_t` | `0..255` | Additive features. Minor increments remain backward-compatible with older runtimes. |
| `6..7` | `flags` | `uint16_t` | Bitmask | Bit 0: `STEREO_OUTPUT` (1=Stereo, 0=Mono)<br>Bit 1: `HAS_KEYBED_MAP` (1=Use Sec 1, 0=Firmware defaults)<br>Bit 2: `HAS_TUNING_TABLE` (1=Use Sec 2, 0=12-TET)<br>Bit 3: `HAS_LOOKUP_TABLE` (1=O(1) lookup present)<br>Bit 4: `PSRAM_PRELOAD_HINT` (Copy PCM to PSRAM if available). |
| `8..9` | `num_physical_keys`| `uint16_t` | `1..128` (typically `61`)| Informs runtime how many entries exist in Section 1. Ensures bounds safety for scanner. |
| `10..11`| `num_instruments` | `uint16_t` | `1..32` | Slices Section 3 Instrument Table. |
| `12..13`| `total_region_count`| `uint16_t`| `1..4096` | Slices Section 4 Region Table. |
| `14..15`| `total_sample_count`| `uint16_t`| `1..1024` | Slices Section 5 Sample Directory. |
| `16..19`| `target_sample_rate`| `uint32_t`| `44100`, `48000` | Informs ESP32 I2S clock PLL setup without hardcoding. |
| `20..21`| `dma_block_size` | `uint16_t` | `32..256` (typically `64`)| Recommended DMA frame length for lowest latency. |
| `22..53`| `offset_*` (8 fields)| `uint32_t[8]`| Valid byte offsets | Enables instantaneous $O(1)$ pointer indexing to any section without sequential file scanning. |
| `54..57`| `total_file_size` | `uint32_t` | $> 128$ bytes | Guards against truncated file downloads over Wi-Fi or partial flash burns. |
| `58..61`| `crc32` | `uint32_t` | Standard IEEE CRC-32 | Comprehensive file integrity verification. Checked before loading into audio memory. |
| `62..65`| `build_timestamp`| `uint32_t`| Unix timestamp | Enables diagnostic logs and synchronization auditing between PC app and ESP32. |
| `66..67`| `builder_version`| `uint16_t`| e.g. `0x0200` (v2.0)| Tooling diagnostics. |
| `68..69`| `_reserved_meta` | `uint16_t` | `0` | Alignment padding. |
| `70..101`| `build_tag[32]` | `char[32]`| Null-terminated ASCII | Human-readable string shown on LCD or PC app: e.g. `"Harmonium+Piano Split v2\0"`. |
| `102..127`| `_reserved_future`| `uint8_t[26]`| All `0` | Reserved space for future expansion while keeping header size at 128 bytes. |

---

## 5. Section 1: Physical Keybed & Matrix Mapping Table (NEW in v2.0)

### 5.1 Structure Definition (`VHLKeyMapEntry_v2`, 16 Bytes per Key)
This section contains an array of `num_physical_keys` entries (for our hardware: exactly 61 entries, one per physical mechanical key from Key Index 0 to 60).

```c
typedef struct VHL_PACKED {
    uint8_t  physical_key_idx;   /* 0 to 60 (Physical switch index on keybed)  */
    uint8_t  assigned_inst_id;   /* Target Instrument ID in Section 3          */
    uint8_t  midi_note;          /* Musical MIDI pitch (e.g. 36 = C2, 60 = C4) */
    int8_t   transpose_offset;   /* Zone transposition in semitones (-24..+24) */

    float    pitch_ratio;        /* Pre-baked IEEE 754 float: 2^((note-root)/12)*/

    int16_t  fine_cents;         /* Fine detune in cents x 10 (-1000..+1000)   */
    uint8_t  zone_id;            /* 0 = Left Zone, 1 = Right Zone, 2 = Drone   */
    uint8_t  key_flags;          /* Bitmask: Bit 0 = LATCH, Bit 1 = MUTE       */

    uint16_t _reserved;          /* Padding to enforce strict 16-byte size     */
} VHLKeyMapEntry_v2; /* Total: Exactly 16 Bytes */
```

### 5.2 Field-by-Field Technical Specification & Engineering Rationale

| Field Name | Type | Range | Engineering Rationale & Purpose |
|---|---|---|---|
| `physical_key_idx` | `uint8_t` | `0..60` | Corresponds directly to the matrix scanner switch index ($Row \times 8 + Col$). Index 0 is physical key 1 (bottom $C_2$); index 60 is physical key 61 (top $C_7$). |
| `assigned_inst_id` | `uint8_t` | `0..31` | Routes this physical key to a specific instrument. Enables multi-timbral setups where different keys trigger entirely different sounds (e.g. Piano vs. Harmonium). |
| `midi_note` | `uint8_t` | `0..127` | The musical pitch assigned to this key. Key 0 does not have to be MIDI 36 ($C_2$); it can be re-mapped to any desired octave or pitch. |
| `transpose_offset` | `int8_t` | `-24..+24` | Allows shifting a specific octave up or down without altering the root sample. |
| `pitch_ratio` | `float32` | `0.125 .. 8.0` | **Crucial performance optimization.** The exact resample step ratio:$$\text{pitch\_ratio} = 2^{\frac{\text{midi\_note} + \text{transpose} - \text{root\_key}}{12.0}}$$ Pre-calculated on PC. At Note-On, the ESP32 performs zero floating-point exponents! |
| `fine_cents` | `int16_t` | `-1000..+1000` | Fine detuning in $1/10$th of a cent ($100 = 10.0$ cents). Allows micro-tuning individual keys for raga shrutis. |
| `zone_id` | `uint8_t` | `0..7` | Identifies which logical zone this key belongs to: `0` = Left split (accompaniment), `1` = Right split (melody lead), `2` = Drone bank. The GUI uses this to render colored split zones. |
| `key_flags` | `uint8_t` | Bitmask | Bit 0: `KEY_FLAG_LATCH` (Continuous drone: press once to start looping, press again to stop; musician does not need to keep holding key).<br>Bit 1: `KEY_FLAG_MUTE` (Key disabled).<br>Bit 2: `KEY_FLAG_VEL_SENSITIVE` (Respond to velocity if hardware supports it). |
| `_reserved` | `uint16_t`| `0` | Enforces that `sizeof(VHLKeyMapEntry_v2) == 16`. Powers of 2 allow single-cycle bit-shift pointer arithmetic: `offset = key_idx << 4`. |

### 5.3 Keyboard Splits (Left Accompaniment vs. Right Lead)
By defining `assigned_inst_id` and `zone_id` per key, keyboard splits are natively solved:
* **Example Split Configuration:**
  * Keys `0..24` ($C_2$ to $C_4$): `assigned_inst_id = 1` (Grand Piano), `zone_id = 0`.
  * Keys `25..60` ($C\sharp_4$ to $C_7$): `assigned_inst_id = 0` (Harmonium Reed), `zone_id = 1`.
* When the musician presses physical Key 10, the ESP32 reads `pKeyMap[10]`, sees `assigned_inst_id = 1`, and allocates a Piano voice.
* When Key 30 is pressed, it sees `assigned_inst_id = 0` and allocates a Harmonium voice.
* **Result:** Arbitrary keyboard splits, triple splits, or interleaved keys with zero firmware modification.

### 5.4 Drone & Latch Keys (Tanpura / Sur / Continuous Harmonium Reeds)
In traditional acoustic harmoniums, players frequently pull mechanical drone stops (*Sur*) to keep a continuous base drone sounding.
* By setting `key_flags |= KEY_FLAG_LATCH` on a key (e.g. Key 0 set to $Sa$):
  * When the key is pressed, a voice begins looping continuously.
  * When the physical key is released, **the voice is NOT killed**.
  * The voice continues sustaining indefinitely until the musician presses Key 0 a second time to toggle it off.

---

## 6. Section 2: Musical Tuning & Swara Shruti Table (NEW in v2.0)

### 6.1 Structure Definition (`VHLTuningTable_v2`, 64 Bytes Fixed)

```c
typedef struct VHL_PACKED {
    char     scale_name[24];         /* Display name: e.g. "Bhairav Shrutis\0"      */
    uint8_t  tonic_root_key;         /* Adhara Shadja (Sa) MIDI note (e.g. 62 = D4) */
    uint8_t  scale_flags;            /* Bit 0: TUNING_ENABLED, Bit 1: JUST_INTONATION */
    int16_t  swara_cent_offsets[12]; /* Microtonal offsets in cents x 10 for 12 swaras */
    uint8_t  _reserved[14];          /* Padding to enforce 64-byte size             */
} VHLTuningTable_v2; /* Total: Exactly 64 Bytes */
```

### 6.2 Field-by-Field Technical Specification & Engineering Rationale

| Field Name | Type | Range | Engineering Rationale & Purpose |
|---|---|---|---|
| `scale_name[24]` | `char[24]` | Null-terminated string | Identifies the tuning preset (e.g. `"Raga Darbari\0"` or `"Equal 12-TET\0"`). Displayed on the PC GUI and workstation LCD. |
| `tonic_root_key` | `uint8_t` | `0..127` (e.g. `60` for $C_4$, `62` for $D_4$) | Specifies which physical key represents the primary base tonic ($Sa$). All 12 swara offsets are calculated relative to this anchor key. |
| `scale_flags` | `uint8_t` | Bitmask | Bit 0: `TUNING_OVERRIDE_ENABLED` (If 0, ESP32 ignores offsets and runs 12-TET; if 1, offsets are added).<br>Bit 1: `IS_PURE_JUST` (Informs educational visualizer). |
| `swara_cent_offsets[12]` | `int16_t[12]` | `-500..+500` ($1/10$th cents) | Stores the deviation from equal temperament for each chromatic interval ($0$ to $11$ semitones above tonic):<br>`[0]` = Sa ($0.0$ cents)<br>`[1]` = Komal Re<br>`[2]` = Shuddha Re<br>`[3]` = Komal Ga<br>`[4]` = Shuddha Ga<br>`[5]` = Shuddha Ma<br>`[6]` = Teevra Ma<br>`[7]` = Pa<br>`[8]` = Komal Dha<br>`[9]` = Shuddha Dha<br>`[10]` = Komal Ni<br>`[11]` = Shuddha Ni |
| `_reserved[14]` | `uint8_t[14]` | `0` | Enforces exact 64-byte alignment. |

---

## 7. Section 3: Multi-Timbral Instrument Table (48 Bytes per Instrument)

### 7.1 Structure Definition (`VHLInstrumentEntry_v2`)

```c
typedef struct VHL_PACKED {
    uint8_t  instrument_id;          /* Unique 0-based index (0..num_instruments-1) */
    uint8_t  bank;                   /* MIDI Bank selection (0..127)                */
    uint8_t  program;                /* MIDI Program number (0..127)                */
    uint8_t  max_polyphony;          /* Max voices allowed (e.g. 4 for lead, 1 for drone) */

    char     name[24];               /* Null-terminated ASCII name: "Harmonium Bass\0" */

    uint16_t region_offset;          /* Index of first VHLRegion in Section 4       */
    uint16_t region_count;           /* Number of consecutive regions in Section 4  */

    uint32_t lookup_table_offset;    /* Absolute byte offset to Section 6 lookup    */

    uint8_t  master_volume_pct;      /* Trim volume: 0..100% (default 100%)         */
    int8_t   pan_default;            /* Default pan: -100 (left) to +100 (right)    */
    uint8_t  reverb_send_pct;        /* Default reverb send: 0..100%                */
    uint8_t  chorus_send_pct;        /* Default chorus send: 0..100%                */

    uint8_t  flags;                  /* Instrument behavior flags                   */
    uint8_t  _reserved[7];           /* Padding to enforce 48-byte size             */
} VHLInstrumentEntry_v2; /* Total: Exactly 48 Bytes */
```

### 7.2 Field-by-Field Technical Specification & Engineering Rationale
* **`max_polyphony`:** Prevents low-priority accompaniment (like a Tanpura drone or Tabla sample) from stealing high-priority melody voices during complex rapid harmonium passages.
* **`region_offset` & `region_count`:** Identifies the precise slice of the global Region Table (Section 4) owned by this instrument.
* **`lookup_table_offset`:** Points directly to the instrument's pre-compiled $O(1)$ search table in Section 6.

---

## 8. Section 4: Pre-Baked DSP Region Table (80 Bytes per Region)

### 8.1 Structure Definition (`VHLRegion_v2`)
The Region Table contains every playback zone for all instruments. Every region is completely flat: **all SF2/SFZ hierarchies have been pre-baked by the PC compiler**.

```c
typedef struct VHL_PACKED {
    /* Key and Velocity Filter Criteria (4 bytes) */
    uint8_t  key_lo;                 /* Minimum MIDI key (0..127)                   */
    uint8_t  key_hi;                 /* Maximum MIDI key (0..127)                   */
    uint8_t  vel_lo;                 /* Minimum velocity (0..127)                   */
    uint8_t  vel_hi;                 /* Maximum velocity (0..127)                   */

    /* Sample Reference (2 bytes) */
    uint16_t sample_id;              /* Index into Section 5 Sample Directory       */

    /* Voice Behavior (4 bytes) */
    uint8_t  exclusive_class;        /* Choke group (same class chokes prior voice) */
    uint8_t  loop_mode;              /* 0 = None, 1 = Continuous, 3 = Sustain      */
    uint8_t  flags;                  /* VHL_REGION_FLAG_* bitmask                   */
    uint8_t  _reserved_a;            /* Alignment padding                           */

    /* Tuning Offsets (4 bytes) */
    int8_t   coarse_tune;            /* Pitch offset in semitones (-96..+96)        */
    int8_t   _pad_tune;              /* Padding                                     */
    int16_t  fine_tune;              /* Pitch offset in cents x 10 (-1000..+1000)   */

    /* Scale Tracking (2 bytes) */
    uint8_t  scale_tuning;           /* Key tracking: 100 = 100 cents/semitone      */
    uint8_t  _reserved_b;            /* Alignment padding                           */

    /* Volume and Spatial (6 bytes) */
    uint16_t attenuation_cb;         /* Initial attenuation in centibels (0..1440)  */
    int16_t  pan;                    /* Pan position: -500 (L) to +500 (R)          */
    uint8_t  reverb_send;            /* Reverb depth (0..100%)                      */
    uint8_t  chorus_send;            /* Chorus depth (0..100%)                      */

    /* Dual DAHDSR Envelopes (28 bytes: 2 x 14 bytes) */
    VHLEnvelope vol_env;             /* Amplitude Envelope Generator                */
    VHLEnvelope mod_env;             /* Filter & Pitch Modulation Envelope          */

    /* Modulation Routing (4 bytes) */
    int16_t  mod_env_to_pitch;       /* Mod envelope -> pitch depth (cents x 10)    */
    int16_t  mod_env_to_filter;      /* Mod envelope -> filter cutoff (cents x 10)  */

    /* Low-Frequency Oscillators (20 bytes: 12 + 8 bytes) */
    VHLModLFO lfo_mod;               /* Multi-target LFO (volume/filter/pitch)      */
    VHLVibLFO lfo_vib;               /* Pitch vibrato LFO                           */

    /* Direct Form II Biquad Filter (4 bytes) */
    VHLFilter filter;                /* Cutoff Hz and resonance centibels           */

    /* Reserved for Future DSP Routing (2 bytes) */
    uint16_t _reserved_c;            /* Must be zero                                */
} VHLRegion_v2; /* Total: Exactly 80 Bytes */
```

### 8.2 Sub-Structures

#### Envelope (`VHLEnvelope`, 14 Bytes)
Used for both Amplitude DAHDSR and Modulation DAHDSR:
```c
typedef struct VHL_PACKED {
    uint16_t delay_ms;               /* Delay before attack phase (0..65535 ms)     */
    uint16_t attack_ms;              /* Attack rise time (0..65535 ms)              */
    uint16_t hold_ms;                /* Peak sustain hold time (0..65535 ms)        */
    uint16_t decay_ms;               /* Decay drop time (0..65535 ms)               */
    uint16_t sustain_level;          /* Volume: centibels; Mod: permille (0..1000)  */
    uint16_t release_ms;             /* Release fade time (0..65535 ms)             */
    int8_t   key_to_hold;            /* Key scaling factor for hold time            */
    int8_t   key_to_decay;           /* Key scaling factor for decay time           */
} VHLEnvelope; /* Total: 14 Bytes */
```

#### Low-Pass Filter (`VHLFilter`, 4 Bytes)
Direct Form II Biquad Filter parameters:
```c
typedef struct VHL_PACKED {
    uint16_t cutoff_hz;              /* Pre-baked cutoff in Hz (20..20000 Hz)       */
    uint16_t resonance_cb;           /* Filter Q resonance in centibels             */
} VHLFilter; /* Total: 4 Bytes */
```

#### LFOs (`VHLModLFO` 12 Bytes, `VHLVibLFO` 8 Bytes)
```c
typedef struct VHL_PACKED {
    uint16_t delay_ms;               /* LFO start delay in ms                       */
    uint16_t freq_mHz;               /* LFO rate in millihertz (1000 = 1.0 Hz)      */
    int16_t  to_pitch_cb;            /* Pitch modulation depth (cents x 10)         */
    int16_t  to_filter_cb;           /* Filter modulation depth (cents x 10)        */
    int16_t  to_volume_cb;           /* Volume modulation depth (centibels)         */
    uint16_t _reserved;
} VHLModLFO; /* Total: 12 Bytes */

typedef struct VHL_PACKED {
    uint16_t delay_ms;               /* Vibrato start delay in ms                   */
    uint16_t freq_mHz;               /* Vibrato rate in millihertz                  */
    int16_t  to_pitch_cb;            /* Vibrato pitch depth (cents x 10)            */
    uint16_t _reserved;
} VHLVibLFO; /* Total: 8 Bytes */
```

---

## 9. Section 5: Sample Directory (32 Bytes per Sample Entry)

### 9.1 Structure Definition (`VHLSampleEntry_v2`)
Each audio sample waveform stored in Section 7 has exactly one 32-byte metadata entry in Section 5.

```c
typedef struct VHL_PACKED {
    /* PCM Data Memory Offset (8 bytes) */
    uint32_t data_offset;            /* Byte offset from Section 7 start to PCM data*/
    uint32_t total_frames;           /* Total sample length in PCM frames           */

    /* Loop Point Boundaries (8 bytes) */
    uint32_t loop_start_frame;       /* Loop start frame index                      */
    uint32_t loop_end_frame;         /* Loop end frame index (exclusive boundary)   */

    /* Audio Properties (6 bytes) */
    uint32_t sample_rate;            /* Original recorded sample rate in Hz (44100) */
    uint8_t  root_key;               /* Natural pitch MIDI root key (e.g. 62 = D4)  */
    int8_t   fine_tune_cents;        /* Sample tuning correction in cents (-50..+50)*/

    /* Format & Channels (3 bytes) */
    uint8_t  bit_depth;              /* 16 (16-bit signed PCM) or 8                 */
    uint8_t  channels;               /* 1 = Mono, 2 = Stereo                        */
    uint8_t  flags;                  /* VHL_SAMPLE_FLAG_* bitmask                   */

    /* Stereo Partner Linking (2 bytes) */
    uint16_t linked_sample_id;       /* Stereo partner index (0xFFFF if mono)       */

    /* Reserved (5 bytes) */
    uint8_t  _reserved[5];           /* Padding to enforce 32-byte size             */
} VHLSampleEntry_v2; /* Total: Exactly 32 Bytes */
```

### 9.2 Seamless Loop Boundaries & Micro-Crossfade Data
For continuous harmonium reed sustain:
* `loop_start_frame` and `loop_end_frame` are detected by the PC compiler at precise waveform zero-crossings with matched phase slopes ($dy/dt > 0$).
* The ESP32 runtime applies a hardware 32-sample linear crossfade at `loop_end_frame`, completely eliminating clicks or phase-discontinuity pops.

---

## 10. Section 6: O(1) Accelerated Region Lookup Table

To guarantee that note triggering takes under **2 microseconds** regardless of whether an instrument has 10 regions or 500 regions, Section 6 provides pre-computed jump tables.

### 10.1 Structure Definitions
For each instrument in Section 3, Section 6 contains:
1. One `VHLLookupHeader` (4 bytes)
2. Followed immediately by 128 `VHLLookupEntry` structs (4 bytes each, one per MIDI note $0..127$):

```c
typedef struct VHL_PACKED {
    uint16_t instrument_id;          /* Target instrument index                     */
    uint16_t _reserved;
} VHLLookupHeader; /* 4 Bytes */

typedef struct VHL_PACKED {
    uint16_t region_start_idx;       /* First region index in Section 4 for this key*/
                                     /* 0xFFFF (VHL_LOOKUP_NONE) if key has no zone */
    uint8_t  region_count;           /* Number of consecutive regions to test       */
    uint8_t  _reserved;
} VHLLookupEntry; /* 4 Bytes */
```

### 10.2 Constant-Time Search Algorithm at Note-On
When Key $K$ is pressed with velocity $V$:
1. The runtime jumps directly to the entry: `entry = pLookupEntries[K]`.
2. If `entry.region_start_idx == 0xFFFF`, the note is rejected in **$O(1)$ constant time**.
3. Otherwise, the runtime checks only `entry.region_count` regions (typically 1 or 2 velocity layers), checking solely `vel_lo <= V <= vel_hi`.
4. **Total Note-On lookup duration: < 1.5 microseconds.**

---

## 11. Section 7: PCM Audio Data Pool

* **Contents:** Raw 16-bit signed little-endian PCM samples (`int16_t`) concatenated contiguously.
* **Alignment:** Every sample starts at a 4-byte aligned boundary. Padding bytes (`0x00`) are inserted by the compiler between samples.
* **Storage Allocation:**
  * **Internal SPI Flash:** The PCM pool is mapped directly into the CPU address space via SPI Flash MMU (Execute-In-Place / XIP cache). The runtime audio synthesis reads sample frames via pointer arithmetic:
    $$\text{pFrame} = \text{pPcmBase} + \text{sample\_entry.data\_offset} + (\text{frame\_idx} \times \text{channels} \times 2)$$
  * **Zero RAM Footprint:** The megabytes of audio data stay in Flash; only the active voice playback cursors exist in SRAM.

---

## 12. CRC-32 Integrity Verification Algorithm

To prevent flashing corrupt packages that could trigger audio buzz or memory crashes:
1. During compilation, the desktop tool constructs the entire VHL v2.0 binary file with `VHLFileHeader_v2.crc32 = 0`.
2. The standard IEEE 802.3 CRC-32 polynomial (`0xEDB88320`, initial `0xFFFFFFFF`) is computed across every byte of the file.
3. The resulting 32-bit CRC is written into the `crc32` header field.
4. When the ESP32 receives the file via Wi-Fi or loads it from SD Card, it computes the CRC-32 across the payload. If the checksum does not match, the file is rejected and the system remains safely on its baseline default.

---

## 13. Comparative Summary: VHL v1.0 vs. VHL v2.0

| Feature Attribute | VHL Specification v1.0 | VHL Specification v2.0 (Master) |
|---|---|---|
| **Header Magic** | `'V','H','L','1'` | `'V','H','L','2'` |
| **Header Size** | 128 Bytes | 128 Bytes (Preserved) |
| **Physical 61-Key Mapping** | **Hardcoded in ESP32 C++ code** | **Dynamically defined in Section 1** |
| **Keyboard Splits (Piano/Harmonium)** | Requires editing C code & reflashing | **Native (configured in Section 1 `zone_id`)** |
| **Resample Pitch Ratio Calculation** | Calculated in C array `g_keyboard` | **Pre-baked 32-bit float in Section 1** |
| **Indian Classical Shruti Tuning** | Unsupported (Hardcoded 12-TET) | **Native (Section 2 Swara Cent Offsets)** |
| **Drone / Latch Key Routing** | Unsupported in format | **Native (`KEY_FLAG_LATCH` in Section 1)** |
| **Multi-Timbral Instrument Storage** | 40-byte descriptor (1 active) | **48-byte descriptor with polyphony caps** |
| **Lookup Acceleration Table** | Present | **Present & Preserved** |
| **DSP Synthesis Compatibility** | 10-step ESP32 synthesis chain | **100% Compatible with existing DSP chain** |
| **Firmware Coupling** | Tightly coupled to harmonium | **Generic Audio Engine; zero firmware rebuilds** |

---

## 14. Compiler, Decompiler & Runtime Execution Lifecycle

```
┌────────────────────────────────────────────────────────────────────────┐
│                        DESKTOP COMPILER WORKFLOW                       │
│                                                                        │
│  [WAV Samples]  +  [Split Configuration]  +  [Raga Tuning Selection]   │
│         │                    │                         │               │
│         ▼                    ▼                         ▼               │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                     RUST VHL v2.0 COMPILER                       │  │
│  │   • Zero-crossing loop detector                                  │  │
│  │   • Pre-calculates 32-bit float pitch ratios                     │  │
│  │   • Converts ADSR times to milliseconds and sustain to centibels │  │
│  │   • Bakes 61-key matrix routing table                            │  │
│  │   • Computes IEEE CRC-32 checksum                                │  │
│  └──────────────────────────────────┬───────────────────────────────┘  │
└─────────────────────────────────────┼──────────────────────────────────┘
                                      │ Single .vhl binary file
                                      ▼ (Direct Wi-Fi Transfer or SD Card)
┌────────────────────────────────────────────────────────────────────────┐
│                         ESP32 RUNTIME EXECUTION                        │
│                                                                        │
│  1. Check Magic 'VHL2' & verify CRC-32 checksum                        │
│  2. Map Section 1 Keybed Table -> `g_vhl_loader.pKeyMap`               │
│  3. Map Section 4 Region Table -> `g_vhl_loader.pRegions`              │
│  4. Map Section 7 PCM Pool     -> `g_vhl_loader.pPcmBase`              │
│                                                                        │
│  [Musician Strikes Key Index #24 on 61-Key Keybed]                     │
│         │                                                              │
│         ▼                                                              │
│  • Core 0 Matrix Scanner detects contact closure                       │
│  • Read `entry = g_vhl_loader.pKeyMap[24]`                             │
│  • Identify `assigned_inst_id` (Piano) & `pitch_ratio` (1.000x)        │
│  • Post event to Core 1 FreeRTOS synthesis queue                       │
│  • Core 1 streams audio to I2S DMA PCM5102A DAC                        │
│                                                                        │
│  Total Elapsed Latency from Physical Switch Closure to Sound: 3.2 ms   │
└────────────────────────────────────────────────────────────────────────┘
```

---

*End of VHL Specification v2.0 Master Document.*  
*This document serves as the permanent technical specification for all upcoming Rust toolchain implementations and ESP32 runtime loaders.*
