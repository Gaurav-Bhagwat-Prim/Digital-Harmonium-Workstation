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

/* Region flags (VHLRegion.flags) */
#define VHL_REGION_FLAG_STEREO_LINKED   (1 << 0)  /* Region has a stereo partner */
#define VHL_REGION_FLAG_VEL_ATTEN       (1 << 1)  /* Apply velocity->attenuation at Note-On */
#define VHL_REGION_FLAG_MOD_ENV_ACTIVE  (1 << 2)  /* Mod envelope has non-trivial timing */
#define VHL_REGION_FLAG_FILTER_ACTIVE   (1 << 3)  /* Filter is below Nyquist, must apply */
#define VHL_REGION_FLAG_MOD_LFO_ACTIVE  (1 << 4)  /* Mod LFO has non-zero depth */
#define VHL_REGION_FLAG_VIB_LFO_ACTIVE  (1 << 5)  /* Vib LFO has non-zero depth */

/* Section 0: File Header (128 bytes) */
typedef struct VHL_PACKED {
    uint8_t  magic[4];          /* Must be { 'V','H','L','1' } */
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t flags;
    uint16_t num_instruments;
    uint16_t total_region_count;
    uint16_t total_sample_count;
    uint32_t target_sample_rate;
    uint16_t dma_block_size;

    uint32_t offset_instrument_table;
    uint32_t offset_region_table;
    uint32_t offset_sample_directory;
    uint32_t offset_lookup_table;
    uint32_t offset_pcm_data;
    uint32_t offset_reserved_1;
    uint32_t offset_reserved_2;
    uint32_t offset_reserved_3;
    uint32_t offset_reserved_4;
    uint32_t offset_reserved_5;

    uint32_t total_file_size;
    uint32_t crc32;
    uint32_t build_timestamp;
    uint16_t builder_version;
    uint16_t _reserved_header;
    char     build_tag[32];
    uint8_t  _reserved[20];
} VHLFileHeader;

/* Section 1: Instrument Table */
typedef struct VHL_PACKED {
    uint8_t  instrument_id;
    uint8_t  bank;
    uint8_t  program;
    uint8_t  flags;
    char     name[VHL_NAME_LEN];
    uint16_t region_offset;
    uint16_t region_count;
    uint32_t lookup_table_offset;
    uint32_t _reserved;
} VHLInstrumentEntry;

/* Envelope Parameters */
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

/* Filter Parameters */
typedef struct VHL_PACKED {
    uint16_t cutoff_hz;
    uint16_t resonance_cb;
} VHLFilter;

/* Section 2: Region Table (80 bytes) */
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

/* Section 3: Sample Directory (32 bytes) */
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

/* Section 4: Lookup Table */
typedef struct VHL_PACKED {
    uint16_t instrument_id;
    uint16_t _reserved;
} VHLLookupHeader;

typedef struct VHL_PACKED {
    uint16_t region_start_idx;
    uint8_t  region_count;
    uint8_t  _reserved;
} VHLLookupEntry;

#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#   pragma pack(pop)
#endif

#endif /* VHL_STRUCTS_H */
