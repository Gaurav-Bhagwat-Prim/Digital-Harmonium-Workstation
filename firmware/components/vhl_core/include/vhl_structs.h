/*
 * vhl_structs.h
 * Virtual Harmonium Lab — Binary Structure Definitions
 * VHL Format Specification v2.0
 *
 * This header defines every binary structure that physically exists
 * inside a .vhl file. It is the shared contract between:
 *
 *   VHL Workstation (desktop, Rust / egui)
 *   ESP32 Runtime (embedded firmware, C/C++)
 */

#ifndef VHL_STRUCTS_H
#define VHL_STRUCTS_H

#include <stdint.h>
#include <stdbool.h>

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

/* Magic bytes at byte 0 of every VHL file */
#define VHL_MAGIC_0          'V'
#define VHL_MAGIC_1          'H'
#define VHL_MAGIC_2          'L'
#define VHL_MAGIC_3          '2'

/* Version */
#define VHL_VERSION_MAJOR    2
#define VHL_VERSION_MINOR    0

/* Limits & Constants */
#define VHL_MAX_INSTRUMENTS     32
#define VHL_MAX_REGIONS         4096
#define VHL_MAX_SAMPLES         1024
#define VHL_NAME_LEN            24     /* Including null terminator */
#define VHL_BUILD_TAG_LEN       32
#define VHL_MIDI_KEYS           128
#define VHL_TOTAL_PHYSICAL_KEYS 61
#define VHL_LOOKUP_NONE         0xFFFF /* Sentinel: no regions for this key */

/* File Header Flags */
#define VHL_FILE_FLAG_STEREO_OUTPUT   (1 << 0)
#define VHL_FILE_FLAG_HAS_KEYBED_MAP  (1 << 1)
#define VHL_FILE_FLAG_HAS_TUNING_TABLE (1 << 2)
#define VHL_FILE_FLAG_HAS_LOOKUP_TABLE (1 << 3)
#define VHL_FILE_FLAG_PSRAM_PRELOAD   (1 << 4)

/* Keybed Flags */
#define KEY_FLAG_LATCH                (1 << 0) /* Continuous drone toggle */
#define KEY_FLAG_MUTE                 (1 << 1) /* Disabled key */
#define KEY_FLAG_VEL_SENSITIVE        (1 << 2)

/* Tuning Flags */
#define TUNING_FLAG_ENABLED           (1 << 0)
#define TUNING_FLAG_IS_JUST           (1 << 1)

/* Region Loop Modes (stored in VHLRegion.loop_mode) */
#define VHL_LOOP_NONE                 0        /* Play once, no loop */
#define VHL_LOOP_CONTINUOUS           1        /* Loop while key held AND during release */
#define VHL_LOOP_SUSTAIN              3        /* Loop while key held; play to end on release */

/* Region Flags (VHLRegion.flags) */
#define VHL_REGION_FLAG_STEREO_LINKED  (1 << 0) /* Region has a stereo partner */
#define VHL_REGION_FLAG_VEL_ATTEN      (1 << 1) /* Apply velocity->attenuation at Note-On */
#define VHL_REGION_FLAG_MOD_ENV_ACTIVE (1 << 2) /* Mod envelope has non-trivial timing */
#define VHL_REGION_FLAG_FILTER_ACTIVE  (1 << 3) /* Filter is below Nyquist, must apply */
#define VHL_REGION_FLAG_MOD_LFO_ACTIVE (1 << 4) /* Mod LFO has non-zero depth */
#define VHL_REGION_FLAG_VIB_LFO_ACTIVE (1 << 5) /* Vib LFO has non-zero depth */

/* Sample Flags */
#define VHL_SAMPLE_FLAG_LOOP_VALID     (1 << 0)
#define VHL_SAMPLE_FLAG_STEREO_RIGHT   (1 << 1)

/* SoundFont 2 Modulator Sources */
#define SF_MOD_SRC_NONE                0
#define SF_MOD_SRC_VELOCITY            2        /* Note-on Velocity (0..127) */
#define SF_MOD_SRC_KEY                 3        /* Key Number (0..127) */
#define SF_MOD_SRC_POLY_PRESSURE       10       /* Key Pressure / Poly Aftertouch */
#define SF_MOD_SRC_CHANNEL_PRESSURE    13       /* Channel Pressure / Mono Aftertouch */
#define SF_MOD_SRC_PITCH_WHEEL         14       /* Pitch Bend Wheel (-8192..+8191) */
#define SF_MOD_SRC_PITCH_WHEEL_SENS    16       /* Pitch Bend Sensitivity */
#define SF_MOD_SRC_MOD_ENV             20       /* Internal Modulation Envelope */
#define SF_MOD_SRC_MOD_LFO             21       /* Internal Mod LFO */
#define SF_MOD_SRC_VIB_LFO             22       /* Internal Vibrato LFO */
#define SF_MOD_SRC_CC_FLAG             0x80     /* Bit 7 indicates MIDI Continuous Controller */

