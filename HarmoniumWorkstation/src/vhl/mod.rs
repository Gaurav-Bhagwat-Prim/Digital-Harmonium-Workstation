pub mod types;
pub mod crc32;
pub mod compiler;
pub mod decompiler;
pub mod wav_reader;

#[cfg(test)]
mod tests {
    use super::types::*;
    use super::compiler::*;
    use super::decompiler::*;

    #[test]
    fn test_vhl_v2_compile_decompile_roundtrip() {
        // 1. Create a project with 61 keys, tuning, 2 instruments, 2 regions, 1 sample
        let mut project = VhlProject::default();
        project.build_tag = "Bhairav-Harmonium-v2\0".to_string();

        // Custom tuning: Raga Bhairav Swaras
        let mut offsets = [0i16; 12];
        offsets[1] = -200; // Komal Re: -20.0 cents
        offsets[4] = -140; // Shuddha Ga: -14.0 cents
        offsets[8] = -200; // Komal Dha: -20.0 cents
        offsets[11] = -140; // Shuddha Ni: -14.0 cents

        let mut scale_name = [0u8; VHL_NAME_LEN];
        scale_name[..13].copy_from_slice(b"Raga Bhairav\0");

        project.tuning_table = Some(VHLTuningTable_v2 {
            scale_name,
            tonic_root_key: 62, // D4
            scale_flags: TUNING_FLAG_ENABLED,
            swara_cent_offsets: offsets,
            _reserved: [0u8; 14],
        });

        // Add 2 Instruments (Piano & Harmonium)
        let mut p_name = [0u8; VHL_NAME_LEN];
        p_name[..5].copy_from_slice(b"Piano");
        project.instruments.push(VHLInstrumentEntry_v2 {
            instrument_id: 0,
            bank: 0,
            program: 0,
            max_polyphony: 4,
            name: p_name,
            region_offset: 0,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 100,
            pan_default: -20,
            reverb_send_pct: 25,
            chorus_send_pct: 0,
            flags: 0,
            _reserved: [0u8; 7],
        });

        let mut h_name = [0u8; VHL_NAME_LEN];
        h_name[..9].copy_from_slice(b"Harmonium");
        project.instruments.push(VHLInstrumentEntry_v2 {
            instrument_id: 1,
            bank: 0,
            program: 1,
            max_polyphony: 5,
            name: h_name,
            region_offset: 1,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 85,
            pan_default: 20,
            reverb_send_pct: 30,
            chorus_send_pct: 10,
            flags: 0,
            _reserved: [0u8; 7],
        });

        // Add 2 Regions
        let default_env = VHLEnvelope {
            delay_ms: 0,
            attack_ms: 15,
            hold_ms: 5,
            decay_ms: 200,
            sustain_level: 850,
            release_ms: 45,
            key_to_hold: 0,
            key_to_decay: 0,
        };

        let filter = VHLFilter {
            cutoff_hz: 5000,
            resonance_cb: 30,
        };

        let mod_lfo = VHLModLFO {
            delay_ms: 0,
            freq_mHz: 5000,
            to_pitch_cb: 0,
            to_filter_cb: 0,
            to_volume_cb: 0,
            _reserved: 0,
        };

        let vib_lfo = VHLVibLFO {
            delay_ms: 50,
            freq_mHz: 6000,
            to_pitch_cb: 15,
            _reserved: 0,
        };

        project.regions.push(VHLRegion_v2 {
            key_lo: 36,
            key_hi: 60,
            vel_lo: 0,
            vel_hi: 127,
            sample_id: 0,
            exclusive_class: 0,
            loop_mode: VHL_LOOP_NONE,
            flags: 0,
            _reserved_a: 0,
            coarse_tune: 0,
            _pad_tune: 0,
            fine_tune: 0,
            scale_tuning: 100,
            _reserved_b: 0,
            attenuation_cb: 0,
            pan: -200,
            reverb_send: 25,
            chorus_send: 0,
            vol_env: default_env,
            mod_env: default_env,
            mod_env_to_pitch: 0,
            mod_env_to_filter: 0,
            lfo_mod: mod_lfo,
            lfo_vib: vib_lfo,
            filter,
            _reserved_c: 0,
        });

        project.regions.push(VHLRegion_v2 {
            key_lo: 61,
            key_hi: 96,
            vel_lo: 0,
            vel_hi: 127,
            sample_id: 0,
            exclusive_class: 0,
            loop_mode: VHL_LOOP_CONTINUOUS,
            flags: 0,
            _reserved_a: 0,
            coarse_tune: 0,
            _pad_tune: 0,
            fine_tune: 0,
            scale_tuning: 100,
            _reserved_b: 0,
            attenuation_cb: 0,
            pan: 200,
            reverb_send: 30,
            chorus_send: 10,
            vol_env: default_env,
            mod_env: default_env,
            mod_env_to_pitch: 0,
            mod_env_to_filter: 0,
            lfo_mod: mod_lfo,
            lfo_vib: vib_lfo,
            filter,
            _reserved_c: 0,
        });

        // Add 1 sample with 100 frames of triangle waveform
        let mut frames = Vec::new();
        for i in 0..100 {
            frames.push((i * 300) as i16);
        }

        project.samples.push(VhlSample {
            name: "C4_Harmonium".to_string(),
            root_key: 60,
            sample_rate: 44100,
            fine_tune_cents: 0,
            loop_start_frame: 20,
            loop_end_frame: 80,
            channels: 1,
            pcm_frames: frames.clone(),
        });

        // 2. Compile to binary bytes
        let binary = VhlCompiler::compile(&project).expect("Compilation must succeed");

        // Verify Header properties
        assert_eq!(&binary[0..4], b"VHL2");
        assert_eq!(binary[4], 2); // Major
        assert_eq!(binary[5], 0); // Minor

        // 3. Decompile binary bytes back into a project
        let decompiled = VhlDecompiler::decompile(&binary).expect("Decompilation must succeed");

        // 4. Verify roundtrip fidelity
        assert_eq!(decompiled.build_tag, "Bhairav-Harmonium-v2");
        assert_eq!(decompiled.instruments.len(), 2);
        assert_eq!(decompiled.regions.len(), 2);
        assert_eq!(decompiled.samples.len(), 1);

        // Verify 61-Key Mapping roundtrip
        for i in 0..61 {
            assert_eq!(decompiled.key_map[i].physical_key_idx, project.key_map[i].physical_key_idx);
            assert_eq!(decompiled.key_map[i].assigned_inst_id, project.key_map[i].assigned_inst_id);
            assert_eq!(decompiled.key_map[i].midi_note, project.key_map[i].midi_note);
            assert_eq!(decompiled.key_map[i].zone_id, project.key_map[i].zone_id);
            assert!((decompiled.key_map[i].pitch_ratio - project.key_map[i].pitch_ratio).abs() < 1e-5);
        }

        // Verify Tuning Table roundtrip
        let dec_tuning = decompiled.tuning_table.expect("Tuning table must be present");
        assert_eq!(dec_tuning.tonic_root_key, 62);
        let dec_offsets = dec_tuning.swara_cent_offsets;
        assert_eq!(dec_offsets[1], -200); // Komal Re: -20.0 cents
        assert_eq!(dec_offsets[4], -140); // Shuddha Ga: -14.0 cents

        // Verify PCM Waveform roundtrip (exact match)
        assert_eq!(decompiled.samples[0].pcm_frames, frames);
    }
}
