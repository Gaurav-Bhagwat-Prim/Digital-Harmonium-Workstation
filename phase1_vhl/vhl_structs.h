/*
 * vhl_structs.h
 * Virtual Harmonium Lab — Binary Structure Definitions
 * VHL Format Specification v1.0
 *
 * This header defines every binary structure that physically exists
 * inside a .vhl file. It is the shared contract between:
 *
 *   VHL Builder (desktop, C++/Python)
 *   ESP32 Runtime (embedded firmware, C)
 *
 * RULES:
 *   - No functions. Only data structures.
 *   - All multi-byte integers are LITTLE-ENDIAN.
 *   - All floats are IEEE 754 single-precision (32-bit).
 *   - All structures are packed (no implicit compiler padding).
 *   - IDs are used instead of pointers.
 *   - Offsets are byte offsets from the start of the file.
 *
 * VHL FILE LAYOUT (sections in order):
 *   [0] VHLFileHeader          (128 bytes, fixed)
 *   [1] VHLInstrumentEntry[]   (num_instruments entries)
 *   [2] VHLRegion[]            (total_region_count entries, all instruments)
 *   [3] VHLSampleEntry[]       (total_sample_count entries)
 *   [4] VHLLookupHeader[]      (one per instrument)
 *   [5] VHLLookupEntry[]       (per-instrument per-key entries, variable)
 *   [6] PCM Sample Data        (raw int16_t PCM, 4-byte aligned per sample)
 */

#ifndef VHL_STRUCTS_H
#define VHL_STRUCTS_H

#include <stdint.h>

/* =========================================================================
 * COMPILER PORTABILITY
 * ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)
#   define VHL_PACKED __attribute__((packed))
#elif defined(_MSC_VER)
#   define VHL_PACKED
#   pragma pack(push, 1)
#else
#   define VHL_PACKED
#   warning "Unknown compiler: structure packing may not work correctly"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * CONSTANTS
 * ========================================================================= */

/* Magic bytes at byte 0 of every VHL file */
#define VHL_MAGIC_0          'V'
#define VHL_MAGIC_1          'H'
#define VHL_MAGIC_2          'L'
#define VHL_MAGIC_3          '1'

/* Version */
#define VHL_VERSION_MAJOR    1
#define VHL_VERSION_MINOR    0

/* Limits */
#define VHL_MAX_INSTRUMENTS  32
#define VHL_MAX_REGIONS      4096
#define VHL_MAX_SAMPLES      1024
#define VHL_NAME_LEN         24     /* Including null terminator */
#define VHL_MIDI_KEYS        128
#define VHL_LOOKUP_NONE      0xFFFF /* Sentinel: no regions for this key */

/* Loop modes (stored in VHLRegion.loop_mode) */
#define VHL_LOOP_NONE        0      /* Play once, no loop */
#define VHL_LOOP_CONTINUOUS  1      /* Loop while key held AND during release */
#define VHL_LOOP_SUSTAIN     3      /* Loop while key held; play to end on release */

/* Envelope sustain units */
#define VHL_VOL_SUSTAIN_UNIT_CB   /* centibels: 0 = full level, 1440 = silence */
#define VHL_MOD_SUSTAIN_UNIT_PM   /* permille: 0 = silent, 1000 = full level */

/* Region flags (VHLRegion.flags) */
#define VHL_REGION_FLAG_STEREO_LINKED   (1 << 0)  /* Region has a stereo partner */
#define VHL_REGION_FLAG_VEL_ATTEN       (1 << 1)  /* Apply velocity→attenuation at Note-On */
#define VHL_REGION_FLAG_MOD_ENV_ACTIVE  (1 << 2)  /* Mod envelope has non-trivial timing */
#define VHL_REGION_FLAG_FILTER_ACTIVE   (1 << 3)  /* Filter is below Nyquist, must apply */
#define VHL_REGION_FLAG_MOD_LFO_ACTIVE  (1 << 4)  /* Mod LFO has non-zero depth */
#define VHL_REGION_FLAG_VIB_LFO_ACTIVE  (1 << 5)  /* Vib LFO has non-zero depth */
#define VHL_REGION_FLAG_RESERVED_6      (1 << 6)
#define VHL_REGION_FLAG_RESERVED_7      (1 << 7)

