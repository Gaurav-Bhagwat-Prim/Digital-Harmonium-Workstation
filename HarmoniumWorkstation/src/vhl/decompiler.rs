//! VHL v2.0 Binary Decompiler
//!
//! Parses a raw .vhl binary file, verifies CRC-32 integrity,
//! and unpacks all sections, key mappings, and waveforms back into
//! an editable `VhlProject`.

use super::types::*;
use super::compiler::{VhlProject, VhlSample};
use super::crc32::Crc32;
use byteorder::{LittleEndian, ReadBytesExt};
use std::io::{Cursor, Read};

pub struct VhlDecompiler;

impl VhlDecompiler {
    /// Decompile a binary .vhl buffer into a structured `VhlProject`
    pub fn decompile(data: &[u8]) -> Result<VhlProject, String> {
        if data.len() < 128 {
            return Err("Binary file too small to contain VHL header (min 128 bytes)".to_string());
        }

        // 1. Verify Magic
        if data[0..4] != VHL_MAGIC {
            return Err(format!("Invalid VHL Magic: expected 'VHL2', found {:?}", &data[0..4]));
        }

        // 2. Read Header
        let mut cur = Cursor::new(&data[0..128]);
        let mut magic = [0u8; 4];
        cur.read_exact(&mut magic).unwrap();
        let version_major = cur.read_u8().map_err(|e| e.to_string())?;
        let _version_minor = cur.read_u8().map_err(|e| e.to_string())?;
        if version_major != VHL_VERSION_MAJOR {
            return Err(format!("Unsupported VHL Major Version: {} (expected {})", version_major, VHL_VERSION_MAJOR));
        }

        let flags = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let num_physical_keys = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let num_instruments = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let total_region_count = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let total_sample_count = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;

        let target_sample_rate = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let dma_block_size = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;

        let offset_keybed = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let offset_tuning = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let offset_instruments = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let offset_regions = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let offset_samples = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let _offset_lookup = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let offset_pcm = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let _offset_reserved = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;

        let total_file_size = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
        let stored_crc32 = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;

        let _build_timestamp = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
        let _builder_version = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
        let _reserved_meta = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;

        let mut build_tag_raw = [0u8; VHL_BUILD_TAG_LEN];
        cur.read_exact(&mut build_tag_raw).unwrap();
        let build_tag = String::from_utf8_lossy(&build_tag_raw).trim_matches('\0').to_string();

        // 3. Verify CRC-32
        let mut verify_buf = data[0..total_file_size.min(data.len())].to_vec();
        // Clear CRC-32 field at bytes 58..62
        verify_buf[58..62].copy_from_slice(&[0, 0, 0, 0]);
        let calculated_crc = Crc32::compute(&verify_buf);
        if calculated_crc != stored_crc32 {
            return Err(format!("CRC-32 Checksum mismatch: file corrupted! (stored 0x{:08X}, calculated 0x{:08X})", stored_crc32, calculated_crc));
        }

        // 4. Section 1: Physical Keybed Mapping Table
        let mut key_map = [VHLKeyMapEntry_v2 {
            physical_key_idx: 0,
            assigned_inst_id: 0,
            midi_note: 0,
            transpose_offset: 0,
            pitch_ratio: 1.0,
            fine_cents: 0,
            zone_id: 0,
            key_flags: 0,
            _reserved: [0u8; 4],
        }; VHL_TOTAL_PHYSICAL_KEYS];

        if (flags & VHL_FILE_FLAG_HAS_KEYBED_MAP) != 0 && offset_keybed > 0 && offset_keybed < data.len() {
            let mut k_cur = Cursor::new(&data[offset_keybed..]);
            for i in 0..(num_physical_keys as usize).min(VHL_TOTAL_PHYSICAL_KEYS) {
                key_map[i] = VHLKeyMapEntry_v2 {
                    physical_key_idx: k_cur.read_u8().map_err(|e| e.to_string())?,
                    assigned_inst_id: k_cur.read_u8().map_err(|e| e.to_string())?,
                    midi_note: k_cur.read_u8().map_err(|e| e.to_string())?,
                    transpose_offset: k_cur.read_i8().map_err(|e| e.to_string())?,
                    pitch_ratio: k_cur.read_f32::<LittleEndian>().map_err(|e| e.to_string())?,
                    fine_cents: k_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?,
                    zone_id: k_cur.read_u8().map_err(|e| e.to_string())?,
                    key_flags: k_cur.read_u8().map_err(|e| e.to_string())?,
                    _reserved: {
                        let mut buf = [0u8; 4];
                        k_cur.read_exact(&mut buf).map_err(|e| e.to_string())?;
                        buf
                    },
                };
            }
        }

        // 5. Section 2: Tuning Table
        let mut tuning_table = None;
        if (flags & VHL_FILE_FLAG_HAS_TUNING_TABLE) != 0 && offset_tuning > 0 && offset_tuning < data.len() {
            let mut t_cur = Cursor::new(&data[offset_tuning..]);
            let mut scale_name = [0u8; VHL_NAME_LEN];
            t_cur.read_exact(&mut scale_name).unwrap();
            let tonic_root_key = t_cur.read_u8().map_err(|e| e.to_string())?;
            let scale_flags = t_cur.read_u8().map_err(|e| e.to_string())?;
            let mut swara_cent_offsets = [0i16; 12];
            for i in 0..12 {
                swara_cent_offsets[i] = t_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;
            }
            let mut res = [0u8; 14];
            t_cur.read_exact(&mut res).unwrap();

            tuning_table = Some(VHLTuningTable_v2 {
                scale_name,
                tonic_root_key,
                scale_flags,
                swara_cent_offsets,
                _reserved: res,
            });
        }

        // 6. Section 3: Instrument Table
        let mut instruments = Vec::new();
        if offset_instruments > 0 && offset_instruments < data.len() {
            let mut inst_cur = Cursor::new(&data[offset_instruments..]);
            for _ in 0..num_instruments {
                let inst_id = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let bank = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let program = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let max_polyphony = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let mut name = [0u8; VHL_NAME_LEN];
                inst_cur.read_exact(&mut name).unwrap();
                let region_offset = inst_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                let region_count = inst_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                let lookup_table_offset = inst_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                let master_volume_pct = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let pan_default = inst_cur.read_i8().map_err(|e| e.to_string())?;
                let reverb_send_pct = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let chorus_send_pct = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let iflags = inst_cur.read_u8().map_err(|e| e.to_string())?;
                let mut ires = [0u8; 7];
                inst_cur.read_exact(&mut ires).unwrap();

                instruments.push(VHLInstrumentEntry_v2 {
                    instrument_id: inst_id,
                    bank,
                    program,
                    max_polyphony,
                    name,
                    region_offset,
                    region_count,
                    lookup_table_offset,
                    master_volume_pct,
                    pan_default,
                    reverb_send_pct,
                    chorus_send_pct,
                    flags: iflags,
                    _reserved: ires,
                });
            }
        }

        // 7. Section 4: Region Table
        let mut regions = Vec::new();
        if offset_regions > 0 && offset_regions < data.len() {
            let mut reg_cur = Cursor::new(&data[offset_regions..]);
            for _ in 0..total_region_count {
                let key_lo = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let key_hi = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let vel_lo = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let vel_hi = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let sample_id = reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                let exclusive_class = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let loop_mode = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let rflags = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let _reserved_a = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let coarse_tune = reg_cur.read_i8().map_err(|e| e.to_string())?;
                let _pad_tune = reg_cur.read_i8().map_err(|e| e.to_string())?;
                let fine_tune = reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;
                let scale_tuning = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let _reserved_b = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let attenuation_cb = reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                let pan = reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;
                let reverb_send = reg_cur.read_u8().map_err(|e| e.to_string())?;
                let chorus_send = reg_cur.read_u8().map_err(|e| e.to_string())?;

                let vol_env = Self::read_envelope(&mut reg_cur)?;
                let mod_env = Self::read_envelope(&mut reg_cur)?;

                let mod_env_to_pitch = reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;
                let mod_env_to_filter = reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;

                let lfo_mod = VHLModLFO {
                    delay_ms: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                    freq_mHz: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                    to_pitch_cb: reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?,
                    to_filter_cb: reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?,
                    to_volume_cb: reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?,
                    _reserved: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                };

                let lfo_vib = VHLVibLFO {
                    delay_ms: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                    freq_mHz: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                    to_pitch_cb: reg_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?,
                    _reserved: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                };

                let filter = VHLFilter {
                    cutoff_hz: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                    resonance_cb: reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
                };

                let _reserved_c = reg_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;

                regions.push(VHLRegion_v2 {
                    key_lo,
                    key_hi,
                    vel_lo,
                    vel_hi,
                    sample_id,
                    exclusive_class,
                    loop_mode,
                    flags: rflags,
                    _reserved_a,
                    coarse_tune,
                    _pad_tune,
                    fine_tune,
                    scale_tuning,
                    _reserved_b,
                    attenuation_cb,
                    pan,
                    reverb_send,
                    chorus_send,
                    vol_env,
                    mod_env,
                    mod_env_to_pitch,
                    mod_env_to_filter,
                    lfo_mod,
                    lfo_vib,
                    filter,
                    _reserved_c,
                });
            }
        }

        // 8. Section 5: Sample Directory & Section 7 PCM Extraction
        let mut samples = Vec::new();
        if offset_samples > 0 && offset_samples < data.len() {
            let mut smpl_cur = Cursor::new(&data[offset_samples..]);
            for i in 0..total_sample_count {
                let data_offset = smpl_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
                let total_frames = smpl_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
                let loop_start_frame = smpl_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                let loop_end_frame = smpl_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                let sample_rate = smpl_cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                let root_key = smpl_cur.read_u8().map_err(|e| e.to_string())?;
                let fine_tune_cents = smpl_cur.read_i8().map_err(|e| e.to_string())?;
                let _bit_depth = smpl_cur.read_u8().map_err(|e| e.to_string())?;
                let channels = smpl_cur.read_u8().map_err(|e| e.to_string())?;
                let _sflags = smpl_cur.read_u8().map_err(|e| e.to_string())?;
                let _linked = smpl_cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                let mut sres = [0u8; 5];
                smpl_cur.read_exact(&mut sres).unwrap();

                // Extract PCM frames
                let pcm_start = offset_pcm + data_offset;
                let pcm_byte_len = total_frames * 2;
                let mut pcm_frames = Vec::with_capacity(total_frames);

                if pcm_start + pcm_byte_len <= data.len() {
                    let mut pcm_cur = Cursor::new(&data[pcm_start..pcm_start + pcm_byte_len]);
                    for _ in 0..total_frames {
                        pcm_frames.push(pcm_cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?);
                    }
                }

                samples.push(VhlSample {
                    name: format!("Sample_{}", i),
                    root_key,
                    sample_rate,
                    fine_tune_cents,
                    loop_start_frame,
                    loop_end_frame,
                    channels,
                    pcm_frames,
                });
            }
        }

        Ok(VhlProject {
            build_tag,
            target_sample_rate,
            dma_block_size,
            key_map,
            tuning_table,
            instruments,
            regions,
            samples,
        })
    }

    fn read_envelope(r: &mut Cursor<&[u8]>) -> Result<VHLEnvelope, String> {
        Ok(VHLEnvelope {
            delay_ms: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            attack_ms: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            hold_ms: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            decay_ms: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            sustain_level: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            release_ms: r.read_u16::<LittleEndian>().map_err(|e| e.to_string())?,
            key_to_hold: r.read_i8().map_err(|e| e.to_string())?,
            key_to_decay: r.read_i8().map_err(|e| e.to_string())?,
        })
    }
}
