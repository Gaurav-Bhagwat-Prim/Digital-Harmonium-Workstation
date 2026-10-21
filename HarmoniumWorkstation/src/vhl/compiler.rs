//! VHL v2.0 Binary Compiler
//!
//! Takes a structured `VhlProject` and bakes a fully compliant,
//! memory-aligned, little-endian binary .vhl v2.0 package.

use super::types::*;
use super::crc32::Crc32;
use byteorder::{LittleEndian, WriteBytesExt};
use std::io::{Cursor, Write};

/// High-level sample container with metadata and PCM frames
#[derive(Debug, Clone)]
pub struct VhlSample {
    pub name: String,
    pub root_key: u8,
    pub sample_rate: u32,
    pub fine_tune_cents: i8,
    pub loop_start_frame: u32,
    pub loop_end_frame: u32,
    pub channels: u8, // 1 = mono, 2 = stereo
    pub pcm_frames: Vec<i16>, // interleaved if stereo
}

/// High-level VHL v2.0 Workstation Project
#[derive(Debug, Clone)]
pub struct VhlProject {
    pub build_tag: String,
    pub target_sample_rate: u32,
    pub dma_block_size: u16,

    pub key_map: [VHLKeyMapEntry_v2; VHL_TOTAL_PHYSICAL_KEYS],
    pub tuning_table: Option<VHLTuningTable_v2>,
    pub instruments: Vec<VHLInstrumentEntry_v2>,
    pub regions: Vec<VHLRegion_v2>,
    pub region_names: Vec<String>,
    pub samples: Vec<VhlSample>,
    pub modulators: Vec<VHLModulator_v2>,
}

impl Default for VhlProject {
    fn default() -> Self {
        // Default 61-key setup (Keys 0..60, MIDI 36 to 96)
        let mut key_map = [VHLKeyMapEntry_v2 {
            physical_key_idx: 0,
            assigned_inst_id: 0,
            midi_note: 36,
            transpose_offset: 0,
            pitch_ratio: 1.0,
            fine_cents: 0,
            zone_id: 0,
            key_flags: 0,
            _reserved: [0u8; 4],
        }; VHL_TOTAL_PHYSICAL_KEYS];

        for i in 0..VHL_TOTAL_PHYSICAL_KEYS {
            let note = (i as u8) + 36; // C2 is 36
            // Default split: Key 0..24 = Zone 0 (instrument 0 = Piano), Key 25..60 = Zone 1 (instrument 1 = Harmonium)
            let (zone, inst) = if i <= 24 { (0, 0) } else { (1, 1) };
            // Resample pitch ratio relative to D4 (MIDI 62)
            let semitone_diff = (note as f32) - 62.0;
            let ratio = 2.0_f32.powf(semitone_diff / 12.0);

            key_map[i] = VHLKeyMapEntry_v2 {
                physical_key_idx: i as u8,
                assigned_inst_id: inst,
                midi_note: note,
                transpose_offset: 0,
                pitch_ratio: ratio,
                fine_cents: 0,
                zone_id: zone,
                key_flags: 0,
                _reserved: [0u8; 4],
            };
        }

        Self {
            build_tag: "HarmoniumStudio V2\0".to_string(),
            target_sample_rate: 44100,
            dma_block_size: 64,
            key_map,
            tuning_table: None,
            instruments: Vec::new(),
            regions: Vec::new(),
            region_names: Vec::new(),
            samples: Vec::new(),
            modulators: VHLModulator_v2::default_modulators(),
        }
    }
}

pub struct VhlCompiler;