/* Sample entry flags (VHLSampleEntry.flags) */
#define VHL_SAMPLE_FLAG_LOOP_VALID      (1 << 0)  /* Loop points are valid */
#define VHL_SAMPLE_FLAG_STEREO_RIGHT    (1 << 1)  /* This is the right channel of a stereo pair */
#define VHL_SAMPLE_FLAG_24BIT_PAD       (1 << 2)  /* Extra byte per sample is stored after 16-bit data */
#define VHL_SAMPLE_FLAG_RESERVED_3      (1 << 3)
#define VHL_SAMPLE_FLAG_RESERVED_4      (1 << 4)
#define VHL_SAMPLE_FLAG_RESERVED_5      (1 << 5)
#define VHL_SAMPLE_FLAG_RESERVED_6      (1 << 6)
#define VHL_SAMPLE_FLAG_RESERVED_7      (1 << 7)

/* File header flags (VHLFileHeader.flags) */
#define VHL_FILE_FLAG_STEREO_OUTPUT     (1 << 0)  /* File was built for stereo output */
#define VHL_FILE_FLAG_HAS_LOOKUP_TABLE  (1 << 1)  /* Lookup acceleration table is present */
#define VHL_FILE_FLAG_SAMPLES_IN_PSRAM  (1 << 2)  /* Hint: preload samples to PSRAM */
#define VHL_FILE_FLAG_RESERVED          0xFFF8

/* =========================================================================
 * SECTION 0: FILE HEADER (128 bytes, fixed size)
 *
 * The very first structure in every VHL file.
 * Contains magic bytes, version, byte offsets to every section, and CRC.
 * The runtime reads and validates this structure first, before any other
 * data access.
 * ========================================================================= */
typedef struct VHL_PACKED {

    /* Identity (4 bytes) */
    uint8_t  magic[4];          /* Must be { 'V','H','L','1' }              */

    /* Version (2 bytes) */
    uint8_t  version_major;     /* Incompatible format change → bump major  */
    uint8_t  version_minor;     /* Additive change only → bump minor        */

    /* File-level flags (2 bytes) */
    uint16_t flags;             /* See VHL_FILE_FLAG_* constants             */

    /* Instrument count (2 bytes) */
    uint16_t num_instruments;   /* Total instruments in this VHL file        */

    /* Global counts (4 bytes) */
    uint16_t total_region_count;  /* Total regions across all instruments    */
    uint16_t total_sample_count;  /* Total samples across all instruments    */

    /* Audio parameters (6 bytes) */
    uint32_t target_sample_rate;  /* Target I2S output rate in Hz (e.g. 44100) */
    uint16_t dma_block_size;      /* Recommended DMA block size in frames    */

    /* Section byte offsets from file start (40 bytes, 10 × uint32_t) */
    uint32_t offset_instrument_table;  /* Start of VHLInstrumentEntry[]      */
    uint32_t offset_region_table;      /* Start of VHLRegion[]               */
    uint32_t offset_sample_directory;  /* Start of VHLSampleEntry[]          */
    uint32_t offset_lookup_table;      /* Start of VHLLookupHeader[]         */
    uint32_t offset_pcm_data;          /* Start of raw PCM sample data       */
    uint32_t offset_reserved_1;        /* Reserved for future section        */
    uint32_t offset_reserved_2;        /* Reserved for future section        */
    uint32_t offset_reserved_3;        /* Reserved for future section        */
    uint32_t offset_reserved_4;        /* Reserved for future section        */
    uint32_t offset_reserved_5;        /* Reserved for future section        */

    /* File integrity (8 bytes) */
    uint32_t total_file_size;   /* Total byte length of the VHL file         */
    uint32_t crc32;             /* CRC-32 of entire file (crc32 field = 0 during computation) */

    /* Build metadata (8 bytes) */
    uint32_t build_timestamp;   /* Unix timestamp of Builder run             */
    uint16_t builder_version;   /* Builder tool version (major<<8 | minor)   */
    uint16_t _reserved_header;  /* Padding — must be zero                    */

    /* Human-readable build tag (32 bytes) */
    char     build_tag[32];     /* e.g., "HarmoniumV1.0-Release\0"           */

    /* Reserved for future use (20 bytes) — must be zero */
    uint8_t  _reserved[20];

} VHLFileHeader;  /* Total: 128 bytes */


