/* =============================================================================
 * vhr_format.h — Virtual Harmonium Recording (VHR v2.0) Format Specification
 * Project: Digital Harmonium Workstation Firmware
 *
 * VHR is the native performance recording format for the Digital Harmonium
 * Workstation. It is NOT an audio format. It contains NO PCM samples.
 *
 * Philosophy:
 *   VHL = instrument/audio definition ("how does it sound?")
 *   VHR = musical performance and its metadata ("what did the musician do?")
 *
 * A VHR recorded with Harmonium is playable with Organ, Flute, or any other
 * available VHL instrument. The native instrument is metadata and a preferred
 * rendering context — NOT a hard dependency.
 *
 * Key Properties:
 *   - Compact: 8 bytes per event. 1000-note performance ≈ 16 KB.
 *   - Delta-microsecond timing: preserves exact performance timing.
 *   - CRC-32 over event stream (IEEE 802.3): detects corruption.
 *   - Forward compatible: unknown event types are safely skipped.
 *   - Instrument-independent: native_instrument is a name string, not an ID.
 *
 * Format Version History:
 *   v1.0 (magic 'VHR1'): Stage 11/12 prototype. 64-byte header. uint8_t instrument_id.
 *   v2.0 (magic 'VHR2'): Production format. 96-byte header. Instrument by name string.
 *                         Full event vocabulary. Override architecture.
 * ============================================================================= */

#ifndef VHR_FORMAT_H
#define VHR_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#   define VHR_PACKED __attribute__((packed))
#elif defined(_MSC_VER)
#   define VHR_PACKED
#   pragma pack(push, 1)
#else
#   define VHR_PACKED
#   warning "Unknown compiler: structure packing may not work correctly"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Format Identity Constants
 * ============================================================================= */

/* Magic bytes at byte 0 of every VHR v2.0 performance recording file.
 * v1.0 used 'V','H','R','1'. The major version bump changes the magic so that
 * v1.0 files are cleanly rejected by the v2.0 player (VHL_ERR_VHL_INVALID_MAGIC). */
#define VHR_MAGIC_0          'V'
#define VHR_MAGIC_1          'H'
#define VHR_MAGIC_2          'R'
#define VHR_MAGIC_3          '2'

#define VHR_VERSION_MAJOR    2
#define VHR_VERSION_MINOR    0

/* =============================================================================
 * Compile-Time Configuration
 * ============================================================================= */

#ifndef VHR_RECORDER_RING_SIZE
#   define VHR_RECORDER_RING_SIZE (512U)  /* SPSC ring buffer event capacity */
#endif

#define VHR_DEFAULT_TIMEBASE_US    (1000U) /* 1 ms tick resolution */
#define VHR_DEFAULT_TICKS_PER_QN   (480U)  /* MIDI quarter note ticks */
#define VHR_INSTRUMENT_NAME_LEN    (16U)   /* Max instrument name length incl. null */

/* =============================================================================
 * Format Flags (VHRFileHeader.format_flags bitmask)
 * ============================================================================= */

#define VHR_FLAG_HAS_TEMPO_EVENTS       (1U << 0)  /* Stream contains VHR_EVENT_TEMPO_CHANGE events */
#define VHR_FLAG_HAS_CONTROL_EVENTS     (1U << 1)  /* Stream contains VHR_EVENT_CONTROL_CHANGE events */
#define VHR_FLAG_HAS_INSTRUMENT_EVENTS  (1U << 2)  /* Stream contains VHR_EVENT_INSTRUMENT_CHANGE events */
#define VHR_FLAG_HAS_TRANSPOSE_EVENTS   (1U << 3)  /* Stream contains VHR_EVENT_TRANSPOSE_CHANGE events */
#define VHR_FLAG_HAS_VOLUME_EVENTS      (1U << 4)  /* Stream contains VHR_EVENT_VOLUME_CHANGE events */
#define VHR_FLAG_HAS_SCALE_EVENTS       (1U << 5)  /* Stream contains VHR_EVENT_SCALE_CHANGE events */
#define VHR_FLAG_HAS_PEDAL_EVENTS       (1U << 6)  /* Stream contains VHR_EVENT_PEDAL events */
/* Bits 7–15: Reserved. Must be 0 in v2.0. */

