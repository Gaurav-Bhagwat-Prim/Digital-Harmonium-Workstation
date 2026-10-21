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
    pub offset_modulator_table: u32,

    pub total_file_size: u32,
    pub crc32: u32,

    pub build_timestamp: u32,
    pub builder_version: u16,
    pub _reserved_meta: u16,

    pub build_tag: [u8; VHL_BUILD_TAG_LEN],
    pub total_modulator_count: u16,
    pub _reserved_future: [u8; 24],
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

// ============================================================================
// SECTION 7: SOUNDFONT 2 COMPATIBLE MODULATOR TABLE (12 Bytes per Modulator)
// ============================================================================

// Sources (Primary and Secondary Controllers)
pub const SF_MOD_SRC_NONE: u16 = 0;
pub const SF_MOD_SRC_VELOCITY: u16 = 2;              // Note-on Velocity (0..127)
pub const SF_MOD_SRC_KEY: u16 = 3;                   // Key Number (0..127)
pub const SF_MOD_SRC_POLY_PRESSURE: u16 = 10;        // Key Pressure / Poly Aftertouch
pub const SF_MOD_SRC_CHANNEL_PRESSURE: u16 = 13;     // Channel Pressure / Mono Aftertouch
pub const SF_MOD_SRC_PITCH_WHEEL: u16 = 14;          // Pitch Bend Wheel (-8192..+8191)
pub const SF_MOD_SRC_PITCH_WHEEL_SENS: u16 = 16;     // Pitch Bend Sensitivity
pub const SF_MOD_SRC_MOD_ENV: u16 = 20;              // Internal Modulation Envelope
pub const SF_MOD_SRC_MOD_LFO: u16 = 21;              // Internal Mod LFO
pub const SF_MOD_SRC_VIB_LFO: u16 = 22;              // Internal Vibrato LFO
pub const SF_MOD_SRC_CC_FLAG: u16 = 0x80;            // Bit 7 indicates MIDI Continuous Controller

// Standard MIDI CCs
pub const SF_MOD_CC_MOD_WHEEL: u16 = 1;
pub const SF_MOD_CC_BREATH: u16 = 2;
pub const SF_MOD_CC_VOLUME: u16 = 7;
pub const SF_MOD_CC_PAN: u16 = 10;
pub const SF_MOD_CC_EXPRESSION: u16 = 11;
pub const SF_MOD_CC_SUSTAIN: u16 = 64;
pub const SF_MOD_CC_REVERB: u16 = 91;
pub const SF_MOD_CC_CHORUS: u16 = 93;

// Destinations (SoundFont 2 Standard Generators)
pub const SF_GEN_MOD_LFO_TO_PITCH: u16 = 5;
pub const SF_GEN_VIB_LFO_TO_PITCH: u16 = 6;
pub const SF_GEN_INITIAL_FILTER_FC: u16 = 8;
pub const SF_GEN_INITIAL_FILTER_Q: u16 = 9;
pub const SF_GEN_MOD_LFO_TO_FILTER_FC: u16 = 10;
pub const SF_GEN_MOD_LFO_TO_VOLUME: u16 = 11;
pub const SF_GEN_CHORUS_EFFECTS_SEND: u16 = 15;
pub const SF_GEN_REVERB_EFFECTS_SEND: u16 = 16;
pub const SF_GEN_PAN: u16 = 17;
pub const SF_GEN_VOL_ENV_ATTACK: u16 = 34;
pub const SF_GEN_VOL_ENV_HOLD: u16 = 35;
pub const SF_GEN_VOL_ENV_DECAY: u16 = 36;
pub const SF_GEN_VOL_ENV_SUSTAIN: u16 = 37;
pub const SF_GEN_VOL_ENV_RELEASE: u16 = 38;
pub const SF_GEN_MOD_ENV_TO_PITCH: u16 = 43;
pub const SF_GEN_MOD_ENV_TO_FILTER_FC: u16 = 44;
pub const SF_GEN_INITIAL_ATTENUATION: u16 = 48;
pub const SF_GEN_COARSE_TUNE: u16 = 51;
pub const SF_GEN_FINE_TUNE: u16 = 52;
pub const SF_GEN_SCALE_TUNING: u16 = 56;

// Curves (Transfer Functions)
pub const SF_CURVE_LINEAR: u8 = 0;
pub const SF_CURVE_CONCAVE: u8 = 1;
pub const SF_CURVE_CONVEX: u8 = 2;
pub const SF_CURVE_SWITCH: u8 = 3;
pub const SF_CURVE_CUSTOM: u8 = 4;

// Direction
pub const SF_DIR_ASCENDING: u8 = 0;
pub const SF_DIR_DESCENDING: u8 = 1;

