//! VHR (Virtual Harmonium Recording v2.0) Binary Structure Definitions
//!
//! Mirrors `firmware/components/vhl_storage/include/vhr_format.h`.
//! All multi-byte primitives are Little-Endian.
//! Structs are `#[repr(C, packed)]` to guarantee zero padding.

// Constants
pub const VHR_MAGIC: [u8; 4] = [b'V', b'H', b'R', b'2'];
pub const VHR_VERSION_MAJOR: u8 = 2;
pub const VHR_VERSION_MINOR: u8 = 0;

pub const VHR_HEADER_SIZE: usize = 96;
pub const VHR_EVENT_SIZE: usize = 8;
pub const VHR_INSTRUMENT_NAME_LEN: usize = 16;
pub const VHR_TITLE_LEN: usize = 20;

// Format Flags (bitmask)
pub const VHR_FLAG_HAS_TEMPO_EVENTS: u16      = 1 << 0;
pub const VHR_FLAG_HAS_CONTROL_EVENTS: u16    = 1 << 1;
pub const VHR_FLAG_HAS_INSTRUMENT_EVENTS: u16 = 1 << 2;
pub const VHR_FLAG_HAS_TRANSPOSE_EVENTS: u16  = 1 << 3;
pub const VHR_FLAG_HAS_VOLUME_EVENTS: u16     = 1 << 4;
pub const VHR_FLAG_HAS_SCALE_EVENTS: u16      = 1 << 5;
pub const VHR_FLAG_HAS_PEDAL_EVENTS: u16      = 1 << 6;

// Event Types
pub const VHR_EVENT_NONE: u8              = 0;
pub const VHR_EVENT_NOTE_ON: u8           = 1;
pub const VHR_EVENT_NOTE_OFF: u8          = 2;
pub const VHR_EVENT_CONTROL_CHANGE: u8    = 3;
pub const VHR_EVENT_PROGRAM_CHANGE: u8    = 4;
pub const VHR_EVENT_TEMPO_CHANGE: u8      = 5;
pub const VHR_EVENT_TRANSPOSE_CHANGE: u8  = 6;
pub const VHR_EVENT_INSTRUMENT_CHANGE: u8 = 7;
pub const VHR_EVENT_PEDAL: u8             = 8;
pub const VHR_EVENT_VOLUME_CHANGE: u8     = 9;
pub const VHR_EVENT_SCALE_CHANGE: u8      = 10;
pub const VHR_EVENT_TUNING_CHANGE: u8     = 11;
pub const VHR_EVENT_START_RECORD: u8      = 12;
pub const VHR_EVENT_STOP_RECORD: u8       = 13;
pub const VHR_EVENT_PITCH_BEND: u8        = 15;

// ============================================================================
// VHR Master File Header (96 Bytes Fixed)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHRFileHeader {
    // Section 1: Format Identity (8 bytes)
    pub magic: [u8; 4],
    pub version_major: u8,
    pub version_minor: u8,
    pub format_flags: u16,

    // Section 2: Performance Statistics (16 bytes)
    pub total_events: u32,
    pub duration_ms: u32,
    pub start_timestamp: u32,
    pub total_file_size: u32,

    // Section 3: Native Recording Context (32 bytes)
    pub native_instrument: [u8; VHR_INSTRUMENT_NAME_LEN],
    pub native_tempo_bpm_x10: u16,
    pub native_transpose: i8,
    pub native_scale: u8,
    pub native_octave: u8,
    pub native_volume: u8,
    pub _reserved_ctx: [u8; 10],

    // Section 4: Timing & Integrity (16 bytes)
    pub crc32: u32,
    pub timebase_us: u16,
    pub ticks_per_quarter: u16,
    pub firmware_version: u32,
    pub _reserved_timing: u32,

    // Section 5: Metadata (24 bytes)
    pub title: [u8; VHR_TITLE_LEN],
    pub _reserved_meta: [u8; 4],
}

// ============================================================================
// VHR Event Entry (8 Bytes Fixed)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHREventEntry {
    pub delta_us: u32,
    pub event_type: u8,
    pub data1: u8,
    pub data2: u8,
    pub channel: u8,
}

// ============================================================================
// High-Level In-Memory Representation for GUI and Playback
// ============================================================================
#[derive(Debug, Clone)]
pub struct VhrRecording {
    pub title: String,
    pub native_instrument: String,
    pub native_tempo_bpm: f32,
    pub native_transpose: i8,
    pub native_scale: u8,
    pub native_octave: u8,
    pub native_volume: u8,
    pub timebase_us: u16,
    pub ticks_per_quarter: u16,
    pub events: Vec<VhrEvent>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VhrEvent {
    pub delta_us: u32,
    pub event_type: u8,
    pub data1: u8,
    pub data2: u8,
    pub channel: u8,
}

impl Default for VhrRecording {
    fn default() -> Self {
        Self {
            title: "Untitled Recording".to_string(),
            native_instrument: "harmonium".to_string(),
            native_tempo_bpm: 80.0,
            native_transpose: 0,
            native_scale: 0,
            native_octave: 0,
            native_volume: 100,
            timebase_us: 1000,
            ticks_per_quarter: 480,
            events: Vec::new(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::size_of;

    #[test]
    fn test_vhr_exact_struct_sizes() {
        assert_eq!(size_of::<VHRFileHeader>(), 96, "VHR Header must be exactly 96 bytes");
        assert_eq!(size_of::<VHREventEntry>(), 8, "VHR Event Entry must be exactly 8 bytes");
    }
}