/* =========================================================================
 * SECTION 1: INSTRUMENT TABLE
 *
 * Array of VHLInstrumentEntry, one per instrument.
 * Count: VHLFileHeader.num_instruments
 * Each entry describes one preset (bank + program number combination).
 * ========================================================================= */
typedef struct VHL_PACKED {

    uint8_t  instrument_id;     /* Unique 0-based index for this instrument  */
    uint8_t  bank;              /* MIDI bank number (0-127)                  */
    uint8_t  program;           /* MIDI program number (0-127)               */
    uint8_t  flags;             /* Reserved for instrument-level flags        */

    char     name[VHL_NAME_LEN]; /* Display name, null-terminated ASCII      */

    /* Region Table slice for this instrument */
    uint16_t region_offset;     /* Index of first VHLRegion for this instrument
                                   (0-based index into global Region Table)  */
    uint16_t region_count;      /* Number of VHLRegions for this instrument  */

    /* Lookup Table slice for this instrument */
    uint32_t lookup_table_offset; /* Byte offset into Lookup Table section
                                     where this instrument's VHLLookupHeader
                                     begins (absolute from file start)       */

    /* Reserved (4 bytes) */
    uint32_t _reserved;

} VHLInstrumentEntry;  /* Total: 40 bytes */


/* =========================================================================
 * ENVELOPE PARAMETERS (embedded inside VHLRegion)
 *
 * Used for both the Volume Envelope and the Modulation Envelope.
 * All times are stored in milliseconds as uint16_t (0-65535 ms, i.e. 0-65s).
 *
 * For the Volume Envelope:
 *   sustain_level = attenuation in centibels (0 = full level, 1440 = silence)
 *
 * For the Modulation Envelope:
 *   sustain_level = level in permille (0 = 0%, 1000 = 100%)
 *
 * keynumToHold / keynumToDecay: scaling factor applied per semitone
 * distance from MIDI key 60. Stored as (original_SF2_value / 10) to
 * allow int8_t storage with 1/10 cent precision.
 *   formula: effective_time = base_time * 2^(key_to_X * (60 - played_key) / 1200)
 * ========================================================================= */
typedef struct VHL_PACKED {

    uint16_t delay_ms;          /* Delay phase duration in ms                */
    uint16_t attack_ms;         /* Attack phase duration in ms               */
    uint16_t hold_ms;           /* Hold phase duration in ms                 */
    uint16_t decay_ms;          /* Decay phase duration in ms                */
    uint16_t sustain_level;     /* See unit description above                */
    uint16_t release_ms;        /* Release phase duration in ms              */

    int8_t   key_to_hold;       /* Key number scaling for hold time (×0.1 semitones) */
    int8_t   key_to_decay;      /* Key number scaling for decay time (×0.1 semitones) */

} VHLEnvelope;  /* Total: 14 bytes */


/* =========================================================================
 * LFO PARAMETERS (embedded inside VHLRegion)
 *
 * Used for both the Modulation LFO and the Vibrato LFO.
 * Frequency is stored in millihertz (1 unit = 0.001 Hz).
 *   Range: 0 to 65535 mHz = 0 to 65.535 Hz (more than sufficient for LFO)
 *
 * Modulation depths are stored in centibels:
 *   to_pitch_cb: cents × 10 (e.g., 100 = 10 cents of pitch deviation)
 *   to_filter_cb: cents × 10 applied to filter cutoff
 *   to_volume_cb: centibels of attenuation at maximum LFO deviation
 *
 * The Vibrato LFO only targets pitch, so its struct is smaller.
 * ========================================================================= */
typedef struct VHL_PACKED {

    uint16_t delay_ms;          /* Delay before LFO starts, in ms           */
    uint16_t freq_mHz;          /* LFO frequency in millihertz               */
    int16_t  to_pitch_cb;       /* Pitch modulation depth in cents×10        */
    int16_t  to_filter_cb;      /* Filter cutoff modulation depth in cents×10 */
    int16_t  to_volume_cb;      /* Volume modulation depth in centibels×10   */
    uint16_t _reserved;         /* Padding — must be zero                    */

} VHLModLFO;  /* Total: 12 bytes */