// Polarity
pub const SF_POL_UNIPOLAR: u8 = 0;
pub const SF_POL_BIPOLAR: u8 = 1;

// Transforms
pub const SF_TRANSFORM_LINEAR: u8 = 0;
pub const SF_TRANSFORM_ABSOLUTE: u8 = 2;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VHLModulator_v2 {
    pub src_oper: u16,      // Bitfields: [15:10]=Curve, [9]=Bipolar, [8]=Descending, [7]=CC, [6:0]=Controller
    pub dest_oper: u16,     // Target generator enum or linked modulator index
    pub amt_src_oper: u16,  // Secondary controller bitfields (0 = None)
    pub amount: i16,        // Modulation amount in target generator units
    pub transform: u8,      // 0 = linear, 2 = absolute_value
    pub flags: u8,          // Bit 0 = enabled
    pub index: u16,         // Modulator ID / index
}

impl VHLModulator_v2 {
    pub fn pack_source(ctrl: u8, is_cc: bool, curve: u8, descending: bool, bipolar: bool) -> u16 {
        let mut val = (ctrl as u16) & 0x7F;
        if is_cc { val |= 0x80; }
        if descending { val |= 1 << 8; }
        if bipolar { val |= 1 << 9; }
        val |= ((curve as u16) & 0x3F) << 10;
        val
    }

    pub fn unpack_source(oper: u16) -> (u8, bool, u8, bool, bool) {
        let ctrl = (oper & 0x7F) as u8;
        let is_cc = (oper & 0x80) != 0;
        let descending = (oper & (1 << 8)) != 0;
        let bipolar = (oper & (1 << 9)) != 0;
        let curve = ((oper >> 10) & 0x3F) as u8;
        (ctrl, is_cc, curve, descending, bipolar)
    }

    pub fn source_name(oper: u16) -> String {
        let (ctrl, is_cc, _, _, _) = Self::unpack_source(oper);
        if is_cc {
            match ctrl as u16 {
                SF_MOD_CC_MOD_WHEEL => "Mod Wheel (CC1)".to_string(),
                SF_MOD_CC_BREATH => "Breath (CC2)".to_string(),
                SF_MOD_CC_VOLUME => "Volume (CC7)".to_string(),
                SF_MOD_CC_PAN => "Pan (CC10)".to_string(),
                SF_MOD_CC_EXPRESSION => "Expression (CC11)".to_string(),
                SF_MOD_CC_SUSTAIN => "Sustain Pedal (CC64)".to_string(),
                SF_MOD_CC_REVERB => "Reverb Send (CC91)".to_string(),
                SF_MOD_CC_CHORUS => "Chorus Send (CC93)".to_string(),
                c => format!("CC-{}", c),
            }
        } else {
            match ctrl as u16 {
                SF_MOD_SRC_NONE => "None / Constant 1.0".to_string(),
                SF_MOD_SRC_VELOCITY => "Key Velocity".to_string(),
                SF_MOD_SRC_KEY => "Key Number (Pitch Track)".to_string(),
                SF_MOD_SRC_POLY_PRESSURE => "Key Pressure".to_string(),
                SF_MOD_SRC_CHANNEL_PRESSURE => "Channel Pressure / Aftertouch".to_string(),
                SF_MOD_SRC_PITCH_WHEEL => "Pitch Wheel".to_string(),
                SF_MOD_SRC_PITCH_WHEEL_SENS => "Pitch Bend Range".to_string(),
                SF_MOD_SRC_MOD_ENV => "Modulation Envelope".to_string(),
                SF_MOD_SRC_MOD_LFO => "Mod LFO".to_string(),
                SF_MOD_SRC_VIB_LFO => "Vibrato LFO".to_string(),
                _ => format!("Source #{}", ctrl),
            }
        }
    }