/* =============================================================================
 * Event Type Enumeration v2.0
 *
 * Design rules:
 *   1. Values 0-13 are the v2.0 defined set.
 *   2. Values 14-254 are reserved for future firmware versions.
 *   3. Players MUST silently skip any event type they do not recognize.
 *      (eventsSkippedCount++; do not error; do not crash.)
 *   4. Storing an event type does NOT require implementing its playback engine.
 *      The recorder may capture TEMPO_CHANGE events today even if the playback
 *      engine does not yet implement tempo-following. The player will skip them
 *      gracefully until the engine is ready.
 * ============================================================================= */

typedef enum {

    /* ---- Standard Performance Events ---- */

    VHR_EVENT_NONE              = 0,
    /* Padding/reserved. Always skip silently. */

    VHR_EVENT_NOTE_ON           = 1,
    /* data1 = MIDI note number (0-127)
     * data2 = velocity (1-127; 0 is technically NOTE_OFF in MIDI but use NOTE_OFF here)
     * channel = logical channel (currently always 0) */

    VHR_EVENT_NOTE_OFF          = 2,
    /* data1 = MIDI note number (0-127)
     * data2 = release velocity (0 = no release velocity; typically 0)
     * channel = logical channel */

    VHR_EVENT_CONTROL_CHANGE    = 3,
    /* data1 = control ID (0-127, MIDI CC compatible where applicable)
     * data2 = control value (0-127)
     * channel = logical channel */

    VHR_EVENT_PROGRAM_CHANGE    = 4,
    /* data1 = program number (0-127)
     * data2 = 0
     * channel = logical channel */

    /* ---- Musical Parameter Events ---- */

    VHR_EVENT_TEMPO_CHANGE      = 5,
    /* Encodes tempo as BPM × 10 split into two bytes (supports 1.0 – 6553.5 BPM):
     * data1 = high byte of (bpm_x10 >> 8)
     * data2 = low byte  of (bpm_x10 & 0xFF)
     * Example: 80.0 BPM → bpm_x10 = 800 → data1 = 3, data2 = 32
     * Decode: bpm_x10 = ((uint16_t)data1 << 8) | data2 */

    VHR_EVENT_TRANSPOSE_CHANGE  = 6,
    /* data1 = semitone offset cast as uint8_t from int8_t range -12..+12
     *         (i.e. data1 = (uint8_t)(int8_t)semitones)
     * data2 = 0 */

    VHR_EVENT_INSTRUMENT_CHANGE = 7,
    /* Inline instrument change within the event stream.
     * In v2.0 this event is informational metadata — it records that the
     * performer switched instruments mid-performance.
     * data1 = instrument slot index (0-based, matches /instruments/ directory order)
     * data2 = 0
     * NOTE: Full instrument name resolution still uses header.native_instrument
     *       plus the override architecture. This event does not bypass overrides. */

    VHR_EVENT_PEDAL             = 8,
    /* data1 = pedal ID (0 = sustain pedal, 1 = soft pedal, 2 = sostenuto)
     * data2 = value (0 = off, 127 = on; intermediate values valid for half-pedal) */

    VHR_EVENT_VOLUME_CHANGE     = 9,
    /* data1 = master volume level (0-127)
     * data2 = 0 */

    VHR_EVENT_SCALE_CHANGE      = 10,
    /* data1 = scale/raga index (0 = chromatic/no scale; 1-127 = system-defined scales)
     * data2 = root note (0-11, where 0=C, 1=C#, etc.) */

    VHR_EVENT_TUNING_CHANGE     = 11,
    /* data1 = coarse tuning offset in semitones, cast uint8_t from int8_t (-12..+12)
     * data2 = fine tuning in cents, cast uint8_t from int8_t (-100..+100) */

    /* ---- Lifecycle Marker Events (Informational — NOT re-injected during playback) ---- */

    VHR_EVENT_START_RECORD      = 12,
    /* Marks the logical recording start in the event stream.
     * delta_us = 0 (always the first event in a well-formed stream)
     * data1 = 0, data2 = 0 */

    VHR_EVENT_STOP_RECORD       = 13,
    /* Marks the logical recording stop in the event stream.
     * delta_us = time since last event
     * data1 = 0, data2 = 0 */

    /* ---- Future Extension Zone ---- */
    /* Values 14-254 are reserved for future firmware versions.
     * v2.0 players MUST skip these silently. */

    VHR_EVENT_UNKNOWN           = 255  /* Sentinel. MUST NOT appear in a valid file. */

} vhr_event_type_t;

