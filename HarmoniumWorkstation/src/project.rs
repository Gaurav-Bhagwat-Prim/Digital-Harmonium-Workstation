//! Unified Harmonium Workstation Project Bundle (.hwproj)
//!
//! Encapsulates an entire user workstation session into a single portable file:
//! - Project metadata (name, tempo, transpose, master volume, split point)
//! - All PCM samples and loop boundaries (via VHL v2 container)
//! - Multi-instrument & region mappings
//! - Associated performance recordings (.vhr)
//! - CRC-32 integrity validation

use std::io::{Cursor, Read, Write};
use std::path::Path;
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use crate::vhl::compiler::{VhlCompiler, VhlProject};
use crate::vhl::decompiler::VhlDecompiler;
use crate::vhl::crc32::Crc32;
use crate::vhr::compiler::VhrCompiler;
use crate::vhr::decompiler::VhrDecompiler;
use crate::vhr::types::VhrRecording;

pub const HWPROJ_MAGIC: &[u8; 8] = b"HWPROJ\x02\x00";

#[derive(Debug, Clone)]
pub struct HarmoniumProject {
    pub name: String,
    pub tempo_bpm: u32,
    pub transpose_semitones: i32,
    pub master_volume: f32,
    pub split_key_index: u8,
    pub vhl: VhlProject,
    pub recordings: Vec<VhrRecording>,
}

impl HarmoniumProject {
    pub fn new(name: impl Into<String>, vhl: VhlProject) -> Self {
        Self {
            name: name.into(),
            tempo_bpm: 120,
            transpose_semitones: 0,
            master_volume: 80.0,
            split_key_index: 24,
            vhl,
            recordings: Vec::new(),
        }
    }

    /// Serialize project into a single binary .hwproj package
    pub fn serialize(&self) -> Result<Vec<u8>, String> {
        let mut buf = Vec::new();
        buf.write_all(HWPROJ_MAGIC).map_err(|e| e.to_string())?;

        // 1. Metadata Block
        let name_bytes = self.name.as_bytes();
        buf.write_u16::<LittleEndian>(name_bytes.len() as u16).map_err(|e| e.to_string())?;
        buf.write_all(name_bytes).map_err(|e| e.to_string())?;
        buf.write_u32::<LittleEndian>(self.tempo_bpm).map_err(|e| e.to_string())?;
        buf.write_i32::<LittleEndian>(self.transpose_semitones).map_err(|e| e.to_string())?;
        buf.write_f32::<LittleEndian>(self.master_volume).map_err(|e| e.to_string())?;
        buf.write_u8(self.split_key_index).map_err(|e| e.to_string())?;

        // 2. VHL Package Block (all instruments, regions, samples, PCMs)
        let vhl_bytes = VhlCompiler::compile(&self.vhl).map_err(|e| format!("VHL compile error: {}", e))?;
        buf.write_u32::<LittleEndian>(vhl_bytes.len() as u32).map_err(|e| e.to_string())?;
        buf.write_all(&vhl_bytes).map_err(|e| e.to_string())?;

        // 3. VHR Recordings Block
        buf.write_u16::<LittleEndian>(self.recordings.len() as u16).map_err(|e| e.to_string())?;
        for rec in &self.recordings {
            let vhr_bytes = VhrCompiler::compile(rec).map_err(|e| format!("VHR compile error: {}", e))?;
            buf.write_u32::<LittleEndian>(vhr_bytes.len() as u32).map_err(|e| e.to_string())?;
            buf.write_all(&vhr_bytes).map_err(|e| e.to_string())?;
        }

        // 3b. Sample names (the VHL container has no name field, so without this every sample
        //     comes back as "Sample_0", "Sample_1" ... after save/open). Optional trailing block:
        //     older readers ignore it, older files simply don't have it.
        buf.write_u16::<LittleEndian>(self.vhl.samples.len() as u16).map_err(|e| e.to_string())?;
        for smp in &self.vhl.samples {
            let nb = smp.name.as_bytes();
            let nb = &nb[..nb.len().min(255)];
            buf.write_u16::<LittleEndian>(nb.len() as u16).map_err(|e| e.to_string())?;
            buf.write_all(nb).map_err(|e| e.to_string())?;
        }

        // 3c. Region names
        buf.write_u16::<LittleEndian>(self.vhl.region_names.len() as u16).map_err(|e| e.to_string())?;
        for rname in &self.vhl.region_names {
            let nb = rname.as_bytes();
            let nb = &nb[..nb.len().min(255)];
            buf.write_u16::<LittleEndian>(nb.len() as u16).map_err(|e| e.to_string())?;
            buf.write_all(nb).map_err(|e| e.to_string())?;
        }

        // 4. Compute CRC-32 of entire payload
        let crc = Crc32::compute(&buf);
        buf.write_u32::<LittleEndian>(crc).map_err(|e| e.to_string())?;

        Ok(buf)
    }