typedef struct VHL_PACKED {

    uint16_t delay_ms;          /* Delay before LFO starts, in ms           */
    uint16_t freq_mHz;          /* LFO frequency in millihertz               */
    int16_t  to_pitch_cb;       /* Pitch modulation depth in cents×10        */
    uint16_t _reserved;         /* Padding — must be zero                    */

} VHLVibLFO;  /* Total: 8 bytes */


/* =========================================================================
 * FILTER PARAMETERS (embedded inside VHLRegion)
 *
 * Second-order IIR biquad low-pass filter (Direct Form II).
 * Parameters are stored pre-baked, including any modulator offsets
 * that are static (not dependent on velocity or LFO/envelope).
 *
 * cutoff_hz: filter cutoff in Hz (1–20000 Hz)
 * resonance_cb: filter Q expressed as resonance in centibels above 0 dB
 *   (SF2 stores this as tenths of dB; 0 = Butterworth Q ~= 0.707)
 *   Stored as raw SF2 centibel value for direct use.
 * ========================================================================= */
typedef struct VHL_PACKED {

    uint16_t cutoff_hz;         /* Base filter cutoff frequency in Hz        */
    uint16_t resonance_cb;      /* Filter Q in centibels (0 = Butterworth)   */

} VHLFilter;  /* Total: 4 bytes */


/* =========================================================================
 * SECTION 2: REGION TABLE
 *
 * Array of VHLRegion. This is the central runtime data structure.
 * Every region is fully pre-baked: all SF2 hierarchy has been resolved,
 * all modulator defaults have been applied, all offsets are absolute.
 *
 * The runtime never performs inheritance resolution. It reads this struct
 * directly and uses its values immediately.
 *
 * Count: VHLFileHeader.total_region_count
 * ========================================================================= */
typedef struct VHL_PACKED {

    /* ----- Key and velocity range (4 bytes) ------------------------------ */
    uint8_t  key_lo;            /* Lowest MIDI key this region responds to   */
    uint8_t  key_hi;            /* Highest MIDI key this region responds to  */
    uint8_t  vel_lo;            /* Lowest velocity this region responds to   */
    uint8_t  vel_hi;            /* Highest velocity this region responds to  */

    /* ----- Sample reference (2 bytes) ------------------------------------ */
    uint16_t sample_id;         /* Index into global VHLSampleEntry[]        */

    /* ----- Voice behavior (4 bytes) -------------------------------------- */
    uint8_t  exclusive_class;   /* Choke group (0 = none; same class = choke) */
    uint8_t  loop_mode;         /* VHL_LOOP_NONE / CONTINUOUS / SUSTAIN      */
    uint8_t  flags;             /* See VHL_REGION_FLAG_* constants            */
    uint8_t  _reserved_a;       /* Padding — must be zero                    */

    /* ----- Tuning (4 bytes) ---------------------------------------------- */
    int8_t   coarse_tune;       /* Pitch offset in semitones (-96 to +96)    */
    int8_t   _pad_tune;         /* Padding                                   */
    int16_t  fine_tune;         /* Pitch offset in cents×10 (-1000 to +1000) */

    /* ----- Pitch scale (2 bytes) ----------------------------------------- */
    uint8_t  scale_tuning;      /* Semitones per MIDI key (0=fixed, 100=equal temp.) */
    uint8_t  _reserved_b;       /* Padding — must be zero                    */

    /* ----- Volume and spatial (6 bytes) ---------------------------------- */
    uint16_t attenuation_cb;    /* Initial attenuation in centibels (0-1440) */
    int16_t  pan;               /* Pan position: -500=full left, +500=full right */
    uint8_t  reverb_send;       /* Reverb send amount (0-100%)               */
    uint8_t  chorus_send;       /* Chorus send amount (0-100%)               */

    /* ----- Volume Envelope (14 bytes) ------------------------------------ */
    VHLEnvelope vol_env;

    /* ----- Modulation Envelope (14 bytes) -------------------------------- */
    VHLEnvelope mod_env;

    /* ----- Modulation Envelope routing (4 bytes) ------------------------- */
    int16_t  mod_env_to_pitch;  /* Mod env → pitch depth, in cents×10       */
    int16_t  mod_env_to_filter; /* Mod env → filter cutoff depth, in cents×10 */

    /* ----- Modulation LFO (12 bytes) ------------------------------------- */
    VHLModLFO lfo_mod;

    /* ----- Vibrato LFO (8 bytes) ----------------------------------------- */
    VHLVibLFO lfo_vib;

    /* ----- Low-Pass Filter (4 bytes) ------------------------------------- */
    VHLFilter filter;

    /* ----- Reserved for future routing (2 bytes) ------------------------- */
    uint16_t _reserved_c;       /* Must be zero. Future: effects routing     */

} VHLRegion;  /* Total: 80 bytes */