/* Standard MIDI CCs */
#define SF_MOD_CC_MOD_WHEEL            1
#define SF_MOD_CC_BREATH               2
#define SF_MOD_CC_VOLUME               7
#define SF_MOD_CC_PAN                  10
#define SF_MOD_CC_EXPRESSION           11
#define SF_MOD_CC_SUSTAIN              64
#define SF_MOD_CC_REVERB               91
#define SF_MOD_CC_CHORUS               93

/* Destinations (SoundFont 2 Standard Generators) */
#define SF_GEN_MOD_LFO_TO_PITCH        5
#define SF_GEN_VIB_LFO_TO_PITCH        6
#define SF_GEN_INITIAL_FILTER_FC       8
#define SF_GEN_INITIAL_FILTER_Q        9
#define SF_GEN_MOD_LFO_TO_FILTER_FC    10
#define SF_GEN_MOD_LFO_TO_VOLUME       11
#define SF_GEN_CHORUS_EFFECTS_SEND     15
#define SF_GEN_REVERB_EFFECTS_SEND     16
#define SF_GEN_PAN                     17
#define SF_GEN_VOL_ENV_ATTACK          34
#define SF_GEN_VOL_ENV_HOLD            35
#define SF_GEN_VOL_ENV_DECAY           36
#define SF_GEN_VOL_ENV_SUSTAIN         37
#define SF_GEN_VOL_ENV_RELEASE         38
#define SF_GEN_MOD_ENV_TO_PITCH        43
#define SF_GEN_MOD_ENV_TO_FILTER_FC    44
#define SF_GEN_INITIAL_ATTENUATION     48
#define SF_GEN_COARSE_TUNE             51
#define SF_GEN_FINE_TUNE               52
#define SF_GEN_SCALE_TUNING            56

/* Curves (Transfer Functions) */
#define SF_CURVE_LINEAR                0
#define SF_CURVE_CONCAVE               1
#define SF_CURVE_CONVEX                2
#define SF_CURVE_SWITCH                3
#define SF_CURVE_CUSTOM                4

/* Direction */
#define SF_DIR_ASCENDING               0
#define SF_DIR_DESCENDING              1

/* Polarity */
#define SF_POL_UNIPOLAR                0
#define SF_POL_BIPOLAR                 1

/* Transforms */
#define SF_TRANSFORM_LINEAR            0
#define SF_TRANSFORM_ABSOLUTE          2

/* ============================================================================
 * SECTION 0: MASTER FILE HEADER (128 Bytes Fixed)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint8_t  magic[4];               /* Must be { 'V','H','L','2' } */
    uint8_t  version_major;          /* 2 */
    uint8_t  version_minor;          /* 0 */
    uint16_t flags;

    uint16_t num_physical_keys;
    uint16_t num_instruments;
    uint16_t total_region_count;
    uint16_t total_sample_count;

    uint32_t target_sample_rate;
    uint16_t dma_block_size;

    uint32_t offset_keybed_map;
    uint32_t offset_tuning_table;
    uint32_t offset_instrument_table;
    uint32_t offset_region_table;
    uint32_t offset_sample_directory;
    uint32_t offset_lookup_table;
    uint32_t offset_pcm_data;
    uint32_t offset_modulator_table;

    uint32_t total_file_size;
    uint32_t crc32;

    uint32_t build_timestamp;
    uint16_t builder_version;
    uint16_t _reserved_meta;

    char     build_tag[VHL_BUILD_TAG_LEN];
    uint16_t total_modulator_count;
    uint8_t  _reserved_future[24];
} VHLFileHeader;

/* ============================================================================
 * SECTION 1: PHYSICAL KEYBED MAPPING TABLE (16 Bytes per Key)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint8_t  physical_key_idx;       /* 0 to 60 */
    uint8_t  assigned_inst_id;       /* Target instrument index */
    uint8_t  midi_note;              /* MIDI pitch (e.g. 36 = C2, 60 = C4) */
    int8_t   transpose_offset;       /* Transpose in semitones (-24..+24) */
    float    pitch_ratio;            /* Pre-calculated resample step: 2^((note-root)/12) */
    int16_t  fine_cents;             /* Fine detune in cents * 10 */
    uint8_t  zone_id;                /* 0 = Left Zone, 1 = Right Zone, 2 = Drone */
    uint8_t  key_flags;              /* KEY_FLAG_* */
    uint8_t  _reserved[4];           /* Enforce 16-byte size */
} VHLKeyMapEntry;

/* ============================================================================
 * SECTION 2: MUSICAL TUNING & SHRUTI TABLE (64 Bytes Fixed)
 * ============================================================================ */