    /// Deserialize a .hwproj binary package
    pub fn deserialize(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() < 16 {
            return Err("File too small to be a valid .hwproj".to_string());
        }

        // Verify CRC-32 (last 4 bytes)
        let payload_len = bytes.len() - 4;
        let expected_crc = LittleEndian::read_u32(&bytes[payload_len..]);
        let actual_crc = Crc32::compute(&bytes[..payload_len]);
        if actual_crc != expected_crc {
            return Err(format!("CRC-32 checksum mismatch: expected {:#010X}, got {:#010X}", expected_crc, actual_crc));
        }

        let mut r = Cursor::new(&bytes[..payload_len]);

        // Verify magic
        let mut magic = [0u8; 8];
        r.read_exact(&mut magic).map_err(|e| e.to_string())?;
        if &magic != HWPROJ_MAGIC {
            return Err("Invalid .hwproj header signature".to_string());
        }

        // 1. Metadata Block
        let name_len = r.read_u16::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let mut name_bytes = vec![0u8; name_len];
        r.read_exact(&mut name_bytes).map_err(|e| e.to_string())?;
        let name = String::from_utf8_lossy(&name_bytes).to_string();

        let tempo_bpm = r.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let transpose_semitones = r.read_i32::<LittleEndian>().map_err(|e| e.to_string())?;
        let master_volume = r.read_f32::<LittleEndian>().map_err(|e| e.to_string())?;
        let split_key_index = r.read_u8().map_err(|e| e.to_string())?;

        // 2. VHL Package Block
        let vhl_len = r.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let mut vhl_bytes = vec![0u8; vhl_len];
        r.read_exact(&mut vhl_bytes).map_err(|e| e.to_string())?;
        let mut vhl = VhlDecompiler::decompile(&vhl_bytes).map_err(|e| format!("VHL decompile error: {}", e))?;

        // 3. VHR Recordings Block
        let rec_count = r.read_u16::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let mut recordings = Vec::with_capacity(rec_count);
        for _ in 0..rec_count {
            let vhr_len = r.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
            let mut vhr_bytes = vec![0u8; vhr_len];
            r.read_exact(&mut vhr_bytes).map_err(|e| e.to_string())?;
            let rec = VhrDecompiler::decompile(&vhr_bytes).map_err(|e| format!("VHR decompile error: {}", e))?;
            recordings.push(rec);
        }

        // 3b. Optional sample-name block (absent in files written by older versions)
        if (r.position() as usize) + 2 <= payload_len {
            if let Ok(count) = r.read_u16::<LittleEndian>() {
                for i in 0..count as usize {
                    let Ok(len) = r.read_u16::<LittleEndian>() else { break };
                    let mut nb = vec![0u8; len as usize];
                    if r.read_exact(&mut nb).is_err() {
                        break;
                    }
                    if let Some(smp) = vhl.samples.get_mut(i) {
                        smp.name = String::from_utf8_lossy(&nb).to_string();
                    }
                }
            }
        }

        // 3c. Optional region-name block
        if (r.position() as usize) + 2 <= payload_len {
            if let Ok(count) = r.read_u16::<LittleEndian>() {
                let mut rnames = Vec::new();
                for _ in 0..count as usize {
                    let Ok(len) = r.read_u16::<LittleEndian>() else { break };
                    let mut nb = vec![0u8; len as usize];
                    if r.read_exact(&mut nb).is_err() {
                        break;
                    }
                    rnames.push(String::from_utf8_lossy(&nb).to_string());
                }
                vhl.region_names = rnames;
            }
        }

        Ok(Self {
            name,
            tempo_bpm,
            transpose_semitones,
            master_volume,
            split_key_index,
            vhl,
            recordings,
        })
    }

    /// Save project directly to a file path
    pub fn save_to_file(&self, path: &Path) -> Result<(), String> {
        let data = self.serialize()?;
        std::fs::write(path, data).map_err(|e| e.to_string())
    }

    /// Load project directly from a file path
    pub fn load_from_file(path: &Path) -> Result<Self, String> {
        let data = std::fs::read(path).map_err(|e| e.to_string())?;
        Self::deserialize(&data)
    }
}

use byteorder::ByteOrder;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hwproj_roundtrip() {
        let mut vhl = VhlProject::default();
        vhl.instruments.push(crate::vhl::types::VHLInstrumentEntry_v2 {
            instrument_id: 0,
            bank: 0,
            program: 0,
            max_polyphony: 8,
            name: [0u8; 24],
            region_offset: 0,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 100,
            pan_default: 0,
            reverb_send_pct: 20,
            chorus_send_pct: 10,
            flags: 0,
            _reserved: [0u8; 7],
        });
        vhl.instruments.push(crate::vhl::types::VHLInstrumentEntry_v2 {
            instrument_id: 1,
            bank: 0,
            program: 1,
            max_polyphony: 6,
            name: [0u8; 24],
            region_offset: 1,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 90,
            pan_default: 20,
            reverb_send_pct: 30,
            chorus_send_pct: 15,
            flags: 0,
            _reserved: [0u8; 7],
        });

        let mut proj = HarmoniumProject::new("RagaBhairav_Session", vhl);
        proj.tempo_bpm = 135;
        proj.transpose_semitones = -2;
        proj.master_volume = 92.5;
        proj.split_key_index = 28;

        let bytes = proj.serialize().expect("serialization failed");
        assert!(bytes.len() > 100);

        let restored = HarmoniumProject::deserialize(&bytes).expect("deserialization failed");
        assert_eq!(restored.name, "RagaBhairav_Session");
        assert_eq!(restored.tempo_bpm, 135);
        assert_eq!(restored.transpose_semitones, -2);
        assert_eq!(restored.split_key_index, 28);
        assert_eq!(restored.vhl.instruments.len(), 2);
    }

    #[test]
    fn test_hwproj_keeps_sample_names() {
        let mut vhl = VhlProject::default();
        vhl.samples.push(crate::vhl::compiler::VhlSample {
            name: "Harmonium_C3_Reed".to_string(),
            root_key: 48,
            sample_rate: 44100,
            fine_tune_cents: 0,
            loop_start_frame: 0,
            loop_end_frame: 100,
            channels: 1,
            pcm_frames: vec![0i16; 200],
        });
        let proj = HarmoniumProject::new("Names", vhl);
        let restored = HarmoniumProject::deserialize(&proj.serialize().unwrap()).unwrap();
        assert_eq!(restored.vhl.samples[0].name, "Harmonium_C3_Reed");
    }
}