//! WAV Audio File Reader & Polyphone-style Note/Pitch Detector
//!
//! Parses RIFF/WAVE files (PCM 16-bit, 24-bit, 32-bit float, mono/stereo),
//! extracts smpl chunk loop metadata, infers root MIDI keys from filenames,
//! and converts audio into 16-bit PCM for VHL v2.0 integration.

use byteorder::{LittleEndian, ReadBytesExt};
use std::io::{Cursor, Read, Seek, SeekFrom};
use std::path::Path;
use super::compiler::VhlSample;

#[derive(Debug, Clone)]
pub struct WavInfo {
    pub channels: u16,
    pub sample_rate: u32,
    pub bits_per_sample: u16,
    pub audio_format: u16, // 1 = PCM, 3 = IEEE Float
    pub num_frames: usize,
    pub root_key: u8,
    pub fine_tune_cents: i8,
    pub loop_start: u32,
    pub loop_end: u32,
    pub pcm_16: Vec<i16>, // Interleaved if stereo
}

pub struct WavReader;

impl WavReader {
    /// Read and parse a WAV file from disk
    pub fn read_from_file<P: AsRef<Path>>(path: P) -> Result<WavInfo, String> {
        let bytes = std::fs::read(path.as_ref()).map_err(|e| format!("Failed to read file: {}", e))?;
        let filename = path.as_ref().file_stem().and_then(|s| s.to_str()).unwrap_or("sample");
        Self::parse_wav(&bytes, filename)
    }

    /// Parse a WAV buffer in memory
    pub fn parse_wav(data: &[u8], name_hint: &str) -> Result<WavInfo, String> {
        if data.len() < 44 {
            return Err("WAV data too small (less than 44-byte minimum RIFF header)".to_string());
        }

        let mut cur = Cursor::new(data);

        // 1. RIFF Header
        let mut riff_id = [0u8; 4];
        cur.read_exact(&mut riff_id).map_err(|e| e.to_string())?;
        if &riff_id != b"RIFF" {
            return Err(format!("Invalid RIFF header: {:?}", riff_id));
        }

        let _file_size = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;

        let mut wave_id = [0u8; 4];
        cur.read_exact(&mut wave_id).map_err(|e| e.to_string())?;
        if &wave_id != b"WAVE" {
            return Err(format!("Invalid WAVE format identifier: {:?}", wave_id));
        }

        // Chunk iteration
        let mut channels = 1u16;
        let mut sample_rate = 44100u32;
        let mut bits_per_sample = 16u16;
        let mut audio_format = 1u16;

        let mut raw_pcm_bytes: Option<&[u8]> = None;
        let mut loop_start = 0u32;
        let mut loop_end = 0u32;
        let mut smpl_root_key: Option<u8> = None;
        let mut smpl_fine_cents: Option<i8> = None;

        while (cur.position() as usize) + 8 <= data.len() {
            let mut chunk_id = [0u8; 4];
            cur.read_exact(&mut chunk_id).map_err(|e| e.to_string())?;
            let chunk_size = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())? as usize;
            let chunk_start = cur.position() as usize;

            match &chunk_id {
                b"fmt " => {
                    audio_format = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                    channels = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                    sample_rate = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                    let _byte_rate = cur.read_u32::<LittleEndian>().map_err(|e| e.to_string())?;
                    let _block_align = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                    bits_per_sample = cur.read_u16::<LittleEndian>().map_err(|e| e.to_string())?;
                }
                b"data" => {
                    let end_pos = (chunk_start + chunk_size).min(data.len());
                    raw_pcm_bytes = Some(&data[chunk_start..end_pos]);
                }
                b"smpl" => {
                    // MIDI Sampler chunk (used by Polyphone, Soundfonts, and modern DAWs)
                    if chunk_size >= 36 {
                        let _mfr = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let _prod = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let _period = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let unity_note = cur.read_u32::<LittleEndian>().unwrap_or(60);
                        let pitch_fraction = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let _smpte_fmt = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let _smpte_off = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let num_sample_loops = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        let _sampler_data = cur.read_u32::<LittleEndian>().unwrap_or(0);

                        if unity_note <= 127 {
                            smpl_root_key = Some(unity_note as u8);
                        }
                        if pitch_fraction > 0 {
                            smpl_fine_cents = Some((pitch_fraction as f64 / 42949672.96) as i8);
                        }

                        // Loop records
                        if num_sample_loops > 0 && chunk_size >= 36 + 24 {
                            let _cue_point_id = cur.read_u32::<LittleEndian>().unwrap_or(0);
                            let _loop_type = cur.read_u32::<LittleEndian>().unwrap_or(0);
                            loop_start = cur.read_u32::<LittleEndian>().unwrap_or(0);
                            loop_end = cur.read_u32::<LittleEndian>().unwrap_or(0);
                        }
                    }
                }
                _ => {
                    // Other chunks (LIST, INFO, etc.) - safely skip
                }
            }

            // Move cursor to next chunk (aligned to 2-byte boundary per RIFF spec)
            let next_offset = chunk_start + chunk_size + (chunk_size % 2);
            if next_offset > data.len() {
                break;
            }
            cur.seek(SeekFrom::Start(next_offset as u64)).map_err(|e| e.to_string())?;
        }

