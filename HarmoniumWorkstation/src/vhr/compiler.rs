//! VHR v2.0 Binary Compiler
//!
//! Encodes a `VhrRecording` into a valid `.vhr` binary file conforming to
//! ESP32 firmware specification with IEEE 802.3 CRC-32 checksum.

use byteorder::{LittleEndian, WriteBytesExt};
use std::io::Write;
use crate::vhl::crc32::Crc32;
use crate::vhr::types::*;

pub struct VhrCompiler;

impl VhrCompiler {
    /// Compile a high-level `VhrRecording` into a binary `.vhr` file buffer
    pub fn compile(recording: &VhrRecording) -> Result<Vec<u8>, String> {
        let total_events = recording.events.len() as u32;

        // 1. Calculate duration in ms and scan event flags
        let mut total_duration_us: u64 = 0;
        let mut format_flags: u16 = 0;

        for ev in &recording.events {
            total_duration_us += ev.delta_us as u64;
            match ev.event_type {
                VHR_EVENT_TEMPO_CHANGE => format_flags |= VHR_FLAG_HAS_TEMPO_EVENTS,
                VHR_EVENT_CONTROL_CHANGE => format_flags |= VHR_FLAG_HAS_CONTROL_EVENTS,
                VHR_EVENT_INSTRUMENT_CHANGE => format_flags |= VHR_FLAG_HAS_INSTRUMENT_EVENTS,
                VHR_EVENT_TRANSPOSE_CHANGE => format_flags |= VHR_FLAG_HAS_TRANSPOSE_EVENTS,
                VHR_EVENT_VOLUME_CHANGE => format_flags |= VHR_FLAG_HAS_VOLUME_EVENTS,
                VHR_EVENT_SCALE_CHANGE => format_flags |= VHR_FLAG_HAS_SCALE_EVENTS,
                VHR_EVENT_PEDAL => format_flags |= VHR_FLAG_HAS_PEDAL_EVENTS,
                _ => {}
            }
        }
        let duration_ms = (total_duration_us / 1000) as u32;

        // 2. Serialize event stream (8 bytes per event)
        let mut event_bytes = Vec::with_capacity(recording.events.len() * VHR_EVENT_SIZE);
        for ev in &recording.events {
            event_bytes.write_u32::<LittleEndian>(ev.delta_us).map_err(|e| e.to_string())?;
            event_bytes.write_u8(ev.event_type).map_err(|e| e.to_string())?;
            event_bytes.write_u8(ev.data1).map_err(|e| e.to_string())?;
            event_bytes.write_u8(ev.data2).map_err(|e| e.to_string())?;
            event_bytes.write_u8(ev.channel).map_err(|e| e.to_string())?;
        }

        // 3. Compute IEEE 802.3 CRC-32 over event stream bytes only
        let crc32 = Crc32::compute(&event_bytes);

        // 4. Construct 96-byte Header
        let total_file_size = (VHR_HEADER_SIZE + event_bytes.len()) as u32;
        let mut header_buf = Vec::with_capacity(VHR_HEADER_SIZE);

        // Section 1: Format Identity (8 bytes)
        header_buf.write_all(&VHR_MAGIC).map_err(|e| e.to_string())?;
        header_buf.write_u8(VHR_VERSION_MAJOR).map_err(|e| e.to_string())?;
        header_buf.write_u8(VHR_VERSION_MINOR).map_err(|e| e.to_string())?;
        header_buf.write_u16::<LittleEndian>(format_flags).map_err(|e| e.to_string())?;

        // Section 2: Performance Statistics (16 bytes)
        header_buf.write_u32::<LittleEndian>(total_events).map_err(|e| e.to_string())?;
        header_buf.write_u32::<LittleEndian>(duration_ms).map_err(|e| e.to_string())?;
        let now_epoch = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs() as u32)
            .unwrap_or(0);
        header_buf.write_u32::<LittleEndian>(now_epoch).map_err(|e| e.to_string())?;
        header_buf.write_u32::<LittleEndian>(total_file_size).map_err(|e| e.to_string())?;

        // Section 3: Native Context (32 bytes)
        let mut inst_buf = [0u8; VHR_INSTRUMENT_NAME_LEN];
        let inst_bytes = recording.native_instrument.as_bytes();
        let copy_len = inst_bytes.len().min(VHR_INSTRUMENT_NAME_LEN - 1);
        inst_buf[..copy_len].copy_from_slice(&inst_bytes[..copy_len]);
        header_buf.write_all(&inst_buf).map_err(|e| e.to_string())?;

        let tempo_bpm_x10 = (recording.native_tempo_bpm * 10.0).clamp(10.0, 6000.0) as u16;
        header_buf.write_u16::<LittleEndian>(tempo_bpm_x10).map_err(|e| e.to_string())?;
        header_buf.write_i8(recording.native_transpose).map_err(|e| e.to_string())?;
        header_buf.write_u8(recording.native_scale).map_err(|e| e.to_string())?;
        header_buf.write_u8(recording.native_octave).map_err(|e| e.to_string())?;
        header_buf.write_u8(recording.native_volume).map_err(|e| e.to_string())?;
        header_buf.write_all(&[0u8; 10]).map_err(|e| e.to_string())?; // _reserved_ctx

        // Section 4: Timing & Integrity (16 bytes)
        header_buf.write_u32::<LittleEndian>(crc32).map_err(|e| e.to_string())?;
        header_buf.write_u16::<LittleEndian>(recording.timebase_us).map_err(|e| e.to_string())?;
        header_buf.write_u16::<LittleEndian>(recording.ticks_per_quarter).map_err(|e| e.to_string())?;
        header_buf.write_u32::<LittleEndian>(200).map_err(|e| e.to_string())?; // firmware_version
        header_buf.write_u32::<LittleEndian>(0).map_err(|e| e.to_string())?; // _reserved_timing

        // Section 5: Metadata (24 bytes)
        let mut title_buf = [0u8; VHR_TITLE_LEN];
        let title_bytes = recording.title.as_bytes();
        let copy_title_len = title_bytes.len().min(VHR_TITLE_LEN - 1);
        title_buf[..copy_title_len].copy_from_slice(&title_bytes[..copy_title_len]);
        header_buf.write_all(&title_buf).map_err(|e| e.to_string())?;
        header_buf.write_all(&[0u8; 4]).map_err(|e| e.to_string())?; // _reserved_meta

        assert_eq!(header_buf.len(), VHR_HEADER_SIZE);

        // 5. Combine header and event stream
        let mut output = Vec::with_capacity(header_buf.len() + event_bytes.len());
        output.extend_from_slice(&header_buf);
        output.extend_from_slice(&event_bytes);

        Ok(output)
    }
}