impl VhlCompiler {
    /// Compile a `VhlProject` into a packed binary `.vhl` file
    pub fn compile(project: &VhlProject) -> Result<Vec<u8>, String> {
        let mut buffer: Vec<u8> = Vec::new();

        // 1. Placeholder for Section 0: File Header (128 bytes)
        buffer.resize(128, 0);

        // 2. Section 1: Physical Keybed Mapping Table
        let offset_keybed = buffer.len() as u32;
        for entry in &project.key_map {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u8(entry.physical_key_idx).unwrap();
            cur.write_u8(entry.assigned_inst_id).unwrap();
            cur.write_u8(entry.midi_note).unwrap();
            cur.write_i8(entry.transpose_offset).unwrap();
            cur.write_f32::<LittleEndian>(entry.pitch_ratio).unwrap();
            cur.write_i16::<LittleEndian>(entry.fine_cents).unwrap();
            cur.write_u8(entry.zone_id).unwrap();
            cur.write_u8(entry.key_flags).unwrap();
            let k_res = entry._reserved;
            cur.write_all(&k_res).unwrap();
            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        // 3. Section 2: Tuning Table
        let offset_tuning = buffer.len() as u32;
        let has_tuning = project.tuning_table.is_some();
        if let Some(tuning) = &project.tuning_table {
            let mut cur = Cursor::new(Vec::new());
            let scale_name = tuning.scale_name;
            cur.write_all(&scale_name).unwrap();
            cur.write_u8(tuning.tonic_root_key).unwrap();
            cur.write_u8(tuning.scale_flags).unwrap();
            let offsets = tuning.swara_cent_offsets;
            for offset in offsets {
                cur.write_i16::<LittleEndian>(offset).unwrap();
            }
            let res = tuning._reserved;
            cur.write_all(&res).unwrap();
            buffer.extend_from_slice(&cur.into_inner());
            Self::pad_4byte(&mut buffer);
        }

        // 4. Section 3: Instrument Table
        let offset_instruments = buffer.len() as u32;
        for inst in &project.instruments {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u8(inst.instrument_id).unwrap();
            cur.write_u8(inst.bank).unwrap();
            cur.write_u8(inst.program).unwrap();
            cur.write_u8(inst.max_polyphony).unwrap();
            let inst_name = inst.name;
            cur.write_all(&inst_name).unwrap();
            cur.write_u16::<LittleEndian>(inst.region_offset).unwrap();
            cur.write_u16::<LittleEndian>(inst.region_count).unwrap();
            cur.write_u32::<LittleEndian>(inst.lookup_table_offset).unwrap();
            cur.write_u8(inst.master_volume_pct).unwrap();
            cur.write_i8(inst.pan_default).unwrap();
            cur.write_u8(inst.reverb_send_pct).unwrap();
            cur.write_u8(inst.chorus_send_pct).unwrap();
            cur.write_u8(inst.flags).unwrap();
            let inst_res = inst._reserved;
            cur.write_all(&inst_res).unwrap();
            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        // 5. Section 4: Region Table
        let offset_regions = buffer.len() as u32;
        for reg in &project.regions {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u8(reg.key_lo).unwrap();
            cur.write_u8(reg.key_hi).unwrap();
            cur.write_u8(reg.vel_lo).unwrap();
            cur.write_u8(reg.vel_hi).unwrap();
            cur.write_u16::<LittleEndian>(reg.sample_id).unwrap();
            cur.write_u8(reg.exclusive_class).unwrap();
            cur.write_u8(reg.loop_mode).unwrap();
            cur.write_u8(reg.flags).unwrap();
            cur.write_u8(reg._reserved_a).unwrap();
            cur.write_i8(reg.coarse_tune).unwrap();
            cur.write_i8(reg._pad_tune).unwrap();
            cur.write_i16::<LittleEndian>(reg.fine_tune).unwrap();
            cur.write_u8(reg.scale_tuning).unwrap();
            cur.write_u8(reg._reserved_b).unwrap();
            cur.write_u16::<LittleEndian>(reg.attenuation_cb).unwrap();
            cur.write_i16::<LittleEndian>(reg.pan).unwrap();
            cur.write_u8(reg.reverb_send).unwrap();
            cur.write_u8(reg.chorus_send).unwrap();

            // vol_env (14 bytes)
            Self::write_envelope(&mut cur, &reg.vol_env);
            // mod_env (14 bytes)
            Self::write_envelope(&mut cur, &reg.mod_env);

            cur.write_i16::<LittleEndian>(reg.mod_env_to_pitch).unwrap();
            cur.write_i16::<LittleEndian>(reg.mod_env_to_filter).unwrap();

            // lfo_mod (12 bytes)
            cur.write_u16::<LittleEndian>(reg.lfo_mod.delay_ms).unwrap();
            cur.write_u16::<LittleEndian>(reg.lfo_mod.freq_mHz).unwrap();
            cur.write_i16::<LittleEndian>(reg.lfo_mod.to_pitch_cb).unwrap();
            cur.write_i16::<LittleEndian>(reg.lfo_mod.to_filter_cb).unwrap();
            cur.write_i16::<LittleEndian>(reg.lfo_mod.to_volume_cb).unwrap();
            cur.write_u16::<LittleEndian>(reg.lfo_mod._reserved).unwrap();

            // lfo_vib (8 bytes)
            cur.write_u16::<LittleEndian>(reg.lfo_vib.delay_ms).unwrap();
            cur.write_u16::<LittleEndian>(reg.lfo_vib.freq_mHz).unwrap();
            cur.write_i16::<LittleEndian>(reg.lfo_vib.to_pitch_cb).unwrap();
            cur.write_u16::<LittleEndian>(reg.lfo_vib._reserved).unwrap();

            // filter (4 bytes)
            cur.write_u16::<LittleEndian>(reg.filter.cutoff_hz).unwrap();
            cur.write_u16::<LittleEndian>(reg.filter.resonance_cb).unwrap();

            cur.write_u16::<LittleEndian>(reg._reserved_c).unwrap();

            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        // 6. Section 5: Sample Directory & Section 7 PCM Pre-calculation
        let offset_samples = buffer.len() as u32;
        let mut pcm_offsets: Vec<u32> = Vec::new();
        let mut cur_pcm_offset = 0u32;

        for s in &project.samples {
            pcm_offsets.push(cur_pcm_offset);
            let frame_bytes = (s.pcm_frames.len() * 2) as u32;
            let padding = (4 - (frame_bytes % 4)) % 4;
            cur_pcm_offset += frame_bytes + padding;
        }

        for (idx, s) in project.samples.iter().enumerate() {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u32::<LittleEndian>(pcm_offsets[idx]).unwrap();
            cur.write_u32::<LittleEndian>(s.pcm_frames.len() as u32).unwrap();
            cur.write_u32::<LittleEndian>(s.loop_start_frame).unwrap();
            cur.write_u32::<LittleEndian>(s.loop_end_frame).unwrap();
            cur.write_u32::<LittleEndian>(s.sample_rate).unwrap();
            cur.write_u8(s.root_key).unwrap();
            cur.write_i8(s.fine_tune_cents).unwrap();
            cur.write_u8(16).unwrap(); // 16-bit
            cur.write_u8(s.channels).unwrap();
            let flags = if s.loop_end_frame > s.loop_start_frame { VHL_SAMPLE_FLAG_LOOP_VALID } else { 0 };
            cur.write_u8(flags).unwrap();
            cur.write_u16::<LittleEndian>(0xFFFF).unwrap(); // mono linked
            cur.write_all(&[0u8; 5]).unwrap();
            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        // 7. Section 6: O(1) Lookup Acceleration Table
        let offset_lookup = buffer.len() as u32;
        for inst in &project.instruments {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u16::<LittleEndian>(inst.instrument_id as u16).unwrap();
            cur.write_u16::<LittleEndian>(0).unwrap();

            // 128 key entries
            for key in 0..128u8 {
                let mut start_idx = VHL_LOOKUP_NONE;
                let mut count = 0u8;

                for (r_idx, reg) in project.regions.iter().enumerate() {
                    let r_idx = r_idx as u16;
                    if r_idx >= inst.region_offset && r_idx < (inst.region_offset + inst.region_count) {
                        if key >= reg.key_lo && key <= reg.key_hi {
                            if start_idx == VHL_LOOKUP_NONE {
                                start_idx = r_idx;
                            }
                            count += 1;
                        }
                    }
                }

                cur.write_u16::<LittleEndian>(start_idx).unwrap();
                cur.write_u8(count).unwrap();
                cur.write_u8(0).unwrap();
            }
            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        // 8. Section 7: PCM Audio Data Pool
        let offset_pcm = buffer.len() as u32;
        for s in &project.samples {
            for &sample_frame in &s.pcm_frames {
                buffer.write_i16::<LittleEndian>(sample_frame).map_err(|e| e.to_string())?;
            }
            Self::pad_4byte(&mut buffer);
        }

        // 9. Section 8: SoundFont 2 Compatible Modulator Table
        let offset_modulators = buffer.len() as u32;
        for m in &project.modulators {
            let mut cur = Cursor::new(Vec::new());
            cur.write_u16::<LittleEndian>(m.src_oper).unwrap();
            cur.write_u16::<LittleEndian>(m.dest_oper).unwrap();
            cur.write_u16::<LittleEndian>(m.amt_src_oper).unwrap();
            cur.write_i16::<LittleEndian>(m.amount).unwrap();
            cur.write_u8(m.transform).unwrap();
            cur.write_u8(m.flags).unwrap();
            cur.write_u16::<LittleEndian>(m.index).unwrap();
            buffer.extend_from_slice(&cur.into_inner());
        }
        Self::pad_4byte(&mut buffer);

        let total_file_size = buffer.len() as u32;

        // 10. Bake Master File Header (Section 0)
        let mut flags = VHL_FILE_FLAG_HAS_KEYBED_MAP | VHL_FILE_FLAG_HAS_LOOKUP_TABLE;
        if has_tuning {
            flags |= VHL_FILE_FLAG_HAS_TUNING_TABLE;
        }

        let mut build_tag = [0u8; VHL_BUILD_TAG_LEN];
        let tag_bytes = project.build_tag.as_bytes();
        let copy_len = tag_bytes.len().min(VHL_BUILD_TAG_LEN - 1);
        build_tag[..copy_len].copy_from_slice(&tag_bytes[..copy_len]);

        let mut header_cur = Cursor::new(&mut buffer[0..128]);
        header_cur.write_all(&VHL_MAGIC).unwrap();
        header_cur.write_u8(VHL_VERSION_MAJOR).unwrap();
        header_cur.write_u8(VHL_VERSION_MINOR).unwrap();
        header_cur.write_u16::<LittleEndian>(flags).unwrap();

        header_cur.write_u16::<LittleEndian>(VHL_TOTAL_PHYSICAL_KEYS as u16).unwrap();
        header_cur.write_u16::<LittleEndian>(project.instruments.len() as u16).unwrap();
        header_cur.write_u16::<LittleEndian>(project.regions.len() as u16).unwrap();
        header_cur.write_u16::<LittleEndian>(project.samples.len() as u16).unwrap();

        header_cur.write_u32::<LittleEndian>(project.target_sample_rate).unwrap();
        header_cur.write_u16::<LittleEndian>(project.dma_block_size).unwrap();

        header_cur.write_u32::<LittleEndian>(offset_keybed).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_tuning).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_instruments).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_regions).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_samples).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_lookup).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_pcm).unwrap();
        header_cur.write_u32::<LittleEndian>(offset_modulators).unwrap(); // offset_modulator_table