typedef struct VHL_PACKED {
    char     scale_name[VHL_NAME_LEN]; /* Null-terminated ASCII */
    uint8_t  tonic_root_key;           /* Base tonic Sa MIDI note (e.g. 62 = D4) */
    uint8_t  scale_flags;              /* TUNING_FLAG_* */
    int16_t  swara_cent_offsets[12];   /* Microtonal offsets in cents * 10 */
    uint8_t  _reserved[14];            /* Enforce 64-byte size */
} VHLTuningTable;

/* ============================================================================
 * SECTION 3: MULTI-TIMBRAL INSTRUMENT TABLE (48 Bytes per Instrument)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint8_t  instrument_id;
    uint8_t  bank;
    uint8_t  program;
    uint8_t  max_polyphony;
    char     name[VHL_NAME_LEN];
    uint16_t region_offset;
    uint16_t region_count;
    uint32_t lookup_table_offset;
    uint8_t  master_volume_pct;
    int8_t   pan_default;
    uint8_t  reverb_send_pct;
    uint8_t  chorus_send_pct;
    uint8_t  flags;
    uint8_t  _reserved[7];
} VHLInstrumentEntry;

/* Envelope Parameters (14 Bytes) */
typedef struct VHL_PACKED {
    uint16_t delay_ms;
    uint16_t attack_ms;
    uint16_t hold_ms;
    uint16_t decay_ms;
    uint16_t sustain_level;
    uint16_t release_ms;
    int8_t   key_to_hold;
    int8_t   key_to_decay;
} VHLEnvelope;

/* Filter Parameters (4 Bytes) */
typedef struct VHL_PACKED {
    uint16_t cutoff_hz;
    uint16_t resonance_cb;
} VHLFilter;

/* LFO Parameters */
typedef struct VHL_PACKED {
    uint16_t delay_ms;
    uint16_t freq_mHz;
    int16_t  to_pitch_cb;
    int16_t  to_filter_cb;
    int16_t  to_volume_cb;
    uint16_t _reserved;
} VHLModLFO;

typedef struct VHL_PACKED {
    uint16_t delay_ms;
    uint16_t freq_mHz;
    int16_t  to_pitch_cb;
    uint16_t _reserved;
} VHLVibLFO;

/* ============================================================================
 * SECTION 4: REGION TABLE (80 Bytes per Region)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint8_t  key_lo;
    uint8_t  key_hi;
    uint8_t  vel_lo;
    uint8_t  vel_hi;

    uint16_t sample_id;
    uint8_t  exclusive_class;
    uint8_t  loop_mode;
    uint8_t  flags;
    uint8_t  _reserved_a;

    int8_t   coarse_tune;
    int8_t   _pad_tune;
    int16_t  fine_tune;

    uint8_t  scale_tuning;
    uint8_t  _reserved_b;

    uint16_t attenuation_cb;
    int16_t  pan;
    uint8_t  reverb_send;
    uint8_t  chorus_send;

    VHLEnvelope vol_env;
    VHLEnvelope mod_env;

    int16_t  mod_env_to_pitch;
    int16_t  mod_env_to_filter;

    VHLModLFO lfo_mod;
    VHLVibLFO lfo_vib;

    VHLFilter filter;
    uint16_t _reserved_c;
} VHLRegion;

/* ============================================================================
 * SECTION 5: SAMPLE DIRECTORY (32 Bytes per Sample Entry)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint32_t data_offset;
    uint32_t total_frames;
    uint32_t loop_start_frame;
    uint32_t loop_end_frame;
    uint32_t sample_rate;
    uint8_t  root_key;
    int8_t   fine_tune_cents;
    uint8_t  bit_depth;
    uint8_t  channels;
    uint8_t  flags;
    uint16_t linked_sample_id;
    uint8_t  _reserved[5];
} VHLSampleEntry;

/* ============================================================================
 * SECTION 6: LOOKUP TABLE (4 Bytes Header + 4 Bytes Entry)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint16_t instrument_id;
    uint16_t _reserved;
} VHLLookupHeader;

typedef struct VHL_PACKED {
    uint16_t region_start_idx;
    uint8_t  region_count;
    uint8_t  _reserved;
} VHLLookupEntry;

/* ============================================================================
 * SECTION 7: SOUNDFONT 2 COMPATIBLE MODULATOR TABLE (12 Bytes per Modulator)
 * ============================================================================ */
typedef struct VHL_PACKED {
    uint16_t src_oper;      /* Bitfields: [15:10]=Curve, [9]=Bipolar, [8]=Descending, [7]=CC, [6:0]=Controller */
    uint16_t dest_oper;     /* Target generator enum or linked modulator index */
    uint16_t amt_src_oper;  /* Secondary controller bitfields (0 = None) */
    int16_t  amount;        /* Modulation amount in target generator units */
    uint8_t  transform;     /* 0 = linear, 2 = absolute_value */
    uint8_t  flags;         /* Bit 0 = enabled */
    uint16_t index;         /* Modulator ID / index */
} VHLModulator;

#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#   pragma pack(pop)
#endif

#endif /* VHL_STRUCTS_H */
