//! VHL v2.0 Binary Structure Definitions
//!
//! Mirrors `Docs and context/VHL_v2_SPECIFICATION_MASTER.md`.
//! All multi-byte primitives are Little-Endian.
//! All structs are `#[repr(C, packed)]` to guarantee zero compiler padding.

// Constants
pub const VHL_MAGIC: [u8; 4] = [b'V', b'H', b'L', b'2']; // 0x324C4856
pub const VHL_VERSION_MAJOR: u8 = 2;
pub const VHL_VERSION_MINOR: u8 = 0;

pub const VHL_NAME_LEN: usize = 24;
pub const VHL_BUILD_TAG_LEN: usize = 32;
pub const VHL_MIDI_KEYS: usize = 128;
pub const VHL_TOTAL_PHYSICAL_KEYS: usize = 61;
pub const VHL_LOOKUP_NONE: u16 = 0xFFFF;

// File Header Flags
pub const VHL_FILE_FLAG_STEREO_OUTPUT: u16    = 1 << 0;
pub const VHL_FILE_FLAG_HAS_KEYBED_MAP: u16   = 1 << 1;
pub const VHL_FILE_FLAG_HAS_TUNING_TABLE: u16 = 1 << 2;
pub const VHL_FILE_FLAG_HAS_LOOKUP_TABLE: u16 = 1 << 3;
pub const VHL_FILE_FLAG_PSRAM_PRELOAD: u16    = 1 << 4;

// Keybed Flags
pub const KEY_FLAG_LATCH: u8        = 1 << 0; // Continuous drone toggle
pub const KEY_FLAG_MUTE: u8         = 1 << 1; // Disabled key
pub const KEY_FLAG_VEL_SENSITIVE: u8 = 1 << 2;

// Tuning Flags
pub const TUNING_FLAG_ENABLED: u8   = 1 << 0;
pub const TUNING_FLAG_IS_JUST: u8   = 1 << 1;

// Region Loop Modes
pub const VHL_LOOP_NONE: u8       = 0;
pub const VHL_LOOP_CONTINUOUS: u8 = 1;
pub const VHL_LOOP_SUSTAIN: u8    = 3;

// Region Flags
pub const VHL_REGION_FLAG_STEREO_LINKED: u8  = 1 << 0;
pub const VHL_REGION_FLAG_VEL_ATTEN: u8      = 1 << 1;
pub const VHL_REGION_FLAG_MOD_ENV_ACTIVE: u8 = 1 << 2;
pub const VHL_REGION_FLAG_FILTER_ACTIVE: u8  = 1 << 3;
pub const VHL_REGION_FLAG_MOD_LFO_ACTIVE: u8 = 1 << 4;
pub const VHL_REGION_FLAG_VIB_LFO_ACTIVE: u8 = 1 << 5;

// Sample Flags
pub const VHL_SAMPLE_FLAG_LOOP_VALID: u8   = 1 << 0;
pub const VHL_SAMPLE_FLAG_STEREO_RIGHT: u8 = 1 << 1;

// ============================================================================
// SECTION 0: MASTER FILE HEADER (128 Bytes Fixed)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLFileHeader_v2 {
    pub magic: [u8; 4],
    pub version_major: u8,
    pub version_minor: u8,
    pub flags: u16,

    pub num_physical_keys: u16,
    pub num_instruments: u16,
    pub total_region_count: u16,
    pub total_sample_count: u16,

    pub target_sample_rate: u32,
    pub dma_block_size: u16,

    pub offset_keybed_map: u32,
    pub offset_tuning_table: u32,
    pub offset_instrument_table: u32,
    pub offset_region_table: u32,
    pub offset_sample_directory: u32,
    pub offset_lookup_table: u32,
    pub offset_pcm_data: u32,
    pub offset_reserved: u32,

    pub total_file_size: u32,
    pub crc32: u32,

    pub build_timestamp: u32,
    pub builder_version: u16,
    pub _reserved_meta: u16,

    pub build_tag: [u8; VHL_BUILD_TAG_LEN],
    pub _reserved_future: [u8; 26],
}

// ============================================================================
// SECTION 1: PHYSICAL KEYBED MAPPING TABLE (16 Bytes per Key)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLKeyMapEntry_v2 {
    pub physical_key_idx: u8,   // 0 to 60
    pub assigned_inst_id: u8,   // Target instrument index
    pub midi_note: u8,          // MIDI pitch (e.g. 36 = C2, 60 = C4)
    pub transpose_offset: i8,   // Transpose in semitones (-24..+24)
    pub pitch_ratio: f32,       // Pre-calculated resample step: 2^((note-root)/12)
    pub fine_cents: i16,        // Fine detune in cents * 10
    pub zone_id: u8,            // 0 = Left Zone, 1 = Right Zone, 2 = Drone
    pub key_flags: u8,          // KEY_FLAG_*
    pub _reserved: [u8; 4],     // Enforce 16-byte size
}

// ============================================================================
// SECTION 2: MUSICAL TUNING & SHRUTI TABLE (64 Bytes Fixed)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLTuningTable_v2 {
    pub scale_name: [u8; VHL_NAME_LEN], // Null-terminated ASCII
    pub tonic_root_key: u8,             // Base tonic Sa MIDI note (e.g. 62 = D4)
    pub scale_flags: u8,                // TUNING_FLAG_*
    pub swara_cent_offsets: [i16; 12],  // Microtonal offsets in cents * 10
    pub _reserved: [u8; 14],            // Enforce 64-byte size
}

