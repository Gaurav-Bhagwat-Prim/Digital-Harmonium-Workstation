//! VHR v2.0 Binary Decompiler
//!
//! Validates magic, versions, CRC-32 integrity, and parses binary VHR files
//! into high-level `VhrRecording` instances.

use byteorder::{LittleEndian, ReadBytesExt};
use std::io::Cursor;
use crate::vhl::crc32::Crc32;
use crate::vhr::types::*;

pub struct VhrDecompiler;

impl VhrDecompiler {
    /// Decompiles and validates a `.vhr` binary byte buffer
    pub fn decompile(data: &[u8]) -> Result<VhrRecording, String> {
        if data.len() < VHR_HEADER_SIZE {
            return Err(format!("Buffer too small: {} bytes (expected >= 96)", data.len()));
        }

        let mut cur = Cursor::new(data);

        // 1. Magic
        let mut magic = [0u8; 4];
        std::io::Read::read_exact(&mut cur, &mut magic).map_err(|e| e.to_string())?;
        if magic != VHR_MAGIC {
            return Err(format!("Invalid VHR Magic: {:?} (expected 'VHR2')", magic));
        }

        // 2. Version
        let version_major = cur.read_u8().map_err(|e| e.to_string())?;
        let _version_minor = cur.read_u8().map_err(|e| e.to_string())?;
        if version_major != VHR_VERSION_MAJOR {
            return Err(format!("Unsupported VHR major version: {} (expected {})", version_major, VHR_VERSION_MAJOR));
        }

        let _format_flags = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let total_events = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let _duration_ms = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let _start_timestamp = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let _total_file_size = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;

        let expected_size = VHR_HEADER_SIZE + total_events * VHR_EVENT_SIZE;
        if data.len() < expected_size {
            return Err(format!("Incomplete file: buffer has {} bytes, but {} events require {} bytes",
                data.len(), total_events, expected_size));
        }

        // Section 3: Native Context
        let mut inst_buf = [0u8; VHR_INSTRUMENT_NAME_LEN];
        std::io::Read::read_exact(&mut cur, &mut inst_buf).map_err(|e| e.to_string())?;
        let native_instrument = String::from_utf8_lossy(&inst_buf).trim_matches('\0').to_string();

        let native_tempo_bpm_x10 = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let native_tempo_bpm = (native_tempo_bpm_x10 as f32) / 10.0;
        let native_transpose = cur.read_i8().map_err(|e| e.to_string())?;
        let native_scale = cur.read_u8().map_err(|e| e.to_string())?;
        let native_octave = cur.read_u8().map_err(|e| e.to_string())?;
        let native_volume = cur.read_u8().map_err(|e| e.to_string())?;

        let mut _reserved_ctx = [0u8; 10];
        std::io::Read::read_exact(&mut cur, &mut _reserved_ctx).map_err(|e| e.to_string())?;

        // Section 4: Timing & Integrity
        let stored_crc = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let timebase_us = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let ticks_per_quarter = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let _firmware_ver = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let _reserved_timing = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;

        // Section 5: Metadata
        let mut title_buf = [0u8; VHR_TITLE_LEN];
        std::io::Read::read_exact(&mut cur, &mut title_buf).map_err(|e| e.to_string())?;
        let title = String::from_utf8_lossy(&title_buf).trim_matches('\0').to_string();

        let mut _reserved_meta = [0u8; 4];
        std::io::Read::read_exact(&mut cur, &mut _reserved_meta).map_err(|e| e.to_string())?;

        // 3. Verify CRC-32 over event stream bytes
        let event_stream_slice = &data[VHR_HEADER_SIZE..expected_size];
        let computed_crc = Crc32::compute(event_stream_slice);
        if computed_crc != stored_crc {
            return Err(format!("CRC-32 Mismatch! Stored: 0x{:08X}, Computed: 0x{:08X}", stored_crc, computed_crc));
        }

        // 4. Parse events
        let mut event_cur = Cursor::new(event_stream_slice);
        let mut events = Vec::with_capacity(total_events);

        for _ in 0..total_events {
            let delta_us = event_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
            let event_type = event_cur.read_u8().map_err(|e| e.to_string())?;
            let data1 = event_cur.read_u8().map_err(|e| e.to_string())?;
            let data2 = event_cur.read_u8().map_err(|e| e.to_string())?;
            let channel = event_cur.read_u8().map_err(|e| e.to_string())?;

            events.push(VhrEvent {
                delta_us,
                event_type,
                data1,
                data2,
                channel,
            });
        }

        Ok(VhrRecording {
            title,
            native_instrument,
            native_tempo_bpm,
            native_transpose,
            native_scale,
            native_octave,
            native_volume,
            timebase_us,
            ticks_per_quarter,
            events,
        })
    }

    /// Event type string name helper
    pub fn event_type_name(ev_type: u8) -> &'static str {
        match ev_type {
            VHR_EVENT_NONE => "NONE",
            VHR_EVENT_NOTE_ON => "NOTE_ON",
            VHR_EVENT_NOTE_OFF => "NOTE_OFF",
            VHR_EVENT_CONTROL_CHANGE => "CONTROL_CHANGE",
            VHR_EVENT_PROGRAM_CHANGE => "PROGRAM_CHANGE",
            VHR_EVENT_TEMPO_CHANGE => "TEMPO_CHANGE",
            VHR_EVENT_TRANSPOSE_CHANGE => "TRANSPOSE_CHANGE",
            VHR_EVENT_INSTRUMENT_CHANGE => "INSTRUMENT_CHANGE",
            VHR_EVENT_PEDAL => "PEDAL",
            VHR_EVENT_VOLUME_CHANGE => "VOLUME_CHANGE",
            VHR_EVENT_SCALE_CHANGE => "SCALE_CHANGE",
            VHR_EVENT_TUNING_CHANGE => "TUNING_CHANGE",
            VHR_EVENT_START_RECORD => "START_RECORD",
            VHR_EVENT_STOP_RECORD => "STOP_RECORD",
            VHR_EVENT_PITCH_BEND => "PITCH_BEND",
            _ => "UNKNOWN",
        }
    }
}