/* =============================================================================
 * VHR Event Record v2.0 — 8 bytes (UNCHANGED from v1.0)
 *
 * Timing: delta_us = microseconds elapsed since the PREVIOUS event.
 *   - First event: delta_us = time from recording start to first event.
 *   - Simultaneous events: delta_us = 0 for all events after the first at the same timestamp.
 *   - Maximum representable delta: ~71.6 minutes (2^32 µs). Performances longer than this
 *     should use multiple VHR segments (not required in v2.0).
 * ============================================================================= */

typedef struct VHR_PACKED {
    uint32_t delta_us;   /* Microseconds since previous event (0 = simultaneous) */
    uint8_t  type;       /* vhr_event_type_t value — unknown values must be skipped */
    uint8_t  data1;      /* Meaning depends on event type (see vhr_event_type_t comments) */
    uint8_t  data2;      /* Meaning depends on event type */
    uint8_t  channel;    /* Logical channel 0-15 (currently always 0; reserved for multi-channel) */
} VHREventEntry; /* sizeof = 8 bytes */

/* =============================================================================
 * VHR File Header v2.0 — 96 bytes
 *
 * Layout:
 *   [0  .. 7 ]  Section 1: Format Identity     (8 bytes)
 *   [8  .. 23]  Section 2: Performance Stats   (16 bytes)
 *   [24 .. 55]  Section 3: Native Context       (32 bytes)
 *   [56 .. 71]  Section 4: Timing & Integrity   (16 bytes)
 *   [72 .. 95]  Section 5: Metadata             (24 bytes)
 *
 * Instrument Resolution Priority (strictly enforced by the player):
 *   1. User playback override (vhr_playback_overrides_t.override_instrument, if has_instrument=true)
 *   2. VHR native instrument (header.native_instrument, if the .vhl file exists on SD)
 *   3. Currently selected system VHL (if native is unavailable)
 *   4. Error (VHL_ERR_NOT_FOUND) if no valid instrument can be resolved
 *
 * Playback Parameter Priority (same cascading priority):
 *   User override → Recorded event/header value → System default
 * ============================================================================= */

