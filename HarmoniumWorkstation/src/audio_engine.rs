//! Real-Time Audio Synthesizer Engine & Device Output
//!
//! Provides a polyphonic audio playback engine via `cpal` for auditioning
//! the currently loaded VHL / WAV samples with pitch-shifting, looping,
//! DAHDSR envelope modulation, and biquad filter.

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::sync::{Arc, Mutex};
use crate::vhl::types::VHLEnvelope;

/// An active voice playing a pitch-shifted sample
#[derive(Clone)]
pub struct ActiveVoice {
    pub key: u8,
    pub midi_note: u8,
    pub sample_pos: f64,
    pub pitch_ratio: f64,
    pub volume: f32,
    pub env_stage: EnvStage,
    pub env_time_samples: usize,
    pub is_released: bool,
    pub is_active: bool,
    // Direct Form II Biquad filter state
    pub filter_x1: f32,
    pub filter_x2: f32,
    pub filter_y1: f32,
    pub filter_y2: f32,
}

#[derive(Clone, Copy, PartialEq)]
pub enum EnvStage {
    Attack,
    Hold,
    Decay,
    Sustain,
    Release,
    Finished,
}

/// Sound generator state shared between GUI thread and cpal audio callback
pub struct AudioEngineState {
    pub pcm_frames: Vec<i16>,
    pub sample_root_key: u8,
    pub loop_start: u32,
    pub loop_end: u32,
    pub sample_rate: u32,

    // Envelope settings
    pub attack_ms: f32,
    pub hold_ms: f32,
    pub decay_ms: f32,
    pub sustain_pct: f32,
    pub release_ms: f32,

    // Filter settings
    pub cutoff_hz: f32,
    pub resonance_db: f32,

    pub master_volume: f32,
    pub voices: Vec<ActiveVoice>,
}

impl Default for AudioEngineState {
    fn default() -> Self {
        Self {
            pcm_frames: Vec::new(),
            sample_root_key: 60,
            loop_start: 0,
            loop_end: 0,
            sample_rate: 44100,
            attack_ms: 15.0,
            hold_ms: 5.0,
            decay_ms: 200.0,
            sustain_pct: 85.0,
            release_ms: 60.0,
            cutoff_hz: 4800.0,
            resonance_db: 2.5,
            master_volume: 0.8,
            voices: Vec::new(),
        }
    }
}

pub struct AudioPlayer {
    pub state: Arc<Mutex<AudioEngineState>>,
    _stream: Option<cpal::Stream>,
}

impl AudioPlayer {
    pub fn new() -> Self {
        let state = Arc::new(Mutex::new(AudioEngineState::default()));
        let state_clone = Arc::clone(&state);

        let stream = Self::init_cpal_stream(state_clone);

        Self {
            state,
            _stream: stream,
        }
    }

    fn init_cpal_stream(state: Arc<Mutex<AudioEngineState>>) -> Option<cpal::Stream> {
        let host = cpal::default_host();
        let device = host.default_output_device()?;
        let config = device.default_output_config().ok()?;

        let sample_rate = config.sample_rate().0;
        let channels = config.channels() as usize;

        let err_fn = |err| eprintln!("Audio stream error: {}", err);

        let stream = match config.sample_format() {
            cpal::SampleFormat::F32 => device.build_output_stream(
                &config.into(),
                move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
                    Self::process_audio_f32(data, channels, sample_rate, &state);
                },
                err_fn,
                None,
            ).ok()?,
            _ => return None,
        };

