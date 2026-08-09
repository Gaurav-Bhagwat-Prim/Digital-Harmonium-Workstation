/* =============================================================================
 * vhr_format.h — Virtual Harmonium Recording (VHR v1.0) Format Specification
 * Project: Digital Harmonium Workstation Firmware
 * Stage 11 — Event Recorder Engine & Performance Logging Subsystem
 * ============================================================================= */

#ifndef VHR_FORMAT_H
#define VHR_FORMAT_H

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

/* Magic bytes at byte 0 of every VHR performance recording file */
#define VHR_MAGIC_0          'V'
#define VHR_MAGIC_1          'H'
#define VHR_MAGIC_2          'R'
#define VHR_MAGIC_3          '1'

/* Format Version */
#define VHR_VERSION_MAJOR    1
#define VHR_VERSION_MINOR    0

/* Default Configurable Compile-Time Constants */
#ifndef VHL_RECORDER_RING_SIZE
#   define VHL_RECORDER_RING_SIZE (512U)
#endif

#define VHR_DEFAULT_TIMEBASE_US   (1000U) /* 1000 us = 1 ms tick resolution */
#define VHR_DEFAULT_TICKS_PER_QN  (480U)  /* 480 ticks per quarter note */

/* Strongly Typed Event Enumeration */
typedef enum {
    VHR_EVENT_NONE              = 0,
    VHR_EVENT_NOTE_ON           = 1,
    VHR_EVENT_NOTE_OFF          = 2,
    VHR_EVENT_CONTROL_CHANGE    = 3,
    VHR_EVENT_PROGRAM_CHANGE    = 4,
    VHR_EVENT_TEMPO_CHANGE      = 5,
    VHR_EVENT_TRANSPOSE_CHANGE  = 6,
    VHR_EVENT_INSTRUMENT_CHANGE = 7,
    VHR_EVENT_PEDAL             = 8,
    VHR_EVENT_START_RECORD      = 9,
    VHR_EVENT_STOP_RECORD       = 10
} vhr_event_type_t;

/* 8-Byte Renamed Performance Log Event Entry */
typedef struct VHL_PACKED {
    uint32_t delta_us;  /* Microseconds elapsed since previous recorded event */
    uint8_t  type;      /* vhr_event_type_t enum */
    uint8_t  data1;     /* MIDI Note Number (0-127) or Control ID */
    uint8_t  data2;     /* Velocity (0-127) or Control Value */
    uint8_t  channel;   /* MIDI Channel (0-15) */
} VHREventEntry;

/* Extended 64-Byte VHR File Header */
typedef struct VHL_PACKED {
    uint8_t  magic[4];          /* Must be {'V','H','R','1'} */
    uint8_t  version_major;     /* Major version (1) */
    uint8_t  version_minor;     /* Minor version (0) */
    uint16_t flags;             /* Bit 0: Has Tabla, Bit 1: Has Tanpura */
    uint32_t total_events;      /* Total count of recorded event entries */
    uint32_t duration_ms;       /* Total performance duration in ms */
    uint32_t start_timestamp;   /* Unix timestamp of recording start */
    uint8_t  instrument_id;     /* VHL Instrument ID used for recording */
    uint8_t  _reserved_a;       /* Alignment padding */
    uint16_t timebase_us;       /* Microsecond resolution per tick */
    uint16_t ticks_per_quarter; /* MIDI Quarter note tick resolution */
    char     title[30];         /* Performance title (null-terminated ASCII) */
    uint32_t crc32;             /* CRC-32 checksum of event stream */
    uint32_t total_file_size;   /* Total file byte length (header + events) */
} VHRFileHeader;

#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
#   pragma pack(pop)
#endif

#endif /* VHR_FORMAT_H */