/* =========================================================================
 * SECTION 3: SAMPLE DIRECTORY
 *
 * Array of VHLSampleEntry, one per unique sample.
 * Stores metadata only. The actual PCM data is in Section 6 (PCM Data).
 * The runtime uses data_offset to jump directly to the PCM frames.
 *
 * PCM data format: 16-bit signed integer (int16_t), little-endian.
 * Each sample starts at a 4-byte aligned address within the PCM section.
 *
 * Count: VHLFileHeader.total_sample_count
 * ========================================================================= */
typedef struct VHL_PACKED {

    /* ----- PCM data location (8 bytes) ----------------------------------- */
    uint32_t data_offset;       /* Byte offset from start of PCM Data section
                                   to first frame of this sample            */
    uint32_t total_frames;      /* Total number of PCM frames (samples)      */

    /* ----- Loop points (8 bytes) ----------------------------------------- */
    uint32_t loop_start_frame;  /* Loop start position in PCM frames         */
    uint32_t loop_end_frame;    /* Loop end position in PCM frames (exclusive) */

    /* ----- Audio properties (6 bytes) ------------------------------------ */
    uint32_t sample_rate;       /* Original sample rate in Hz                */
    uint8_t  root_key;          /* MIDI root note (0-127)                    */
    int8_t   fine_tune_cents;   /* Fine tune offset in cents (-50 to +50)    */

    /* ----- Format (3 bytes) ----------------------------------------------- */
    uint8_t  bit_depth;         /* PCM bit depth: 8 or 16                    */
    uint8_t  channels;          /* Channel count: 1=mono, 2=stereo           */
    uint8_t  flags;             /* See VHL_SAMPLE_FLAG_* constants           */

    /* ----- Stereo linking (2 bytes) --------------------------------------- */
    uint16_t linked_sample_id;  /* For stereo: partner sample ID (0xFFFF = none) */

    /* ----- Reserved (5 bytes) --------------------------------------------- */
    uint8_t  _reserved[5];      /* Must be zero                              */

} VHLSampleEntry;  /* Total: 32 bytes */


/* =========================================================================
 * SECTION 4: LOOKUP TABLE
 *
 * Accelerates region selection at Note-On time.
 *
 * Structure per instrument:
 *   VHLLookupHeader  (one per instrument)
 *   VHLLookupEntry[] (one per MIDI key 0-127, contiguous after the header)
 *
 * At Note-On:
 *   1. Find this instrument's VHLLookupHeader via instrument's lookup_table_offset
 *   2. Index into entries[played_key] to get VHLLookupEntry
 *   3. Scan VHLRegion[] from region_start_idx for region_count entries
 *   4. Check vel_lo <= vel <= vel_hi for each candidate
 *
 * This reduces region search from a full linear scan to:
 *   O(1) key lookup + O(M) velocity scan where M is typically 1-4
 * ========================================================================= */
typedef struct VHL_PACKED {

    uint16_t instrument_id;     /* Which instrument this header belongs to   */
    uint16_t _reserved;         /* Padding — must be zero                    */
    /* Followed immediately by: VHLLookupEntry entries[VHL_MIDI_KEYS]        */

} VHLLookupHeader;  /* Total: 4 bytes */

typedef struct VHL_PACKED {

    uint16_t region_start_idx;  /* First VHLRegion index for this key
                                   (global index into Region Table)
                                   VHL_LOOKUP_NONE = no regions for this key */
    uint8_t  region_count;      /* Number of regions to scan for this key    */
    uint8_t  _reserved;         /* Padding — must be zero                    */

} VHLLookupEntry;  /* Total: 4 bytes */