typedef struct VHR_PACKED {

    /* ---- Section 1: Format Identity (bytes 0–7) ---- */
    uint8_t  magic[4];              /* Must be {'V','H','R','2'} */
    uint8_t  version_major;         /* 2 for v2.0 */
    uint8_t  version_minor;         /* 0 for v2.0 */
    uint16_t format_flags;          /* VHR_FLAG_* bitmask (see defines above) */

    /* ---- Section 2: Performance Statistics (bytes 8–23) ---- */
    uint32_t total_events;          /* Count of VHREventEntry records in the stream */
    uint32_t duration_ms;           /* Total recorded performance duration in milliseconds */
    uint32_t start_timestamp;       /* Unix timestamp at recording start (0 = unknown/unavailable) */
    uint32_t total_file_size;       /* Total byte length: sizeof(VHRFileHeader) + total_events × 8 */

    /* ---- Section 3: Native Recording Context (bytes 24–55) ---- */
    /* Describes the environment in which this performance was originally recorded.
     * These are METADATA fields — preferred rendering context, not hard dependencies.
     * The player ALWAYS checks user overrides before consulting these values. */
    char     native_instrument[VHR_INSTRUMENT_NAME_LEN]; /* Null-terminated instrument name.
                                       e.g. "harmonium", "organ", "tanpura".
                                       Must match a .vhl filename on the SD card at
                                       /instruments/<native_instrument>.vhl
                                       If the file is not found, the player falls back
                                       to the currently selected system VHL. */
    uint16_t native_tempo_bpm_x10;  /* Original BPM × 10 (e.g. 800 = 80.0 BPM, 1200 = 120.0 BPM).
                                       0 = unknown/not recorded. */
    int8_t   native_transpose;      /* Original semitone transpose offset at record time (-12..+12).
                                       0 = no transpose. */
    uint8_t  native_scale;          /* Original scale/raga index (0 = chromatic). */
    uint8_t  native_octave;         /* Original octave shift (0 = default). */
    uint8_t  native_volume;         /* Original master volume 0-127 (0 = unknown). */
    uint8_t  _reserved_ctx[10];     /* Reserved. Must be 0 in v2.0. */

    /* ---- Section 4: Timing & Integrity (bytes 56–71) ---- */
    uint32_t crc32;                 /* CRC-32 (IEEE 802.3, poly 0xEDB88320) computed over
                                       the event stream bytes ONLY (not the header).
                                       Recorder accumulates incrementally during flush_block().
                                       Player recomputes over same bytes on load to verify. */
    uint16_t timebase_us;           /* Microseconds per delta_us tick. Default: 1000 (= 1 ms). */
    uint16_t ticks_per_quarter;     /* Ticks per MIDI quarter note. Default: 480. */
    uint32_t firmware_version;      /* Firmware version that created this recording (diagnostic). */
    uint32_t _reserved_timing;      /* Reserved. Must be 0 in v2.0. */

    /* ---- Section 5: Metadata (bytes 72–95) ---- */
    char     title[20];             /* Performance title, null-terminated ASCII. */
    uint8_t  _reserved_meta[4];     /* Reserved. Must be 0 in v2.0. */

} VHRFileHeader; /* sizeof = 96 bytes */

/* =============================================================================
 * Playback Override Struct
 *
 * Caller-supplied overrides applied by the VHR player at runtime.
 * User overrides always have the HIGHEST priority:
 *
 *   User override (has_*=true)
 *       ↓
 *   Recorded VHR event / header value
 *       ↓
 *   System default
 *
 * Pass a zero-initialized struct (all has_* = false) for "no overrides" —
 * in that case the player uses native VHR values and system defaults.
 * ============================================================================= */

typedef struct {
    /* Instrument override */
    bool    has_instrument;
    char    override_instrument[VHR_INSTRUMENT_NAME_LEN]; /* e.g. "organ" */

    /* Tempo override */
    bool     has_tempo;
    uint16_t override_tempo_bpm_x10; /* BPM × 10 (800 = 80.0 BPM) */

    /* Transpose override */
    bool    has_transpose;
    int8_t  override_transpose; /* Semitone offset -12..+12 */

    /* Playback speed override (scales wall-clock time, not musical tempo) */
    bool    has_speed;
    float   override_speed;  /* [0.25, 4.0] — clamped by player */

    /* Volume override */
    bool    has_volume;
    uint8_t override_volume; /* Master volume 0-127 */

    uint8_t _reserved[3];
} vhr_playback_overrides_t;

/* Helper: initialize overrides struct to "no overrides" (all has_* = false) */
static inline void vhr_overrides_init(vhr_playback_overrides_t* pOv) {
    if (pOv) {
        /* Use memset equivalent via byte zeroing for C89 portability */
        uint8_t* p = (uint8_t*)pOv;
        uint8_t* end = p + sizeof(vhr_playback_overrides_t);
        while (p < end) *p++ = 0;
    }
}

#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#   pragma pack(pop)
#endif

#endif /* VHR_FORMAT_H */