        header_cur.write_u32::<LittleEndian>(total_file_size).unwrap();
        header_cur.write_u32::<LittleEndian>(0).unwrap(); // CRC32 = 0 during computation

        header_cur.write_u32::<LittleEndian>(0).unwrap(); // build timestamp
        header_cur.write_u16::<LittleEndian>(0x0200).unwrap(); // v2.0
        header_cur.write_u16::<LittleEndian>(0).unwrap();

        header_cur.write_all(&build_tag).unwrap();
        header_cur.write_u16::<LittleEndian>(project.modulators.len() as u16).unwrap(); // total_modulator_count
        header_cur.write_all(&[0u8; 24]).unwrap(); // reserved future

        // 11. Compute CRC-32 across complete buffer
        let crc = Crc32::compute(&buffer);

        // Write CRC-32 at byte offset 58
        let mut crc_cur = Cursor::new(&mut buffer[58..62]);
        crc_cur.write_u32::<LittleEndian>(crc).unwrap();

        Ok(buffer)
    }

    fn write_envelope<W: Write>(w: &mut W, env: &VHLEnvelope) {
        w.write_u16::<LittleEndian>(env.delay_ms).unwrap();
        w.write_u16::<LittleEndian>(env.attack_ms).unwrap();
        w.write_u16::<LittleEndian>(env.hold_ms).unwrap();
        w.write_u16::<LittleEndian>(env.decay_ms).unwrap();
        w.write_u16::<LittleEndian>(env.sustain_level).unwrap();
        w.write_u16::<LittleEndian>(env.release_ms).unwrap();
        w.write_i8(env.key_to_hold).unwrap();
        w.write_i8(env.key_to_decay).unwrap();
    }

    fn pad_4byte(buffer: &mut Vec<u8>) {
        let remainder = buffer.len() % 4;
        if remainder != 0 {
            let padding = 4 - remainder;
            buffer.extend(std::iter::repeat(0).take(padding));
        }
    }
}