// ============================================================================
// SECTION 3: MULTI-TIMBRAL INSTRUMENT TABLE (48 Bytes per Instrument)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLInstrumentEntry_v2 {
    pub instrument_id: u8,
    pub bank: u8,
    pub program: u8,
    pub max_polyphony: u8,
    pub name: [u8; VHL_NAME_LEN],
    pub region_offset: u16,
    pub region_count: u16,
    pub lookup_table_offset: u32,
    pub master_volume_pct: u8,
    pub pan_default: i8,
    pub reverb_send_pct: u8,
    pub chorus_send_pct: u8,
    pub flags: u8,
    pub _reserved: [u8; 7],
}

// ============================================================================
// SECTION 4: PRE-BAKED REGION TABLE (80 Bytes per Region)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLEnvelope {
    pub delay_ms: u16,
    pub attack_ms: u16,
    pub hold_ms: u16,
    pub decay_ms: u16,
    pub sustain_level: u16,
    pub release_ms: u16,
    pub key_to_hold: i8,
    pub key_to_decay: i8,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLFilter {
    pub cutoff_hz: u16,
    pub resonance_cb: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
#[allow(non_snake_case)]
pub struct VHLModLFO {
    pub delay_ms: u16,
    pub freq_mHz: u16,
    pub to_pitch_cb: i16,
    pub to_filter_cb: i16,
    pub to_volume_cb: i16,
    pub _reserved: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
#[allow(non_snake_case)]
pub struct VHLVibLFO {
    pub delay_ms: u16,
    pub freq_mHz: u16,
    pub to_pitch_cb: i16,
    pub _reserved: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLRegion_v2 {
    pub key_lo: u8,
    pub key_hi: u8,
    pub vel_lo: u8,
    pub vel_hi: u8,

    pub sample_id: u16,

    pub exclusive_class: u8,
    pub loop_mode: u8,
    pub flags: u8,
    pub _reserved_a: u8,

    pub coarse_tune: i8,
    pub _pad_tune: i8,
    pub fine_tune: i16,

    pub scale_tuning: u8,
    pub _reserved_b: u8,

    pub attenuation_cb: u16,
    pub pan: i16,
    pub reverb_send: u8,
    pub chorus_send: u8,

    pub vol_env: VHLEnvelope,
    pub mod_env: VHLEnvelope,

    pub mod_env_to_pitch: i16,
    pub mod_env_to_filter: i16,

    pub lfo_mod: VHLModLFO,
    pub lfo_vib: VHLVibLFO,

    pub filter: VHLFilter,
    pub _reserved_c: u16,
}

// ============================================================================
// SECTION 5: SAMPLE DIRECTORY (32 Bytes per Sample Entry)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLSampleEntry_v2 {
    pub data_offset: u32,
    pub total_frames: u32,

    pub loop_start_frame: u32,
    pub loop_end_frame: u32,

    pub sample_rate: u32,
    pub root_key: u8,
    pub fine_tune_cents: i8,

    pub bit_depth: u8,
    pub channels: u8,
    pub flags: u8,

    pub linked_sample_id: u16,
    pub _reserved: [u8; 5],
}

// ============================================================================
// SECTION 6: O(1) LOOKUP ACCELERATION TABLE (4 Bytes Header + 4 Bytes Entry)
// ============================================================================
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLLookupHeader {
    pub instrument_id: u16,
    pub _reserved: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLLookupEntry {
    pub region_start_idx: u16,
    pub region_count: u8,
    pub _reserved: u8,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::size_of;

    #[test]
    fn test_exact_struct_sizes() {
        assert_eq!(size_of::<VHLFileHeader_v2>(), 128, "File Header must be exactly 128 bytes");
        assert_eq!(size_of::<VHLKeyMapEntry_v2>(), 16, "KeyMap Entry must be exactly 16 bytes");
        assert_eq!(size_of::<VHLTuningTable_v2>(), 64, "Tuning Table must be exactly 64 bytes");
        assert_eq!(size_of::<VHLInstrumentEntry_v2>(), 48, "Instrument Entry must be exactly 48 bytes");
        assert_eq!(size_of::<VHLEnvelope>(), 14, "Envelope must be exactly 14 bytes");
        assert_eq!(size_of::<VHLFilter>(), 4, "Filter must be exactly 4 bytes");
        assert_eq!(size_of::<VHLModLFO>(), 12, "Mod LFO must be exactly 12 bytes");
        assert_eq!(size_of::<VHLVibLFO>(), 8, "Vib LFO must be exactly 8 bytes");
        assert_eq!(size_of::<VHLRegion_v2>(), 80, "Region must be exactly 80 bytes");
        assert_eq!(size_of::<VHLSampleEntry_v2>(), 32, "Sample Entry must be exactly 32 bytes");
        assert_eq!(size_of::<VHLLookupHeader>(), 4, "Lookup Header must be exactly 4 bytes");
        assert_eq!(size_of::<VHLLookupEntry>(), 4, "Lookup Entry must be exactly 4 bytes");
    }
}