/* =========================================================================
 * SECTION 5: PCM SAMPLE DATA
 *
 * Raw 16-bit signed PCM samples, concatenated.
 * No structure: the runtime jumps directly to
 *   (pcm_base + VHLSampleEntry.data_offset)
 * and reads int16_t frames sequentially.
 *
 * Alignment: each sample must start at a 4-byte aligned address.
 * The Builder inserts padding bytes between samples as needed.
 *
 * This section is the largest in the file (typically 80-95% of total size).
 * ========================================================================= */
/* No struct. Accessed as: const int16_t *pcm_start = file_base + header.offset_pcm_data */


/* =========================================================================
 * RUNTIME-ONLY STRUCTURES
 *
 * These structures are NOT written to disk. They exist only in RAM
 * during ESP32 playback. They reference VHLRegion and VHLSampleEntry
 * by index, never duplicating their data.
 *
 * Included here for completeness: the Builder and Runtime both include
 * this header, but only the Runtime instantiates these types.
 * ========================================================================= */

/* Maximum number of simultaneously active voices */
#define VHL_MAX_VOICES       32

/* Number of samples per DSP chunk (amortizes per-chunk computations) */
#define VHL_CHUNK_SIZE       256

/* Number of chunks per DMA block (DMA_BLOCK_SIZE / CHUNK_SIZE) */
#define VHL_CHUNKS_PER_BLOCK 2    /* For a 512-sample DMA block */

/* Resampling: number of integer bits in the fractional position accumulator */
#define VHL_FRAC_BITS        16

/* Envelope phase identifiers */
#define VHL_ENV_PHASE_DELAY       0
#define VHL_ENV_PHASE_ATTACK      1
#define VHL_ENV_PHASE_HOLD        2
#define VHL_ENV_PHASE_DECAY       3
#define VHL_ENV_PHASE_SUSTAIN     4
#define VHL_ENV_PHASE_RELEASE     5
#define VHL_ENV_PHASE_QUICK_REL   6
#define VHL_ENV_PHASE_OFF         7

/* Duration of quick release in ms (exclusive class / voice choke) */
#define VHL_QUICK_RELEASE_MS 40

/*
 * VHLEnvelopeState — runtime per-voice envelope state
 * (not written to file; initialized fresh at each Note-On)
 */
typedef struct {
    uint8_t  phase;             /* VHL_ENV_PHASE_* current state             */
    uint8_t  is_mod;            /* 0=volume envelope, 1=modulation envelope  */
    uint8_t  quick_release;     /* If 1, use VHL_QUICK_RELEASE_MS            */
    uint8_t  _reserved;
    uint32_t chunk_counter;     /* Chunks elapsed in current phase           */
    uint32_t phase_duration;    /* Total chunks for current phase            */
    float    last_value;        /* Last output value (for smooth transitions) */
    float    level_sustain;     /* Cached sustain level (converted to 0-1)   */
} VHLEnvelopeState;  /* 20 bytes */

/*
 * VHLLFOState — runtime per-voice LFO state
 */
typedef struct {
    uint8_t  delay_done;        /* 1 when delay period has elapsed           */
    uint8_t  _reserved[3];
    uint32_t delay_elapsed_ms;  /* Milliseconds elapsed during delay phase   */
    float    delta;             /* Per-chunk increment (sign = direction)     */
    float    previous_point;    /* Last LFO output value                     */
    float    previous_freq_hz;  /* Last frequency (for smooth transitions)    */
} VHLLFOState;  /* 20 bytes */

/*
 * VHLFilterState — runtime per-voice biquad filter delay lines
 */
typedef struct {
    float    x1, x2;           /* Input delay line (previous 2 input samples) */
    float    y1, y2;           /* Output delay line (previous 2 output samples) */
} VHLFilterState;  /* 16 bytes */

/*
 * VHLVoice — complete runtime state for one playing note
 *
 * References VHLRegion and VHLSampleEntry by index (no copies of static data).
 * All mutable state is contained here.
 */