    pub fn dest_name(dest: u16) -> String {
        match dest {
            SF_GEN_INITIAL_ATTENUATION => "Initial Attenuation (Volume dB)".to_string(),
            SF_GEN_INITIAL_FILTER_FC => "Filter Cutoff (Hz / Cents)".to_string(),
            SF_GEN_INITIAL_FILTER_Q => "Filter Resonance (cB)".to_string(),
            SF_GEN_MOD_ENV_TO_FILTER_FC => "Mod Env -> Filter Cutoff".to_string(),
            SF_GEN_MOD_ENV_TO_PITCH => "Mod Env -> Pitch".to_string(),
            SF_GEN_MOD_LFO_TO_VOLUME => "Mod LFO -> Volume (Tremolo)".to_string(),
            SF_GEN_MOD_LFO_TO_PITCH => "Mod LFO -> Pitch (Vibrato)".to_string(),
            SF_GEN_MOD_LFO_TO_FILTER_FC => "Mod LFO -> Filter (Wah)".to_string(),
            SF_GEN_VIB_LFO_TO_PITCH => "Vibrato LFO -> Pitch".to_string(),
            SF_GEN_PAN => "Stereo Pan [-100..+100]".to_string(),
            SF_GEN_REVERB_EFFECTS_SEND => "Reverb Send (%)".to_string(),
            SF_GEN_CHORUS_EFFECTS_SEND => "Chorus Send (%)".to_string(),
            SF_GEN_FINE_TUNE => "Fine Tune (Cents)".to_string(),
            SF_GEN_COARSE_TUNE => "Coarse Tune (Semitones)".to_string(),
            SF_GEN_VOL_ENV_ATTACK => "Volume Attack Time".to_string(),
            SF_GEN_VOL_ENV_DECAY => "Volume Decay Time".to_string(),
            _ => format!("Destination #{}", dest),
        }
    }

    /// Default modulators built-in for zero-hardware setup (keyboard velocity, pitch track, LFOs, envelopes)
    pub fn default_modulators() -> Vec<Self> {
        vec![
            // 1. Key Velocity -> Volume Attenuation (Softer hits are quieter, harder hits are full loudness)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_VELOCITY as u8, false, SF_CURVE_CONCAVE, true, false),
                dest_oper: SF_GEN_INITIAL_ATTENUATION,
                amt_src_oper: 0,
                amount: 960, // up to 96 dB attenuation on zero velocity
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 1,
            },
            // 2. Key Velocity -> Filter Cutoff (Hit softer = warmer/sweeter tone, hit harder = full bright reed)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_VELOCITY as u8, false, SF_CURVE_LINEAR, true, false),
                dest_oper: SF_GEN_INITIAL_FILTER_FC,
                amt_src_oper: 0,
                amount: -2400, // drops cutoff up to 2 octaves on soft hits
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 2,
            },
            // 3. Key Number -> Volume Decay (Acoustic reed scaling: treble notes decay faster, bass sustains longer)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_KEY as u8, false, SF_CURVE_LINEAR, false, true),
                dest_oper: SF_GEN_VOL_ENV_DECAY,
                amt_src_oper: 0,
                amount: 100,
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 3,
            },
            // 4. Modulation Envelope -> Filter Cutoff (Harmonium bellows / attack swell over time)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_MOD_ENV as u8, false, SF_CURVE_LINEAR, false, false),
                dest_oper: SF_GEN_INITIAL_FILTER_FC,
                amt_src_oper: 0,
                amount: 2400, // opens cutoff up to 2 octaves as envelope attacks
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 4,
            },
            // 5. Modulation Envelope -> Reverb Send (Reverb evolves and blooms over time)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_MOD_ENV as u8, false, SF_CURVE_LINEAR, false, false),
                dest_oper: SF_GEN_REVERB_EFFECTS_SEND,
                amt_src_oper: 0,
                amount: 400, // up to 40% reverb bloom
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 5,
            },
            // 6. Vibrato LFO -> Pitch (Natural gentle pitch flutter / vocal meend shimmer)
            Self {
                src_oper: Self::pack_source(SF_MOD_SRC_VIB_LFO as u8, false, SF_CURVE_LINEAR, false, true),
                dest_oper: SF_GEN_VIB_LFO_TO_PITCH,
                amt_src_oper: 0,
                amount: 25, // +/- 25 cents vibrato
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 6,
            },
            // 7. Mod Wheel CC1 -> Vibrato (If user has mod wheel on MIDI keyboard, lets them dial in vibrato)
            Self {
                src_oper: Self::pack_source(SF_MOD_CC_MOD_WHEEL as u8, true, SF_CURVE_LINEAR, false, false),
                dest_oper: SF_GEN_VIB_LFO_TO_PITCH,
                amt_src_oper: 0,
                amount: 50,
                transform: SF_TRANSFORM_LINEAR,
                flags: 1,
                index: 7,
            },
        ]
    }
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
        assert_eq!(size_of::<VHLModulator_v2>(), 12, "Modulator must be exactly 12 bytes");
    }

    #[test]
    fn test_modulator_pack_unpack() {
        let oper = VHLModulator_v2::pack_source(SF_MOD_CC_EXPRESSION as u8, true, SF_CURVE_CONCAVE, true, false);
        let (ctrl, is_cc, curve, descending, bipolar) = VHLModulator_v2::unpack_source(oper);
        assert_eq!(ctrl, 11);
        assert!(is_cc);
        assert_eq!(curve, SF_CURVE_CONCAVE);
        assert!(descending);
        assert!(!bipolar);
    }
}