        let pcm_data = raw_pcm_bytes.ok_or_else(|| "WAV file does not contain a 'data' chunk".to_string())?;

        // 2. Decode raw audio bytes into 16-bit PCM frames
        let pcm_16 = Self::decode_to_pcm16(pcm_data, audio_format, bits_per_sample)?;
        let num_frames = if channels > 0 { pcm_16.len() / (channels as usize) } else { pcm_16.len() };

        // 3. Fallback loop points: if not specified or invalid, default to full range or 50%..95% sustain loop
        if loop_end == 0 || loop_end >= num_frames as u32 {
            if num_frames > 100 {
                loop_start = (num_frames as f32 * 0.4) as u32;
                loop_end = (num_frames as f32 * 0.95) as u32;
            } else {
                loop_start = 0;
                loop_end = num_frames as u32;
            }
        }

        // 4. Root pitch detection: If not in smpl chunk, parse from filename (e.g. "harmonium-c4", "Flute A#4")
        let root_key = smpl_root_key.unwrap_or_else(|| Self::detect_note_from_filename(name_hint).unwrap_or(60));
        let fine_tune_cents = smpl_fine_cents.unwrap_or(0);

        Ok(WavInfo {
            channels,
            sample_rate,
            bits_per_sample,
            audio_format,
            num_frames,
            root_key,
            fine_tune_cents,
            loop_start,
            loop_end,
            pcm_16,
        })
    }

    /// Decode audio into normalized 16-bit signed PCM
    fn decode_to_pcm16(data: &[u8], audio_format: u16, bits_per_sample: u16) -> Result<Vec<i16>, String> {
        let mut cur = Cursor::new(data);
        let mut output = Vec::new();

        match (audio_format, bits_per_sample) {
            (1, 16) => {
                // Standard 16-bit PCM Little Endian
                let count = data.len() / 2;
                output.reserve(count);
                for _ in 0..count {
                    let s = cur.read_i16::<LittleEndian>().map_err(|e| e.to_string())?;
                    output.push(s);
                }
            }
            (1, 8) => {
                // 8-bit unsigned PCM (0..255, where 128 is center 0)
                output.reserve(data.len());
                for &b in data {
                    let centered = (b as i32) - 128;
                    output.push((centered * 256) as i16);
                }
            }
            (1, 24) => {
                // 24-bit PCM Little Endian
                let count = data.len() / 3;
                output.reserve(count);
                for _ in 0..count {
                    let b0 = cur.read_u8().map_err(|e| e.to_string())? as i32;
                    let b1 = cur.read_u8().map_err(|e| e.to_string())? as i32;
                    let b2 = cur.read_i8().map_err(|e| e.to_string())? as i32;
                    let val24 = (b2 << 16) | (b1 << 8) | b0;
                    output.push((val24 >> 8) as i16);
                }
            }
            (3, 32) => {
                // 32-bit IEEE Float (-1.0 to +1.0)
                let count = data.len() / 4;
                output.reserve(count);
                for _ in 0..count {
                    let f = cur.read_f32::<LittleEndian>().map_err(|e| e.to_string())?;
                    let clamped = f.clamp(-1.0, 1.0);
                    output.push((clamped * 32767.0) as i16);
                }
            }
            _ => {
                return Err(format!("Unsupported WAV format: code {}, {} bits per sample", audio_format, bits_per_sample));
            }
        }

        Ok(output)
    }

    /// Extract MIDI root note from filename (e.g. "harmonium-c4" -> 60, "Flute A#4" -> 70, "Flute G3" -> 55)
    /// Follows standard Polyphone / Soundfont note naming conventions.
    pub fn detect_note_from_filename(name: &str) -> Option<u8> {
        let name_upper = name.to_uppercase();

        let notes = [
            ("C#", 1), ("DB", 1),
            ("D#", 3), ("EB", 3),
            ("F#", 6), ("GB", 6),
            ("G#", 8), ("AB", 8),
            ("A#", 10), ("BB", 10),
            ("C", 0),
            ("D", 2),
            ("E", 4),
            ("F", 5),
            ("G", 7),
            ("A", 9),
            ("B", 11),
        ];

        for &(note_str, semitone) in &notes {
            if let Some(pos) = name_upper.find(note_str) {
                // Look at the character immediately following the note name for octave number (-1 to 9)
                let rest = &name_upper[pos + note_str.len()..];
                let mut octave_val: Option<i32> = None;

                let trimmed = rest.trim_start_matches(|c| c == '-' || c == '_' || c == ' ');
                if let Some(first_char) = trimmed.chars().next() {
                    if let Some(d) = first_char.to_digit(10) {
                        octave_val = Some(d as i32);
                    }
                }

                if let Some(oct) = octave_val {
                    // Standard MIDI: C4 = 60, C-1 = 0, C0 = 12
                    let midi = (oct + 1) * 12 + semitone;
                    if (0..=127).contains(&midi) {
                        return Some(midi as u8);
                    }
                }
            }
        }

        None
    }

    /// Convert `WavInfo` into a high-level `VhlSample` ready for compilation
    pub fn to_vhl_sample(info: WavInfo, name: impl Into<String>) -> VhlSample {
        VhlSample {
            name: name.into(),
            root_key: info.root_key,
            sample_rate: info.sample_rate,
            fine_tune_cents: info.fine_tune_cents,
            loop_start_frame: info.loop_start,
            loop_end_frame: info.loop_end,
            channels: info.channels.min(255) as u8,
            pcm_frames: info.pcm_16,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_note_detection_from_filenames() {
        assert_eq!(WavReader::detect_note_from_filename("harmonium-c4"), Some(60));
        assert_eq!(WavReader::detect_note_from_filename("harmomium_c4_reduced"), Some(60));
        assert_eq!(WavReader::detect_note_from_filename("Flute A#4"), Some(70));
        assert_eq!(WavReader::detect_note_from_filename("Flute G3 -133"), Some(55));
        assert_eq!(WavReader::detect_note_from_filename("Flute D#5 -82"), Some(75));
        assert_eq!(WavReader::detect_note_from_filename("single-piano-note-c4"), Some(60));
    }

    #[test]
    fn test_real_project_wav_file_parsing() {
        let wav_path = r"D:\TYBSC\TYBSC Arduino Project\Esp32 Digital Musical Workstation\Instruments\samples\harmomium_c4_reduced.wav";
        if std::path::Path::new(wav_path).exists() {
            let wav = WavReader::read_from_file(wav_path).expect("Should parse real project WAV");
            assert!(wav.root_key == 60 || wav.root_key == 62); // 62 if smpl chunk specifies D4 root
            assert!(wav.sample_rate > 0);
            assert!(!wav.pcm_16.is_empty());
        }
    }
}