        let _ = stream.play();
        Some(stream)
    }

    fn process_audio_f32(output: &mut [f32], channels: usize, host_sample_rate: u32, state_lock: &Arc<Mutex<AudioEngineState>>) {
        let mut state = match state_lock.try_lock() {
            Ok(guard) => guard,
            Err(_) => {
                for sample in output.iter_mut() {
                    *sample = 0.0;
                }
                return;
            }
        };

        if state.pcm_frames.is_empty() || state.voices.is_empty() {
            for sample in output.iter_mut() {
                *sample = 0.0;
            }
            return;
        }

        let pcm_len = state.pcm_frames.len();
        let loop_start = state.loop_start as f64;
        let loop_end = (state.loop_end as f64).min(pcm_len as f64);
        let has_loop = loop_end > loop_start + 10.0;

        let attack_samples = (state.attack_ms * 0.001 * host_sample_rate as f32) as usize;
        let hold_samples = (state.hold_ms * 0.001 * host_sample_rate as f32) as usize;
        let decay_samples = (state.decay_ms * 0.001 * host_sample_rate as f32).max(1.0) as usize;
        let sustain_lvl = (state.sustain_pct / 100.0).clamp(0.0, 1.0);
        let release_samples = (state.release_ms * 0.001 * host_sample_rate as f32).max(1.0) as usize;
        let master_vol = state.master_volume;

        let num_frames = output.len() / channels;
        let mut voices = std::mem::take(&mut state.voices);

        let host_sr = host_sample_rate as f32;
        // Pre-calculate 2-pole resonant low-pass filter coefficients (Biquad)
        let cutoff = state.cutoff_hz.clamp(100.0, host_sr * 0.48);
        let q = 10.0_f32.powf(state.resonance_db / 20.0).max(0.5);
        let omega = 2.0 * std::f32::consts::PI * cutoff / host_sr;
        let alpha = omega.sin() / (2.0 * q);
        let cos_omega = omega.cos();

        let b0 = (1.0 - cos_omega) / 2.0;
        let b1 = 1.0 - cos_omega;
        let b2 = (1.0 - cos_omega) / 2.0;
        let a0 = 1.0 + alpha;
        let a1 = -2.0 * cos_omega;
        let a2 = 1.0 - alpha;

        let nb0 = b0 / a0;
        let nb1 = b1 / a0;
        let nb2 = b2 / a0;
        let na1 = a1 / a0;
        let na2 = a2 / a0;

        for frame_idx in 0..num_frames {
            let mut mix = 0.0f32;

            for voice in voices.iter_mut() {
                if !voice.is_active {
                    continue;
                }

                // Resample interpolation (linear interpolation between frames)
                let idx0 = voice.sample_pos as usize;
                let idx1 = (idx0 + 1).min(pcm_len - 1);
                let frac = (voice.sample_pos - idx0 as f64) as f32;

                let s0 = state.pcm_frames[idx0] as f32 / 32768.0;
                let s1 = state.pcm_frames[idx1] as f32 / 32768.0;
                let raw_val = s0 + frac * (s1 - s0);

                // Apply Direct Form II Biquad Filter to voice
                let x0 = raw_val;
                let y0 = nb0 * x0 + nb1 * voice.filter_x1 + nb2 * voice.filter_x2
                    - na1 * voice.filter_y1 - na2 * voice.filter_y2;

                voice.filter_x2 = voice.filter_x1;
                voice.filter_x1 = x0;
                voice.filter_y2 = voice.filter_y1;
                voice.filter_y1 = y0;

                // DAHDSR envelope calculation
                let env_gain = match voice.env_stage {
                    EnvStage::Attack => {
                        if attack_samples > 0 {
                            voice.env_time_samples as f32 / attack_samples as f32
                        } else {
                            1.0
                        }
                    }
                    EnvStage::Hold => 1.0,
                    EnvStage::Decay => {
                        let t = (voice.env_time_samples as f32 / decay_samples as f32).min(1.0);
                        1.0 - t * (1.0 - sustain_lvl)
                    }
                    EnvStage::Sustain => sustain_lvl,
                    EnvStage::Release => {
                        let t = (voice.env_time_samples as f32 / release_samples as f32).min(1.0);
                        (sustain_lvl * (1.0 - t)).max(0.0)
                    }
                    EnvStage::Finished => 0.0,
                };

                mix += y0 * env_gain * voice.volume;

                // Advance envelope
                voice.env_time_samples += 1;
                match voice.env_stage {
                    EnvStage::Attack if voice.env_time_samples >= attack_samples => {
                        voice.env_stage = EnvStage::Hold;
                        voice.env_time_samples = 0;
                    }
                    EnvStage::Hold if voice.env_time_samples >= hold_samples => {
                        voice.env_stage = EnvStage::Decay;
                        voice.env_time_samples = 0;
                    }
                    EnvStage::Decay if voice.env_time_samples >= decay_samples => {
                        voice.env_stage = EnvStage::Sustain;
                        voice.env_time_samples = 0;
                    }
                    EnvStage::Release if voice.env_time_samples >= release_samples => {
                        voice.env_stage = EnvStage::Finished;
                        voice.is_active = false;
                    }
                    _ => {}
                }

                // Advance sample position
                voice.sample_pos += voice.pitch_ratio;
                if has_loop && voice.sample_pos >= loop_end {
                    voice.sample_pos = loop_start + (voice.sample_pos - loop_end);
                } else if voice.sample_pos >= pcm_len as f64 {
                    voice.is_active = false;
                }
            }

            let final_sample = (mix * master_vol).clamp(-1.0, 1.0);

            for c in 0..channels {
                output[frame_idx * channels + c] = final_sample;
            }
        }

        // Clean up inactive voices and put back into state
        voices.retain(|v| v.is_active);
        state.voices = voices;
    }

    /// Trigger a note (key 0..60, midi 36..96)
    pub fn note_on(&self, key: u8, midi_note: u8, velocity: u8) {
        if let Ok(mut state) = self.state.lock() {
            let root = state.sample_root_key as f64;
            let note = midi_note as f64;
            let semitone_diff = note - root;
            let pitch_ratio = 2.0_f64.powf(semitone_diff / 12.0);
            let volume = (velocity as f32 / 127.0).clamp(0.1, 1.0);

            // Stop existing voice for same key if any
            for v in state.voices.iter_mut() {
                if v.key == key {
                    v.env_stage = EnvStage::Release;
                    v.env_time_samples = 0;
                }
            }

            state.voices.push(ActiveVoice {
                key,
                midi_note,
                sample_pos: 0.0,
                pitch_ratio,
                volume,
                env_stage: EnvStage::Attack,
                env_time_samples: 0,
                is_released: false,
                is_active: true,
                filter_x1: 0.0,
                filter_x2: 0.0,
                filter_y1: 0.0,
                filter_y2: 0.0,
            });
        }
    }

    /// Release a note
    pub fn note_off(&self, key: u8) {
        if let Ok(mut state) = self.state.lock() {
            for v in state.voices.iter_mut() {
                if v.key == key && !v.is_released {
                    v.is_released = true;
                    v.env_stage = EnvStage::Release;
                    v.env_time_samples = 0;
                }
            }
        }
    }

    /// Sync active sample and envelope params from GUI into audio engine
    pub fn sync_from_project(&self, pcm: &[i16], root: u8, loop_s: u32, loop_e: u32, env: &VHLEnvelope, cutoff_hz: f32, resonance_db: f32, vol: f32) {
        if let Ok(mut state) = self.state.lock() {
            state.pcm_frames = pcm.to_vec();
            state.sample_root_key = root;
            state.loop_start = loop_s;
            state.loop_end = loop_e;
            state.attack_ms = env.attack_ms as f32;
            state.hold_ms = env.hold_ms as f32;
            state.decay_ms = env.decay_ms as f32;
            state.sustain_pct = (env.sustain_level as f32 / 1000.0) * 100.0;
            state.release_ms = env.release_ms as f32;
            state.cutoff_hz = cutoff_hz;
            state.resonance_db = resonance_db;
            state.master_volume = vol.clamp(0.0, 1.0);
        }
    }
}