typedef struct {

    /* ----- Identity (6 bytes) -------------------------------------------- */
    uint8_t  active;            /* 1 = voice is playing, 0 = free slot       */
    uint8_t  releasing;         /* 1 = Note-Off received, in release phase    */
    uint8_t  finished;          /* 1 = envelope at OFF, ready for pool return */
    uint8_t  channel;           /* MIDI channel                               */
    uint8_t  key;               /* MIDI note number                           */
    uint8_t  velocity;          /* MIDI velocity at Note-On                   */

    /* ----- Static data references (4 bytes) ------------------------------- */
    uint16_t region_idx;        /* Index into global Region Table             */
    uint16_t sample_idx;        /* Index into global Sample Directory         */

    /* ----- Sample playback state (12 bytes) ------------------------------ */
    uint32_t pos_integer;       /* Current integer sample position            */
    uint32_t pos_frac;          /* Fractional position (Q0.VHL_FRAC_BITS)    */
    int32_t  more_available;    /* Samples to next boundary (loop or end)     */

    /* ----- Pitch (4 bytes) ------------------------------------------------ */
    int32_t  pitch_delta_cents; /* Base pitch delta in cents (computed at Note-On,
                                   updated per chunk for LFO/mod-env)         */

    /* ----- Volume (8 bytes) ----------------------------------------------- */
    float    volume_coeff;      /* Current linearly-interpolated gain coeff  */
    float    gain;              /* Master gain applied at voice creation      */

    /* ----- Envelope state (40 bytes) ------------------------------------- */
    VHLEnvelopeState vol_env;
    VHLEnvelopeState mod_env;

    /* ----- LFO state (40 bytes) ------------------------------------------ */
    VHLLFOState lfo_mod;
    VHLLFOState lfo_vib;

    /* ----- Filter state (16 bytes) --------------------------------------- */
    VHLFilterState filter;

    /* ----- Per-chunk computation arrays ---------------------------------- */
    /* One value per chunk per DMA block. For VHL_CHUNKS_PER_BLOCK = 2:     */
    float    vol_env_array[VHL_CHUNKS_PER_BLOCK];
    float    mod_env_array[VHL_CHUNKS_PER_BLOCK];
    float    mod_lfo_array[VHL_CHUNKS_PER_BLOCK];
    float    vib_lfo_array[VHL_CHUNKS_PER_BLOCK];

    /* ----- Resampling scratch buffer (16 bytes) -------------------------- */
    /* Used when sample read crosses a loop or end boundary                 */
    int16_t  boundary_scratch[8];

    /* ----- Exclusive class (2 bytes) ------------------------------------- */
    uint8_t  exclusive_class;   /* Copied from region at voice activation    */
    uint8_t  instrument_id;     /* Instrument this voice belongs to          */

    /* ----- Padding to 32-byte alignment ---------------------------------- */
    uint8_t  _padding[2];

} VHLVoice;


/* =========================================================================
 * VERIFICATION: Compile-time size checks
 * These will cause a compile error if any structure has unexpected padding.
 * ========================================================================= */
#ifdef __cplusplus
static_assert(sizeof(VHLFileHeader)      == 128, "VHLFileHeader must be 128 bytes");
static_assert(sizeof(VHLInstrumentEntry) == 40,  "VHLInstrumentEntry must be 40 bytes");
static_assert(sizeof(VHLEnvelope)        == 14,  "VHLEnvelope must be 14 bytes");
static_assert(sizeof(VHLModLFO)          == 12,  "VHLModLFO must be 12 bytes");
static_assert(sizeof(VHLVibLFO)          == 8,   "VHLVibLFO must be 8 bytes");
static_assert(sizeof(VHLFilter)          == 4,   "VHLFilter must be 4 bytes");
static_assert(sizeof(VHLRegion)          == 80,  "VHLRegion must be 80 bytes");
static_assert(sizeof(VHLSampleEntry)     == 32,  "VHLSampleEntry must be 32 bytes");
static_assert(sizeof(VHLLookupHeader)    == 4,   "VHLLookupHeader must be 4 bytes");
static_assert(sizeof(VHLLookupEntry)     == 4,   "VHLLookupEntry must be 4 bytes");
#endif


#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#   pragma pack(pop)
#endif

#endif /* VHL_STRUCTS_H */
