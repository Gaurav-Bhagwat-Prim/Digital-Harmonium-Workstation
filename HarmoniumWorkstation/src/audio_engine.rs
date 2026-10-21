//! Real-time polyphonic multi-sample synth engine (cpal output + offline WAV render).
//!
//! Design (why it is built this way):
//!  * The GUI never touches the audio thread's data. It sends small commands
//!    (note on/off, "use this patch") through a channel, and the audio callback
//!    drains them at the start of every buffer. No locks are taken in the callback,
//!    so GUI activity can never starve the audio thread or poison a mutex.
//!  * A `Patch` is an immutable snapshot of "which samples exist" and "which region
//!    plays which sample with which envelope/filter/tuning". Editing any region
//!    just builds a new Patch and swaps it in. Every voice keeps its own `Arc` to
//!    the sample it started with, so replacing or deleting a sample while a note is
//!    sounding can no longer index out of bounds (the old engine did, which killed
//!    the audio thread and made the sound disappear for good).
//!  * Every matching region starts its own voice, so each region can use a
//!    different sample.
//!  * The same `Synth` renders live audio and the offline "Export WAV".

use crate::vhl::compiler::{VhlProject, VhlSample};
use crate::vhl::types::*;
use crate::vhr::types::*;
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender};
use std::sync::{Arc, Mutex};

pub const MAX_VOICES: usize = 64;

// ───────────────────────────── filter maths (shared with the GUI plot) ─────────────────────────────

/// Resonance (dB, 0 = flat Butterworth) -> biquad Q.
pub fn lowpass_q(res_db: f32) -> f32 {
    std::f32::consts::FRAC_1_SQRT_2 * 10.0_f32.powf(res_db.clamp(0.0, 24.0) / 20.0)
}

/// RBJ low-pass biquad, coefficients normalised by a0: (b0, b1, b2, a1, a2).
pub fn biquad_lowpass(cutoff_hz: f32, q: f32, sample_rate: f32) -> (f32, f32, f32, f32, f32) {
    let fc = cutoff_hz.clamp(20.0, sample_rate * 0.45);
    let w0 = 2.0 * std::f32::consts::PI * fc / sample_rate;
    let (sin_w, cos_w) = w0.sin_cos();
    let alpha = sin_w / (2.0 * q.max(0.1));
    let a0 = 1.0 + alpha;
    let b1 = (1.0 - cos_w) / a0;
    (b1 * 0.5, b1, b1 * 0.5, -2.0 * cos_w / a0, (1.0 - alpha) / a0)
}

/// Exact magnitude (dB) of the engine's low-pass at `freq_hz`. The inspector plot uses this,
/// so the curve on screen is the curve you actually hear.
pub fn lowpass_mag_db(freq_hz: f32, cutoff_hz: f32, res_db: f32, sample_rate: f32) -> f32 {
    let (b0, b1, b2, a1, a2) = biquad_lowpass(cutoff_hz, lowpass_q(res_db), sample_rate);
    let w = 2.0 * std::f32::consts::PI * freq_hz.min(sample_rate * 0.499) / sample_rate;
    let (c1, s1) = (w.cos(), w.sin());
    let (c2, s2) = ((2.0 * w).cos(), (2.0 * w).sin());
    let nr = b0 + b1 * c1 + b2 * c2;
    let ni = -(b1 * s1 + b2 * s2);
    let dr = 1.0 + a1 * c1 + a2 * c2;
    let di = -(a1 * s1 + a2 * s2);
    let mag = ((nr * nr + ni * ni) / (dr * dr + di * di).max(1e-20)).sqrt();
    20.0 * mag.max(1e-6).log10()
}

// ───────────────────────────── Freeverb & Stereo Chorus DSP ─────────────────────────────

const NUM_COMBS: usize = 8;
const NUM_ALLPASSES: usize = 4;
const STEREO_SPREAD: usize = 23;

const COMB_TUNING: [usize; NUM_COMBS] = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617];
const ALLPASS_TUNING: [usize; NUM_ALLPASSES] = [556, 441, 341, 225];

#[derive(Clone)]
struct CombFilter {
    buffer: Vec<f32>,
    buf_idx: usize,
    filter_store: f32,
    damp: f32,
    feedback: f32,
}

impl CombFilter {
    fn new(size: usize) -> Self {
        Self {
            buffer: vec![0.0; size.max(1)],
            buf_idx: 0,
            filter_store: 0.0,
            damp: 0.2,
            feedback: 0.84,
        }
    }

    #[inline(always)]
    fn process(&mut self, input: f32) -> f32 {
        let output = self.buffer[self.buf_idx];
        self.filter_store = output * (1.0 - self.damp) + self.filter_store * self.damp;
        self.buffer[self.buf_idx] = input + self.filter_store * self.feedback;
        self.buf_idx += 1;
        if self.buf_idx >= self.buffer.len() {
            self.buf_idx = 0;
        }
        output
    }

    fn set_damp(&mut self, val: f32) {
        self.damp = val.clamp(0.0, 1.0) * 0.4;
    }

    fn set_feedback(&mut self, val: f32) {
        self.feedback = (0.7 + 0.28 * val.clamp(0.0, 1.0)).clamp(0.0, 0.98);
    }
}

#[derive(Clone)]
struct AllpassFilter {
    buffer: Vec<f32>,
    buf_idx: usize,
    feedback: f32,
}

impl AllpassFilter {
    fn new(size: usize) -> Self {
        Self {
            buffer: vec![0.0; size.max(1)],
            buf_idx: 0,
            feedback: 0.5,
        }
    }

    #[inline(always)]
    fn process(&mut self, input: f32) -> f32 {
        let buf_out = self.buffer[self.buf_idx];
        let output = -input + buf_out;
        self.buffer[self.buf_idx] = input + buf_out * self.feedback;
        self.buf_idx += 1;
        if self.buf_idx >= self.buffer.len() {
            self.buf_idx = 0;
        }
        output
    }
}

pub struct Freeverb {
    combs_l: Vec<CombFilter>,
    combs_r: Vec<CombFilter>,
    allpasses_l: Vec<AllpassFilter>,
    allpasses_r: Vec<AllpassFilter>,
    wet: f32,
}

impl Freeverb {
    pub fn new(sr: f32) -> Self {
        let scale = sr / 44100.0;
        let mut combs_l = Vec::with_capacity(NUM_COMBS);
        let mut combs_r = Vec::with_capacity(NUM_COMBS);
        for &tun in &COMB_TUNING {
            let s_l = (tun as f32 * scale).round() as usize;
            let s_r = ((tun + STEREO_SPREAD) as f32 * scale).round() as usize;
            combs_l.push(CombFilter::new(s_l));
            combs_r.push(CombFilter::new(s_r));
        }
        let mut allpasses_l = Vec::with_capacity(NUM_ALLPASSES);
        let mut allpasses_r = Vec::with_capacity(NUM_ALLPASSES);
        for &tun in &ALLPASS_TUNING {
            let s_l = (tun as f32 * scale).round() as usize;
            let s_r = ((tun + STEREO_SPREAD) as f32 * scale).round() as usize;
            allpasses_l.push(AllpassFilter::new(s_l));
            allpasses_r.push(AllpassFilter::new(s_r));
        }
        let mut verb = Self {
            combs_l,
            combs_r,
            allpasses_l,
            allpasses_r,
            wet: 1.0,
        };
        verb.set_room_size(0.65);
        verb.set_damp(0.3);
        verb
    }

    pub fn set_room_size(&mut self, val: f32) {
        for c in self.combs_l.iter_mut().chain(self.combs_r.iter_mut()) {
            c.set_feedback(val);
        }
    }

    pub fn set_damp(&mut self, val: f32) {
        for c in self.combs_l.iter_mut().chain(self.combs_r.iter_mut()) {
            c.set_damp(val);
        }
    }

    #[inline(always)]
    pub fn process(&mut self, in_l: f32, in_r: f32) -> (f32, f32) {
        let input_mono = (in_l + in_r) * 0.5 * 0.15; // Scaled down to prevent comb overflow
        let mut out_l = 0.0_f32;
        let mut out_r = 0.0_f32;

        for c in self.combs_l.iter_mut() {
            out_l += c.process(input_mono);
        }
        for c in self.combs_r.iter_mut() {
            out_r += c.process(input_mono);
        }

        for a in self.allpasses_l.iter_mut() {
            out_l = a.process(out_l);
        }
        for a in self.allpasses_r.iter_mut() {
            out_r = a.process(out_r);
        }

        (out_l * self.wet, out_r * self.wet)
    }
}

pub struct StereoChorus {
    buffer_l: Vec<f32>,
    buffer_r: Vec<f32>,
    write_idx: usize,
    phase_l: f32,
    phase_r: f32,
    sr: f32,
    depth_samples: f32,
    base_delay_samples: f32,
    rate_hz: f32,
}

impl StereoChorus {
    pub fn new(sr: f32) -> Self {
        let max_samples = (sr * 0.05).ceil() as usize + 64; // ~50ms max delay
        Self {
            buffer_l: vec![0.0; max_samples.max(2048)],
            buffer_r: vec![0.0; max_samples.max(2048)],
            write_idx: 0,
            phase_l: 0.0,
            phase_r: std::f32::consts::FRAC_PI_2, // 90 degree stereo quadrature
            sr,
            depth_samples: 0.0025 * sr, // 2.5 ms depth
            base_delay_samples: 0.010 * sr, // 10 ms base delay
            rate_hz: 1.2, // 1.2 Hz
        }
    }

    #[inline(always)]
    pub fn process(&mut self, in_l: f32, in_r: f32) -> (f32, f32) {
        let buf_len = self.buffer_l.len();
        self.buffer_l[self.write_idx] = in_l;
        self.buffer_r[self.write_idx] = in_r;

        // Modulated read tap L
        let d_l = self.base_delay_samples + self.depth_samples * self.phase_l.sin();
        let mut read_pos_l = self.write_idx as f32 - d_l;
        while read_pos_l < 0.0 {
            read_pos_l += buf_len as f32;
        }
        let idx0_l = read_pos_l as usize % buf_len;
        let idx1_l = (idx0_l + 1) % buf_len;
        let frac_l = read_pos_l - read_pos_l.floor();
        let wet_l = self.buffer_l[idx0_l] + frac_l * (self.buffer_l[idx1_l] - self.buffer_l[idx0_l]);

        // Modulated read tap R
        let d_r = self.base_delay_samples + self.depth_samples * self.phase_r.sin();
        let mut read_pos_r = self.write_idx as f32 - d_r;
        while read_pos_r < 0.0 {
            read_pos_r += buf_len as f32;
        }
        let idx0_r = read_pos_r as usize % buf_len;
        let idx1_r = (idx0_r + 1) % buf_len;
        let frac_r = read_pos_r - read_pos_r.floor();
        let wet_r = self.buffer_r[idx0_r] + frac_r * (self.buffer_r[idx1_r] - self.buffer_r[idx0_r]);

        self.write_idx = (self.write_idx + 1) % buf_len;

        let inc = 2.0 * std::f32::consts::PI * self.rate_hz / self.sr;
        self.phase_l += inc;
        if self.phase_l >= 2.0 * std::f32::consts::PI {
            self.phase_l -= 2.0 * std::f32::consts::PI;
        }
        self.phase_r += inc;
        if self.phase_r >= 2.0 * std::f32::consts::PI {
            self.phase_r -= 2.0 * std::f32::consts::PI;
        }

        (wet_l, wet_r)
    }
}

// ───────────────────────────── SoundFont 2 Modulator Curves & Real-Time Evaluator ─────────────────────────────

pub struct ModCurves;

impl ModCurves {
    /// Calculate base transfer curve for input index v in [0..127]
    /// 0 = Linear, 1 = Concave, 2 = Convex, 3 = Switch
    #[inline(always)]
    pub fn eval_base(v: u8, curve_type: u8) -> f32 {
        let v_clamp = v.min(127) as f32;
        match curve_type {
            SF_CURVE_LINEAR => v_clamp / 127.0,
            SF_CURVE_CONCAVE => {
                if v == 0 {
                    0.0
                } else if v >= 127 {
                    1.0
                } else {
                    let db = -20.0 * (v_clamp / 127.0).log10();
                    (1.0 - (db / 96.0)).clamp(0.0, 1.0)
                }
            }
            SF_CURVE_CONVEX => {
                if v == 0 {
                    0.0
                } else if v >= 127 {
                    1.0
                } else {
                    let db = -20.0 * ((127.0 - v_clamp) / 127.0).log10();
                    (db / 96.0).clamp(0.0, 1.0)
                }
            }
            SF_CURVE_SWITCH => {
                if v >= 64 { 1.0 } else { 0.0 }
            }
            _ => v_clamp / 127.0,
        }
    }

    /// Evaluates source value with Direction and Polarity
    #[inline(always)]
    pub fn eval_source(v: u8, curve_type: u8, descending: bool, bipolar: bool) -> f32 {
        let base = Self::eval_base(v, curve_type);
        let dir = if descending { 1.0 - base } else { base };
        if bipolar {
            2.0 * dir - 1.0
        } else {
            dir
        }
    }

    /// Continuous version for envelopes and LFOs where input is already normalized 0.0 .. 1.0
    #[inline(always)]
    pub fn eval_continuous(val_01: f32, curve_type: u8, descending: bool, bipolar: bool) -> f32 {
        let v = (val_01.clamp(0.0, 1.0) * 127.0).round() as u8;
        Self::eval_source(v, curve_type, descending, bipolar)
    }

    /// Evaluates piecewise 5-node custom curve
    #[inline(always)]
    pub fn eval_custom(x: f32, points: &[f32; 5]) -> f32 {
        let x = x.clamp(0.0, 1.0);
        let scaled = x * 4.0;
        let seg = (scaled.floor() as usize).min(3);
        let t = scaled - seg as f32;
        let y0 = points[seg];
        let y1 = points[seg + 1];
        (y0 + t * (y1 - y0)).clamp(0.0, 1.0)
    }

    #[inline(always)]
    pub fn eval_base_custom(v: u8, curve_type: u8, custom: &[f32; 5]) -> f32 {
        if curve_type == SF_CURVE_CUSTOM {
            Self::eval_custom(v.min(127) as f32 / 127.0, custom)
        } else {
            Self::eval_base(v, curve_type)
        }
    }

    #[inline(always)]
    pub fn eval_source_custom(v: u8, curve_type: u8, descending: bool, bipolar: bool, custom: &[f32; 5]) -> f32 {
        let base = Self::eval_base_custom(v, curve_type, custom);
        let dir = if descending { 1.0 - base } else { base };
        if bipolar {
            2.0 * dir - 1.0
        } else {
            dir
        }
    }

    #[inline(always)]
    pub fn eval_continuous_custom(val_01: f32, curve_type: u8, descending: bool, bipolar: bool, custom: &[f32; 5]) -> f32 {
        let x = val_01.clamp(0.0, 1.0);
        let base = if curve_type == SF_CURVE_CUSTOM {
            Self::eval_custom(x, custom)
        } else {
            let v = (x * 127.0).round() as u8;
            Self::eval_base(v, curve_type)
        };
        let dir = if descending { 1.0 - base } else { base };
        if bipolar {
            2.0 * dir - 1.0
        } else {
            dir
        }
    }
}

#[derive(Clone, Copy, Default)]
pub struct ModDeltas {
    pub atten_db: f32,        // initialAttenuation (centibels / 10)
    pub filter_fc_cents: f32, // initialFilterFc
    pub filter_q_cb: f32,     // initialFilterQ
    pub pitch_cents: f32,     // fineTune / modEnvToPitch / vibLfoToPitch
    pub pan_offset: f32,      // pan [-1..+1]
    pub reverb_send: f32,     // reverbEffectsSend [0..1]
    pub chorus_send: f32,     // chorusEffectsSend [0..1]
    pub tremolo_db: f32,      // modLfoToVolume
}

impl ModDeltas {
    pub fn compute(
        mods: &[VHLModulator_v2],
        custom_curves: &[[f32; 5]],
        key: u8,
        vel: u8,
        mod_env: f32,
        mod_lfo: f32,
        vib_lfo: f32,
        cc: &[u8; 128],
        pitch_wheel: i16,
        channel_pressure: u8,
    ) -> Self {
        let mut deltas = Self::default();

        for (m_idx, m) in mods.iter().enumerate() {
            if m.flags & 1 == 0 {
                continue;
            }

            let custom = custom_curves.get(m_idx).unwrap_or(&[0.0, 0.25, 0.5, 0.75, 1.0]);
            let (ctrl1, is_cc1, curve1, desc1, bipol1) = VHLModulator_v2::unpack_source(m.src_oper);
            let s1 = if is_cc1 {
                let v = cc[ctrl1 as usize & 0x7F];
                ModCurves::eval_source_custom(v, curve1, desc1, bipol1, custom)
            } else {
                match ctrl1 as u16 {
                    SF_MOD_SRC_NONE => {
                        if desc1 { 0.0 } else { 1.0 }
                    }
                    SF_MOD_SRC_VELOCITY => ModCurves::eval_source_custom(vel, curve1, desc1, bipol1, custom),
                    SF_MOD_SRC_KEY => ModCurves::eval_source_custom(key, curve1, desc1, bipol1, custom),
                    SF_MOD_SRC_CHANNEL_PRESSURE => ModCurves::eval_source_custom(channel_pressure, curve1, desc1, bipol1, custom),
                    SF_MOD_SRC_POLY_PRESSURE => ModCurves::eval_source_custom(channel_pressure, curve1, desc1, bipol1, custom),
                    SF_MOD_SRC_PITCH_WHEEL => {
                        let norm = (((pitch_wheel as f32 + 8192.0) / 16383.0) * 127.0).clamp(0.0, 127.0) as u8;
                        ModCurves::eval_source_custom(norm, curve1, desc1, bipol1, custom)
                    }
                    SF_MOD_SRC_MOD_ENV => ModCurves::eval_continuous_custom(mod_env, curve1, desc1, bipol1, custom),
                    SF_MOD_SRC_MOD_LFO => {
                        let norm = (mod_lfo * 0.5 + 0.5).clamp(0.0, 1.0);
                        ModCurves::eval_continuous_custom(norm, curve1, desc1, bipol1, custom)
                    }
                    SF_MOD_SRC_VIB_LFO => {
                        let norm = (vib_lfo * 0.5 + 0.5).clamp(0.0, 1.0);
                        ModCurves::eval_continuous_custom(norm, curve1, desc1, bipol1, custom)
                    }
                    _ => 0.0,
                }
            };

            let s2 = if m.amt_src_oper == 0 {
                1.0
            } else {
                let (ctrl2, is_cc2, curve2, desc2, bipol2) = VHLModulator_v2::unpack_source(m.amt_src_oper);
                if is_cc2 {
                    let v = cc[ctrl2 as usize & 0x7F];
                    ModCurves::eval_source(v, curve2, desc2, bipol2)
                } else {
                    match ctrl2 as u16 {
                        SF_MOD_SRC_VELOCITY => ModCurves::eval_source(vel, curve2, desc2, bipol2),
                        SF_MOD_SRC_KEY => ModCurves::eval_source(key, curve2, desc2, bipol2),
                        SF_MOD_SRC_PITCH_WHEEL_SENS => 1.0,
                        _ => 1.0,
                    }
                }
            };

            let mut out = s1 * s2 * (m.amount as f32);
            if m.transform == SF_TRANSFORM_ABSOLUTE {
                out = out.abs();
            }

            match m.dest_oper {
                SF_GEN_INITIAL_ATTENUATION => {
                    deltas.atten_db += out / 10.0;
                }
                SF_GEN_INITIAL_FILTER_FC | SF_GEN_MOD_ENV_TO_FILTER_FC | SF_GEN_MOD_LFO_TO_FILTER_FC => {
                    deltas.filter_fc_cents += out;
                }
                SF_GEN_INITIAL_FILTER_Q => {
                    deltas.filter_q_cb += out;
                }
                SF_GEN_FINE_TUNE | SF_GEN_VIB_LFO_TO_PITCH | SF_GEN_MOD_LFO_TO_PITCH | SF_GEN_MOD_ENV_TO_PITCH => {
                    deltas.pitch_cents += out;
                }
                SF_GEN_COARSE_TUNE => {
                    deltas.pitch_cents += out * 100.0;
                }
                SF_GEN_PAN => {
                    deltas.pan_offset += out / 500.0;
                }
                SF_GEN_REVERB_EFFECTS_SEND => {
                    deltas.reverb_send += out / 1000.0;
                }
                SF_GEN_CHORUS_EFFECTS_SEND => {
                    deltas.chorus_send += out / 1000.0;
                }
                SF_GEN_MOD_LFO_TO_VOLUME => {
                    deltas.tremolo_db += out / 10.0;
                }
                _ => {}
            }
        }

        deltas
    }
}

// ───────────────────────────── patch (immutable snapshot) ─────────────────────────────

#[derive(Clone)]
pub struct SampleInfo {
    pub pcm: Arc<Vec<f32>>, // mono, -1..1
    pub rate: f32,
    pub root_key: u8,
    pub fine_cents: f32,
    pub loop_start: f32,
    pub loop_end: f32,
}

#[derive(Clone, Copy)]
pub struct EnvSpec {
    pub delay_ms: f32,
    pub attack_ms: f32,
    pub hold_ms: f32,
    pub decay_ms: f32,
    pub sustain: f32, // 0..1
    pub release_ms: f32,
    pub key_to_hold: f32,  // timecents per key
    pub key_to_decay: f32, // timecents per key
}

#[derive(Clone, Copy)]
pub struct LfoSpec {
    pub delay_ms: f32,
    pub freq_hz: f32,
    pub to_pitch: f32,     // cents
    pub to_filter: f32,    // cents
    pub to_volume_db: f32, // dB
}

#[derive(Clone, Copy)]
pub struct VibLfoSpec {
    pub delay_ms: f32,
    pub freq_hz: f32,
    pub to_pitch: f32,     // cents
}

#[derive(Clone, Copy)]
pub struct RegionParams {
    pub key_lo: u8,
    pub key_hi: u8,
    pub vel_lo: u8,
    pub vel_hi: u8,
    pub sample: usize,
    pub loop_mode: u8, // 0 none, 1 continuous, anything else = loop while the key is held
    pub coarse: f32,   // semitones
    pub fine: f32,     // cents
    pub scale_tuning: f32, // cents per key (100 = normal)
    pub gain: f32,
    pub pan: f32, // -1..1
    pub reverb_send: f32, // 0..1
    pub chorus_send: f32, // 0..1
    pub env: EnvSpec,
    pub mod_env: EnvSpec,
    pub mod_env_to_pitch: f32,
    pub mod_env_to_filter: f32,
    pub lfo_mod: LfoSpec,
    pub lfo_vib: VibLfoSpec,
    pub cutoff_hz: f32,
    pub res_db: f32,
}

#[derive(Clone, Default)]
pub struct Patch {
    pub samples: Vec<SampleInfo>,
    pub regions: Vec<RegionParams>,
    pub modulators: Vec<VHLModulator_v2>,
    pub custom_curves: Vec<[f32; 5]>,
}

/// Mono float copy of a sample (stereo is averaged). Done once per sample, off the audio thread.
pub fn mono_pcm(s: &VhlSample) -> Vec<f32> {
    let ch = s.channels.max(1) as usize;
    if ch == 1 {
        s.pcm_frames.iter().map(|&v| v as f32 / 32768.0).collect()
    } else {
        s.pcm_frames.chunks(ch).map(|c| c.iter().map(|&v| v as f32).sum::<f32>() / (c.len() as f32 * 32768.0)).collect()
    }
}

impl Patch {
    /// `pcm[i]` must be `mono_pcm(&project.samples[i])` (the caller caches these).
    pub fn from_project(project: &VhlProject, pcm: &[Arc<Vec<f32>>]) -> Patch {
        let samples: Vec<SampleInfo> = project
            .samples
            .iter()
            .enumerate()
            .map(|(i, s)| {
                let n = pcm.get(i).map(|p| p.len()).unwrap_or(0) as f32;
                let ls = (s.loop_start_frame as f32).min(n);
                let le = (s.loop_end_frame as f32).min(n);
                SampleInfo {
                    pcm: pcm.get(i).cloned().unwrap_or_default(),
                    rate: s.sample_rate.max(1) as f32,
                    root_key: s.root_key,
                    fine_cents: s.fine_tune_cents as f32,
                    loop_start: ls,
                    loop_end: le,
                }
            })
            .collect();
        let regions = project
            .regions
            .iter()
            .map(|r| RegionParams {
                key_lo: r.key_lo,
                key_hi: r.key_hi,
                vel_lo: r.vel_lo,
                vel_hi: r.vel_hi,
                sample: r.sample_id as usize,
                loop_mode: r.loop_mode,
                coarse: r.coarse_tune as f32,
                fine: r.fine_tune as f32,
                scale_tuning: r.scale_tuning as f32,
                gain: 10.0_f32.powf(-(r.attenuation_cb as f32) / 200.0),
                pan: (r.pan as f32 / 500.0).clamp(-1.0, 1.0),
                reverb_send: (r.reverb_send as f32 / 100.0).clamp(0.0, 1.0),
                chorus_send: (r.chorus_send as f32 / 100.0).clamp(0.0, 1.0),
                env: EnvSpec {
                    delay_ms: r.vol_env.delay_ms as f32,
                    attack_ms: r.vol_env.attack_ms as f32,
                    hold_ms: r.vol_env.hold_ms as f32,
                    decay_ms: r.vol_env.decay_ms as f32,
                    sustain: (r.vol_env.sustain_level as f32 / 1000.0).clamp(0.0, 1.0),
                    release_ms: r.vol_env.release_ms as f32,
                    key_to_hold: r.vol_env.key_to_hold as f32,
                    key_to_decay: r.vol_env.key_to_decay as f32,
                },
                mod_env: EnvSpec {
                    delay_ms: r.mod_env.delay_ms as f32,
                    attack_ms: r.mod_env.attack_ms as f32,
                    hold_ms: r.mod_env.hold_ms as f32,
                    decay_ms: r.mod_env.decay_ms as f32,
                    sustain: (r.mod_env.sustain_level as f32 / 1000.0).clamp(0.0, 1.0),
                    release_ms: r.mod_env.release_ms as f32,
                    key_to_hold: r.mod_env.key_to_hold as f32,
                    key_to_decay: r.mod_env.key_to_decay as f32,
                },
                mod_env_to_pitch: r.mod_env_to_pitch as f32,
                mod_env_to_filter: r.mod_env_to_filter as f32,
                lfo_mod: LfoSpec {
                    delay_ms: r.lfo_mod.delay_ms as f32,
                    freq_hz: (r.lfo_mod.freq_mHz as f32 / 1000.0).max(0.01),
                    to_pitch: r.lfo_mod.to_pitch_cb as f32,
                    to_filter: r.lfo_mod.to_filter_cb as f32,
                    to_volume_db: r.lfo_mod.to_volume_cb as f32 / 10.0,
                },
                lfo_vib: VibLfoSpec {
                    delay_ms: r.lfo_vib.delay_ms as f32,
                    freq_hz: (r.lfo_vib.freq_mHz as f32 / 1000.0).max(0.01),
                    to_pitch: r.lfo_vib.to_pitch_cb as f32,
                },
                cutoff_hz: r.filter.cutoff_hz as f32,
                res_db: r.filter.resonance_cb as f32 / 10.0,
            })
            .collect();
        Patch {
            samples,
            regions,
            modulators: project.modulators.clone(),
            custom_curves: Vec::new(),
        }
    }
}

// ───────────────────────────── synth core ─────────────────────────────

#[derive(Clone, Copy, PartialEq)]
enum Stage {
    Delay,
    Attack,
    Hold,
    Decay,
    Sustain,
    Release,
}

#[derive(Clone, Copy)]
struct EnvState {
    stage: Stage,
    level: f32,
    counter: f32,
    rel_step: f32,
}

impl EnvState {
    fn new() -> Self {
        Self {
            stage: Stage::Delay,
            level: 0.0,
            counter: 0.0,
            rel_step: 0.0,
        }
    }

    fn begin_release(&mut self, sr: f32, rel_ms: f32, fast: bool) {
        self.stage = Stage::Release;
        let actual_rel = if fast { 8.0 } else { rel_ms.max(5.0) };
        self.rel_step = self.level.max(1e-4) / (actual_rel * 0.001 * sr).max(8.0);
    }

    #[inline(always)]
    fn tick(&mut self, dly: f32, atk: f32, hold: f32, dec_step: f32, sus: f32) -> (f32, bool) {
        let mut active = true;
        match self.stage {
            Stage::Delay => {
                self.counter += 1.0;
                if self.counter >= dly {
                    self.stage = Stage::Attack;
                    self.counter = 0.0;
                }
            }
            Stage::Attack => {
                self.level += 1.0 / atk;
                if self.level >= 1.0 {
                    self.level = 1.0;
                    self.stage = Stage::Hold;
                    self.counter = 0.0;
                }
            }
            Stage::Hold => {
                self.counter += 1.0;
                if self.counter >= hold {
                    self.stage = Stage::Decay;
                }
            }
            Stage::Decay => {
                self.level -= dec_step;
                if self.level <= sus {
                    self.level = sus;
                    self.stage = Stage::Sustain;
                }
            }
            Stage::Sustain => {
                self.level += (sus - self.level) * 0.002;
            }
            Stage::Release => {
                self.level -= self.rel_step;
                if self.level <= 0.0 {
                    self.level = 0.0;
                    active = false;
                }
            }
        }
        (self.level, active)
    }
}

struct Voice {
    key: u8,
    midi: u8,
    vel: u8,
    region: usize,
    sample: usize,
    pcm: Arc<Vec<f32>>, // keeps the sample alive even if the patch no longer has it
    params: RegionParams, // last known region settings (used if the region disappears)
    info: SampleInfo,
    pos: f64,
    vel_gain: f32,
    vol_env: EnvState,
    mod_env: EnvState,
    mod_lfo_phase: f32,
    mod_lfo_samples: f32,
    vib_lfo_phase: f32,
    vib_lfo_samples: f32,
    released: bool,
    fast_release: bool,
    x1: f32,
    x2: f32,
    y1: f32,
    y2: f32,
    age: u64,
}

pub struct Synth {
    sr: f32,
    voices: Vec<Voice>,
    age: u64,
    reverb: Freeverb,
    chorus: StereoChorus,
    chorus_rev: StereoChorus,
    pub midi_controllers: [u8; 128],
    pub pitch_bend: i16,
    pub channel_pressure: u8,
    dry_l: Vec<f32>,
    dry_r: Vec<f32>,
    rev_l: Vec<f32>,
    rev_r: Vec<f32>,
    cho_l: Vec<f32>,
    cho_r: Vec<f32>,
    cho_rev_l: Vec<f32>,
    cho_rev_r: Vec<f32>,
}

impl Synth {
    pub fn new(sample_rate: f32) -> Self {
        let mut midi_controllers = [0u8; 128];
        midi_controllers[7] = 100; // CC7 Volume
        midi_controllers[10] = 64; // CC10 Pan Center
        midi_controllers[11] = 127; // CC11 Expression Full
        midi_controllers[91] = 40; // CC91 Reverb Send

        Self {
            sr: sample_rate,
            voices: Vec::with_capacity(MAX_VOICES + 8),
            age: 0,
            reverb: Freeverb::new(sample_rate),
            chorus: StereoChorus::new(sample_rate),
            chorus_rev: StereoChorus::new(sample_rate),
            midi_controllers,
            pitch_bend: 0,
            channel_pressure: 0,
            dry_l: vec![0.0; 4096],
            dry_r: vec![0.0; 4096],
            rev_l: vec![0.0; 4096],
            rev_r: vec![0.0; 4096],
            cho_l: vec![0.0; 4096],
            cho_r: vec![0.0; 4096],
            cho_rev_l: vec![0.0; 4096],
            cho_rev_r: vec![0.0; 4096],
        }
    }

    pub fn control_change(&mut self, cc: u8, val: u8) {
        self.midi_controllers[cc as usize & 0x7F] = val;
    }

    pub fn pitch_bend(&mut self, val: i16) {
        self.pitch_bend = val;
    }

    pub fn channel_pressure(&mut self, val: u8) {
        self.channel_pressure = val;
    }

    pub fn active_voices(&self) -> usize {
        self.voices.len()
    }

    pub fn note_on(&mut self, patch: &Patch, key: u8, midi: u8, vel: u8) {
        // Re-striking a key quickly releases the previous note instead of piling up.
        for v in self.voices.iter_mut() {
            if v.key == key && !v.released {
                v.begin_release(self.sr, true);
            }
        }
        for (ri, r) in patch.regions.iter().enumerate() {
            if key < r.key_lo || key > r.key_hi || vel < r.vel_lo || vel > r.vel_hi {
                continue;
            }
            let Some(info) = patch.samples.get(r.sample) else { continue };
            if info.pcm.len() < 2 {
                continue;
            }
            self.age += 1;
            let has_vel_atten_mod = patch.modulators.iter().any(|m| {
                m.flags & 1 != 0 &&
                m.dest_oper == SF_GEN_INITIAL_ATTENUATION &&
                (m.src_oper & 0x7F) == (SF_MOD_SRC_VELOCITY as u16) &&
                (m.src_oper & 0x80) == 0
            });
            let vel_gain = if has_vel_atten_mod {
                1.0
            } else {
                (vel as f32 / 127.0).clamp(0.1, 1.0)
            };

            self.voices.push(Voice {
                key,
                midi,
                vel,
                region: ri,
                sample: r.sample,
                pcm: info.pcm.clone(),
                params: *r,
                info: info.clone(),
                pos: 0.0,
                vel_gain,
                vol_env: EnvState::new(),
                mod_env: EnvState::new(),
                mod_lfo_phase: 0.0,
                mod_lfo_samples: 0.0,
                vib_lfo_phase: 0.0,
                vib_lfo_samples: 0.0,
                released: false,
                fast_release: false,
                x1: 0.0,
                x2: 0.0,
                y1: 0.0,
                y2: 0.0,
                age: self.age,
            });
        }
        // Voice stealing: drop the oldest already-released voice first, otherwise the oldest.
        while self.voices.len() > MAX_VOICES {
            let idx = self
                .voices
                .iter()
                .enumerate()
                .filter(|(_, v)| v.released)
                .min_by_key(|(_, v)| v.age)
                .or_else(|| self.voices.iter().enumerate().min_by_key(|(_, v)| v.age))
                .map(|(i, _)| i)
                .unwrap_or(0);
            self.voices.swap_remove(idx);
        }
    }

    pub fn note_off(&mut self, key: u8) {
        for v in self.voices.iter_mut() {
            if v.key == key && !v.released {
                v.begin_release(self.sr, false);
            }
        }
    }

    pub fn all_notes_off(&mut self) {
        for v in self.voices.iter_mut() {
            if !v.released {
                v.begin_release(self.sr, true);
            }
        }
    }

    fn ensure_scratch_len(&mut self, n: usize) {
        if self.dry_l.len() < n {
            self.dry_l.resize(n, 0.0);
            self.dry_r.resize(n, 0.0);
            self.rev_l.resize(n, 0.0);
            self.rev_r.resize(n, 0.0);
            self.cho_l.resize(n, 0.0);
            self.cho_r.resize(n, 0.0);
            self.cho_rev_l.resize(n, 0.0);
            self.cho_rev_r.resize(n, 0.0);
        }
    }

    /// Mix `l.len()` frames into `l`/`r` (they are overwritten) with equal-power panning, reverb, and chorus.
    pub fn render(&mut self, patch: &Patch, l: &mut [f32], r: &mut [f32]) {
        let n = l.len();
        self.ensure_scratch_len(n);
        self.dry_l[..n].fill(0.0);
        self.dry_r[..n].fill(0.0);
        self.rev_l[..n].fill(0.0);
        self.rev_r[..n].fill(0.0);
        self.cho_l[..n].fill(0.0);
        self.cho_r[..n].fill(0.0);
        self.cho_rev_l[..n].fill(0.0);
        self.cho_rev_r[..n].fill(0.0);

        let sr = self.sr;
        let dry_l = &mut self.dry_l[..n];
        let dry_r = &mut self.dry_r[..n];
        let rev_l = &mut self.rev_l[..n];
        let rev_r = &mut self.rev_r[..n];
        let cho_l = &mut self.cho_l[..n];
        let cho_r = &mut self.cho_r[..n];
        let cho_rev_l = &mut self.cho_rev_l[..n];
        let cho_rev_r = &mut self.cho_rev_r[..n];

        let cc = self.midi_controllers;
        let pb = self.pitch_bend;
        let cp = self.channel_pressure;

        self.voices.retain_mut(|v| {
            v.render(patch, sr, &cc, pb, cp, dry_l, dry_r, rev_l, rev_r, cho_l, cho_r, cho_rev_l, cho_rev_r)
        });

        // 1. Process chorus on cho_rev buffer and sum into reverb buffer
        for i in 0..n {
            let (crl, crr) = self.chorus_rev.process(self.cho_rev_l[i], self.cho_rev_r[i]);
            self.rev_l[i] += crl;
            self.rev_r[i] += crr;
        }

        // 2. Process chorus on pure cho buffer
        for i in 0..n {
            let (cl, cr) = self.chorus.process(self.cho_l[i], self.cho_r[i]);
            self.cho_l[i] = cl;
            self.cho_r[i] = cr;
        }

        // 3. Process reverb on rev buffer
        for i in 0..n {
            let (rl, rr) = self.reverb.process(self.rev_l[i], self.rev_r[i]);
            self.rev_l[i] = rl;
            self.rev_r[i] = rr;
        }

        // 4. Combine dry + chorus + reverb into final output
        for i in 0..n {
            l[i] = self.dry_l[i] + self.cho_l[i] + self.rev_l[i];
            r[i] = self.dry_r[i] + self.cho_r[i] + self.rev_r[i];
        }
    }
}

impl Voice {
    fn begin_release(&mut self, sr: f32, fast: bool) {
        self.released = true;
        self.fast_release = fast;
        self.vol_env.begin_release(sr, self.params.env.release_ms, fast);
        self.mod_env.begin_release(sr, self.params.mod_env.release_ms, fast);
    }

    /// Returns false when the voice has finished and can be dropped.
    #[allow(clippy::too_many_arguments)]
    fn render(
        &mut self,
        patch: &Patch,
        sr: f32,
        cc: &[u8; 128],
        pitch_wheel: i16,
        channel_pressure: u8,
        dry_l: &mut [f32],
        dry_r: &mut [f32],
        rev_l: &mut [f32],
        rev_r: &mut [f32],
        cho_l: &mut [f32],
        cho_r: &mut [f32],
        cho_rev_l: &mut [f32],
        cho_rev_r: &mut [f32],
    ) -> bool {
        // Pick up live edits from the current patch
        if let Some(reg) = patch.regions.get(self.region) {
            if reg.sample == self.sample {
                self.params = *reg;
            }
        }
        if let Some(si) = patch.samples.get(self.sample) {
            if Arc::ptr_eq(&si.pcm, &self.pcm) {
                self.info = si.clone();
            }
        }
        let p = self.params;
        let len = self.pcm.len();
        if len < 2 {
            return false;
        }

        // Base pitch cents
        let base_cents = (self.midi as f32 - self.info.root_key as f32) * (p.scale_tuning / 100.0) * 100.0
            + p.coarse * 100.0
            + p.fine
            + self.info.fine_cents;

        // Key tracking for Hold and Decay (Polyphone / SoundFont 2 spec relative to MIDI note 60)
        let key_diff = 60.0 - self.midi as f32;
        let vol_scale_hold = 2.0_f32.powf((p.env.key_to_hold / 1200.0) * key_diff);
        let vol_scale_decay = 2.0_f32.powf((p.env.key_to_decay / 1200.0) * key_diff);
        let vol_dly = p.env.delay_ms * 0.001 * sr;
        let vol_atk = (p.env.attack_ms * 0.001 * sr).max(8.0);
        let vol_hold = (p.env.hold_ms * vol_scale_hold * 0.001 * sr).max(0.0);
        let vol_dec = (p.env.decay_ms * vol_scale_decay * 0.001 * sr).max(8.0);
        let vol_sus = p.env.sustain;
        let vol_dec_step = (1.0 - vol_sus).max(0.0) / vol_dec;

        let mod_scale_hold = 2.0_f32.powf((p.mod_env.key_to_hold / 1200.0) * key_diff);
        let mod_scale_decay = 2.0_f32.powf((p.mod_env.key_to_decay / 1200.0) * key_diff);
        let mod_dly = p.mod_env.delay_ms * 0.001 * sr;
        let mod_atk = (p.mod_env.attack_ms * 0.001 * sr).max(8.0);
        let mod_hold = (p.mod_env.hold_ms * mod_scale_hold * 0.001 * sr).max(0.0);
        let mod_dec = (p.mod_env.decay_ms * mod_scale_decay * 0.001 * sr).max(8.0);
        let mod_sus = p.mod_env.sustain;
        let mod_dec_step = (1.0 - mod_sus).max(0.0) / mod_dec;

        let gain = p.gain * self.vel_gain;

        // Initial modulator evaluation
        let mut deltas = ModDeltas::compute(
            &patch.modulators,
            &patch.custom_curves,
            self.key,
            self.vel,
            self.mod_env.level,
            0.0,
            0.0,
            cc,
            pitch_wheel,
            channel_pressure,
        );

        let init_delta_filter = p.mod_env_to_filter * self.mod_env.level + deltas.filter_fc_cents;
        let init_eff_fc = (p.cutoff_hz * 2.0_f32.powf(init_delta_filter / 1200.0)).clamp(20.0, sr * 0.45);
        let init_eff_q = lowpass_q((p.res_db + deltas.filter_q_cb / 10.0).clamp(0.0, 24.0));
        let (mut b0, mut b1, mut b2, mut a1, mut a2) = biquad_lowpass(init_eff_fc, init_eff_q, sr);

        let loop_end = self.info.loop_end.min(len as f32) as f64;
        let loop_start = (self.info.loop_start as f64).min(loop_end);
        let loop_len = loop_end - loop_start;
        let loops = match p.loop_mode {
            0 => false,
            1 => true,
            _ => !self.released,
        } && loop_len >= 2.0;

        let mod_lfo_delay_samp = p.lfo_mod.delay_ms * 0.001 * sr;
        let vib_lfo_delay_samp = p.lfo_vib.delay_ms * 0.001 * sr;

        let n = dry_l.len();
        for i in 0..n {
            // 1. Advance Envelopes
            let (vol_level, vol_active) = self.vol_env.tick(vol_dly, vol_atk, vol_hold, vol_dec_step, vol_sus);
            if !vol_active {
                return false;
            }
            let (mod_level, _) = self.mod_env.tick(mod_dly, mod_atk, mod_hold, mod_dec_step, mod_sus);

            // 2. Advance LFOs
            self.mod_lfo_samples += 1.0;
            let mod_lfo_val = if self.mod_lfo_samples >= mod_lfo_delay_samp {
                let s = self.mod_lfo_phase.sin();
                self.mod_lfo_phase += 2.0 * std::f32::consts::PI * p.lfo_mod.freq_hz / sr;
                if self.mod_lfo_phase >= std::f32::consts::TAU {
                    self.mod_lfo_phase -= std::f32::consts::TAU;
                }
                s
            } else {
                0.0
            };

            self.vib_lfo_samples += 1.0;
            let vib_lfo_val = if self.vib_lfo_samples >= vib_lfo_delay_samp {
                let s = self.vib_lfo_phase.sin();
                self.vib_lfo_phase += 2.0 * std::f32::consts::PI * p.lfo_vib.freq_hz / sr;
                if self.vib_lfo_phase >= std::f32::consts::TAU {
                    self.vib_lfo_phase -= std::f32::consts::TAU;
                }
                s
            } else {
                0.0
            };

            // 3. Modulations (update every 8 samples)
            if (i & 7) == 0 {
                deltas = ModDeltas::compute(
                    &patch.modulators,
                    &patch.custom_curves,
                    self.key,
                    self.vel,
                    mod_level,
                    mod_lfo_val,
                    vib_lfo_val,
                    cc,
                    pitch_wheel,
                    channel_pressure,
                );

                let delta_filter = p.mod_env_to_filter * mod_level + p.lfo_mod.to_filter * mod_lfo_val + deltas.filter_fc_cents;
                let eff_fc = (p.cutoff_hz * 2.0_f32.powf(delta_filter / 1200.0)).clamp(20.0, sr * 0.45);
                let eff_q = lowpass_q((p.res_db + deltas.filter_q_cb / 10.0).clamp(0.0, 24.0));
                let (nb0, nb1, nb2, na1, na2) = biquad_lowpass(eff_fc, eff_q, sr);
                b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
            }

            // Pitch modulation
            let delta_pitch = p.mod_env_to_pitch * mod_level
                + p.lfo_mod.to_pitch * mod_lfo_val
                + p.lfo_vib.to_pitch * vib_lfo_val
                + deltas.pitch_cents;
            let step = 2.0_f64.powf((base_cents + delta_pitch) as f64 / 1200.0) * (self.info.rate as f64 / sr as f64);

            // Volume modulation & dynamic attenuation from modulators (safely clamped to avoid mute / NaN)
            let eff_atten_db = deltas.atten_db.clamp(-96.0, 96.0);
            let atten_linear = if eff_atten_db.abs() > 0.001 {
                10.0_f32.powf(-eff_atten_db / 20.0)
            } else {
                1.0
            };
            let eff_gain = gain * atten_linear;

            let tremolo_total = (p.lfo_mod.to_volume_db * mod_lfo_val) + deltas.tremolo_db;
            let eff_tremolo_db = tremolo_total.clamp(-48.0, 48.0);
            let vol_mod = if eff_tremolo_db.abs() > 0.001 {
                10.0_f32.powf(-eff_tremolo_db / 20.0)
            } else {
                1.0
            };

            // Dynamic Send & Panning modulation
            let eff_rev = (p.reverb_send + deltas.reverb_send).clamp(0.0, 1.0);
            let eff_cho = (p.chorus_send + deltas.chorus_send).clamp(0.0, 1.0);
            let eff_pan = (p.pan + deltas.pan_offset).clamp(-1.0, 1.0);

            let pan_angle = (eff_pan + 1.0) * std::f32::consts::FRAC_PI_4;
            let (pan_r, pan_l) = pan_angle.sin_cos();

            let non_rev = 1.0 - eff_rev;
            let non_cho = 1.0 - eff_cho;

            let cur_dry_l = pan_l * non_rev * non_cho;
            let cur_dry_r = pan_r * non_rev * non_cho;
            let cur_cho_l = pan_l * non_rev * eff_cho;
            let cur_cho_r = pan_r * non_rev * eff_cho;
            let cur_rev_l = pan_l * eff_rev * non_cho;
            let cur_rev_r = pan_r * eff_rev * non_cho;
            let cur_cho_rev_l = pan_l * eff_rev * eff_cho;
            let cur_cho_rev_r = pan_r * eff_rev * eff_cho;

            // 4. Sample fetch (linear interpolation, wraps inside the loop)
            let idx = self.pos as usize;
            if idx >= len {
                return false;
            }
            let frac = (self.pos - idx as f64) as f32;
            let idx1 = if loops && (idx + 1) as f64 >= loop_end { loop_start as usize } else { (idx + 1).min(len - 1) };
            let s0 = self.pcm[idx];
            let s1 = self.pcm[idx1.min(len - 1)];
            let x0 = s0 + frac * (s1 - s0);

            // 5. Low-pass biquad
            let y0 = b0 * x0 + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2;
            self.x2 = self.x1;
            self.x1 = x0;
            self.y2 = self.y1;
            self.y1 = if y0.abs() < 1e-18 { 0.0 } else { y0 };

            let o = y0 * vol_level * eff_gain * vol_mod;

            // 6. Route to dry, chorus, and reverb buffers
            dry_l[i] += o * cur_dry_l;
            dry_r[i] += o * cur_dry_r;
            cho_l[i] += o * cur_cho_l;
            cho_r[i] += o * cur_cho_r;
            rev_l[i] += o * cur_rev_l;
            rev_r[i] += o * cur_rev_r;
            cho_rev_l[i] += o * cur_cho_rev_l;
            cho_rev_r[i] += o * cur_cho_rev_r;

            self.pos += step;
            if loops && self.pos >= loop_end {
                self.pos = loop_start + (self.pos - loop_start) % loop_len;
            } else if self.pos >= len as f64 {
                return false;
            }
        }
        true
    }
}

// ───────────────────────────── device output ─────────────────────────────

enum Cmd {
    NoteOn { key: u8, midi: u8, vel: u8 },
    NoteOff { key: u8 },
    AllOff,
    ControlChange { cc: u8, val: u8 },
    PitchBend { val: i16 },
    ChannelPressure { val: u8 },
    SetPatch(Arc<Patch>),
}

pub struct AudioPlayer {
    tx: Option<SyncSender<Cmd>>,
    patch: Mutex<Arc<Patch>>, // GUI-side copy (used for export and for re-sending after a restart)
    master: Arc<AtomicU32>,   // f32 bits, 0..1
    peak: Arc<AtomicU32>,     // f32 bits, max since last read
    voices: Arc<AtomicU32>,   // currently sounding voices
    ok: Arc<AtomicBool>,
    host_rate: u32,
    _stream: Option<cpal::Stream>,
}

impl AudioPlayer {
    pub fn new() -> Self {
        let mut p = Self {
            tx: None,
            patch: Mutex::new(Arc::new(Patch::default())),
            master: Arc::new(AtomicU32::new(0.8_f32.to_bits())),
            peak: Arc::new(AtomicU32::new(0)),
            voices: Arc::new(AtomicU32::new(0)),
            ok: Arc::new(AtomicBool::new(false)),
            host_rate: 44100,
            _stream: None,
        };
        p.start_stream();
        p
    }

    fn start_stream(&mut self) {
        let (tx, rx) = sync_channel::<Cmd>(4096);
        self.ok.store(false, Ordering::Relaxed);
        match Self::build_stream(rx, self.master.clone(), self.peak.clone(), self.voices.clone(), self.ok.clone()) {
            Some((stream, rate)) => {
                self.host_rate = rate;
                self._stream = Some(stream);
                self.tx = Some(tx);
                self.ok.store(true, Ordering::Relaxed);
                let patch = self.patch.lock().map(|p| p.clone()).unwrap_or_default();
                self.send(Cmd::SetPatch(patch));
            }
            None => {
                self._stream = None;
                self.tx = None;
            }
        }
    }

    /// Re-open the default output device (after it was unplugged / the stream errored).
    pub fn restart(&mut self) {
        self._stream = None;
        self.start_stream();
    }

    fn build_stream(rx: Receiver<Cmd>, master: Arc<AtomicU32>, peak: Arc<AtomicU32>, voices: Arc<AtomicU32>, ok: Arc<AtomicBool>) -> Option<(cpal::Stream, u32)> {
        let host = cpal::default_host();
        let device = host.default_output_device()?;
        let supported = device.default_output_config().ok()?;
        let rate = supported.sample_rate().0;
        let channels = supported.channels() as usize;
        let format = supported.sample_format();
        let config: cpal::StreamConfig = supported.into();
        let stream = match format {
            cpal::SampleFormat::F32 => Self::make_stream::<f32>(&device, &config, channels, rate, rx, master, peak, voices, ok)?,
            cpal::SampleFormat::I16 => Self::make_stream::<i16>(&device, &config, channels, rate, rx, master, peak, voices, ok)?,
            cpal::SampleFormat::U16 => Self::make_stream::<u16>(&device, &config, channels, rate, rx, master, peak, voices, ok)?,
            _ => return None,
        };
        Some((stream, rate))
    }

    #[allow(clippy::too_many_arguments)]
    fn make_stream<T>(
        device: &cpal::Device,
        config: &cpal::StreamConfig,
        channels: usize,
        rate: u32,
        rx: Receiver<Cmd>,
        master: Arc<AtomicU32>,
        peak: Arc<AtomicU32>,
        voices: Arc<AtomicU32>,
        ok: Arc<AtomicBool>,
    ) -> Option<cpal::Stream>
    where
        T: cpal::SizedSample + cpal::FromSample<f32>,
    {
        let mut synth = Synth::new(rate as f32);
        let mut patch: Arc<Patch> = Arc::new(Patch::default());
        let mut l = vec![0.0_f32; 4096];
        let mut r = vec![0.0_f32; 4096];
        let ok_err = ok.clone();
        let stream = device
            .build_output_stream(
                config,
                move |data: &mut [T], _: &cpal::OutputCallbackInfo| {
                    while let Ok(cmd) = rx.try_recv() {
                        match cmd {
                            Cmd::NoteOn { key, midi, vel } => synth.note_on(&patch, key, midi, vel),
                            Cmd::NoteOff { key } => synth.note_off(key),
                            Cmd::AllOff => synth.all_notes_off(),
                            Cmd::ControlChange { cc, val } => synth.control_change(cc, val),
                            Cmd::PitchBend { val } => synth.pitch_bend(val),
                            Cmd::ChannelPressure { val } => synth.channel_pressure(val),
                            Cmd::SetPatch(p) => patch = p,
                        }
                    }
                    let ch = channels.max(1);
                    let frames = data.len() / ch;
                    if l.len() < frames {
                        l.resize(frames, 0.0);
                        r.resize(frames, 0.0);
                    }
                    synth.render(&patch, &mut l[..frames], &mut r[..frames]);
                    let g = f32::from_bits(master.load(Ordering::Relaxed));
                    let mut pk = 0.0_f32;
                    for f in 0..frames {
                        let a = (l[f] * g).tanh();
                        let b = (r[f] * g).tanh();
                        pk = pk.max(a.abs()).max(b.abs());
                        let frame = &mut data[f * ch..f * ch + ch];
                        if ch == 1 {
                            frame[0] = T::from_sample((a + b) * 0.5);
                        } else {
                            frame[0] = T::from_sample(a);
                            frame[1] = T::from_sample(b);
                            for extra in frame.iter_mut().skip(2) {
                                *extra = T::from_sample(0.0_f32);
                            }
                        }
                    }
                    peak.fetch_max(pk.to_bits(), Ordering::Relaxed);
                    voices.store(synth.active_voices() as u32, Ordering::Relaxed);
                },
                move |err| {
                    eprintln!("Audio stream error: {}", err);
                    ok_err.store(false, Ordering::Relaxed);
                },
                None,
            )
            .ok()?;
        stream.play().ok()?;
        Some(stream)
    }

    fn send(&self, cmd: Cmd) {
        if let Some(tx) = &self.tx {
            let _ = tx.try_send(cmd); // full queue = audio thread gone; never block the GUI
        }
    }

    /// Trigger a note: `key` 0..60 (physical key), `midi_note` 0..127.
    pub fn note_on(&self, key: u8, midi_note: u8, velocity: u8) {
        self.send(Cmd::NoteOn { key, midi: midi_note, vel: velocity });
    }

    pub fn note_off(&self, key: u8) {
        self.send(Cmd::NoteOff { key });
    }

    pub fn all_notes_off(&self) {
        self.send(Cmd::AllOff);
    }

    pub fn control_change(&self, cc: u8, val: u8) {
        self.send(Cmd::ControlChange { cc, val });
    }

    pub fn pitch_bend(&self, val: i16) {
        self.send(Cmd::PitchBend { val });
    }

    pub fn channel_pressure(&self, val: u8) {
        self.send(Cmd::ChannelPressure { val });
    }

    /// Replace the whole sound set (samples + regions). Cheap: the GUI builds the Patch, the audio thread just swaps it in.
    pub fn set_patch(&self, patch: Patch) {
        let arc = Arc::new(patch);
        if let Ok(mut g) = self.patch.lock() {
            *g = arc.clone();
        }
        self.send(Cmd::SetPatch(arc));
    }

    /// 0..1
    pub fn set_master_volume(&self, v: f32) {
        self.master.store(v.clamp(0.0, 1.0).to_bits(), Ordering::Relaxed);
    }

    pub fn master_volume(&self) -> f32 {
        f32::from_bits(self.master.load(Ordering::Relaxed))
    }

    /// Peak output level since the previous call (0..1+). Drives the header meter.
    pub fn take_peak(&self) -> f32 {
        f32::from_bits(self.peak.swap(0, Ordering::Relaxed))
    }

    pub fn voice_count(&self) -> u32 {
        self.voices.load(Ordering::Relaxed)
    }

    pub fn host_rate(&self) -> u32 {
        self.host_rate
    }

    pub fn has_device(&self) -> bool {
        self._stream.is_some()
    }

    /// false when there is no device or the stream reported an error.
    pub fn is_ok(&self) -> bool {
        self._stream.is_some() && self.ok.load(Ordering::Relaxed)
    }

    pub fn patch_snapshot(&self) -> Arc<Patch> {
        self.patch.lock().map(|p| p.clone()).unwrap_or_default()
    }

    /// Offline render of a recording to a 16-bit 44.1 kHz stereo WAV (RIFF) buffer.
    pub fn render_vhr_to_wav(&self, recording: &VhrRecording) -> Vec<u8> {
        render_vhr_to_wav(recording, &self.patch_snapshot(), self.master_volume())
    }
}

/// Sample-accurate offline render using exactly the same synth as live playback.
pub fn render_vhr_to_wav(recording: &VhrRecording, patch: &Patch, master: f32) -> Vec<u8> {
    const SR: u32 = 44100;
    const CHUNK: usize = 512;
    let mut synth = Synth::new(SR as f32);
    let mut pcm: Vec<i16> = Vec::new();
    let (mut l, mut r) = (vec![0.0_f32; CHUNK], vec![0.0_f32; CHUNK]);
    let mut pos: u64 = 0;
    let mut abs_us: u64 = 0;

    let mut render_to = |synth: &mut Synth, pcm: &mut Vec<i16>, pos: &mut u64, target: u64| {
        while *pos < target {
            let n = ((target - *pos) as usize).min(CHUNK);
            synth.render(patch, &mut l[..n], &mut r[..n]);
            for i in 0..n {
                pcm.push(((l[i] * master).tanh() * 32767.0) as i16);
                pcm.push(((r[i] * master).tanh() * 32767.0) as i16);
            }
            *pos += n as u64;
        }
    };

    for ev in &recording.events {
        abs_us += ev.delta_us as u64;
        let target = abs_us * SR as u64 / 1_000_000;
        render_to(&mut synth, &mut pcm, &mut pos, target);
        match ev.event_type {
            VHR_EVENT_NOTE_ON => {
                let key = (ev.data1 as i32 - 36).clamp(0, 60) as u8;
                synth.note_on(patch, key, ev.data1, ev.data2.max(1));
            }
            VHR_EVENT_NOTE_OFF => synth.note_off((ev.data1 as i32 - 36).clamp(0, 60) as u8),
            _ => {}
        }
    }
    // Release tail: let everything ring out (at most 4 s).
    synth.all_notes_off();
    let tail_end = pos + 4 * SR as u64;
    while synth.active_voices() > 0 && pos < tail_end {
        let target = pos + CHUNK as u64;
        render_to(&mut synth, &mut pcm, &mut pos, target);
    }

    let channels = 2u16;
    let data_size = (pcm.len() * 2) as u32;
    let mut wav = Vec::with_capacity(44 + pcm.len() * 2);
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36 + data_size).to_le_bytes());
    wav.extend_from_slice(b"WAVEfmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes());
    wav.extend_from_slice(&channels.to_le_bytes());
    wav.extend_from_slice(&SR.to_le_bytes());
    wav.extend_from_slice(&(SR * channels as u32 * 2).to_le_bytes());
    wav.extend_from_slice(&(channels * 2).to_le_bytes());
    wav.extend_from_slice(&16u16.to_le_bytes());
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&data_size.to_le_bytes());
    for s in pcm {
        wav.extend_from_slice(&s.to_le_bytes());
    }
    wav
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sine_patch(n: usize) -> Patch {
        let pcm: Vec<f32> = (0..n).map(|i| (i as f32 / n as f32 * std::f32::consts::TAU).sin()).collect();
        let info = SampleInfo { pcm: Arc::new(pcm), rate: 44100.0, root_key: 48, fine_cents: 0.0, loop_start: 0.0, loop_end: n as f32 };
        let env = EnvSpec { delay_ms: 0.0, attack_ms: 5.0, hold_ms: 0.0, decay_ms: 100.0, sustain: 0.8, release_ms: 50.0, key_to_hold: 0.0, key_to_decay: 0.0 };
        let mod_env = EnvSpec { delay_ms: 0.0, attack_ms: 5.0, hold_ms: 0.0, decay_ms: 100.0, sustain: 0.0, release_ms: 50.0, key_to_hold: 0.0, key_to_decay: 0.0 };
        let lfo_mod = LfoSpec { delay_ms: 0.0, freq_hz: 5.0, to_pitch: 0.0, to_filter: 0.0, to_volume_db: 0.0 };
        let lfo_vib = VibLfoSpec { delay_ms: 0.0, freq_hz: 6.0, to_pitch: 0.0 };
        let reg = RegionParams {
            key_lo: 0,
            key_hi: 60,
            vel_lo: 0,
            vel_hi: 127,
            sample: 0,
            loop_mode: 1,
            coarse: 0.0,
            fine: 0.0,
            scale_tuning: 100.0,
            gain: 1.0,
            pan: 0.0,
            reverb_send: 0.0,
            chorus_send: 0.0,
            env,
            mod_env,
            mod_env_to_pitch: 0.0,
            mod_env_to_filter: 0.0,
            lfo_mod,
            lfo_vib,
            cutoff_hz: 8000.0,
            res_db: 0.0,
        };
        Patch { samples: vec![info], regions: vec![reg], modulators: Vec::new(), custom_curves: Vec::new() }
    }

    #[test]
    fn swapping_to_a_shorter_sample_while_playing_does_not_panic() {
        let mut synth = Synth::new(44100.0);
        let long = sine_patch(4000);
        synth.note_on(&long, 12, 48, 100);
        let (mut l, mut r) = (vec![0.0; 256], vec![0.0; 256]);
        for _ in 0..40 {
            synth.render(&long, &mut l, &mut r);
        }
        let tiny = sine_patch(8); // the old engine indexed past the end here and died
        for _ in 0..40 {
            synth.render(&tiny, &mut l, &mut r);
        }
        let empty = Patch::default();
        for _ in 0..40 {
            synth.render(&empty, &mut l, &mut r);
        }
        assert!(l.iter().all(|v| v.is_finite()));
    }

    #[test]
    fn note_off_releases_to_silence() {
        let mut synth = Synth::new(44100.0);
        let p = sine_patch(441);
        synth.note_on(&p, 12, 48, 100);
        let (mut l, mut r) = (vec![0.0; 512], vec![0.0; 512]);
        synth.render(&p, &mut l, &mut r);
        assert!(l.iter().any(|v| v.abs() > 0.01));
        synth.note_off(12);
        for _ in 0..20 {
            synth.render(&p, &mut l, &mut r);
        }
        assert_eq!(synth.active_voices(), 0);
    }

    #[test]
    fn each_region_plays_its_own_sample() {
        let mut p = sine_patch(441);
        let mut second = p.samples[0].clone();
        second.pcm = Arc::new(vec![0.0; 441]); // silent sample
        p.samples.push(second);
        let mut r2 = p.regions[0];
        r2.key_lo = 30;
        r2.key_hi = 60;
        r2.sample = 1;
        p.regions[0].key_hi = 29;
        p.regions.push(r2);
        let mut synth = Synth::new(44100.0);
        let (mut l, mut r) = (vec![0.0; 1024], vec![0.0; 1024]);
        synth.note_on(&p, 10, 46, 100);
        synth.render(&p, &mut l, &mut r);
        let low_peak = l.iter().fold(0.0_f32, |a, v| a.max(v.abs()));
        let mut synth = Synth::new(44100.0);
        synth.note_on(&p, 40, 76, 100);
        synth.render(&p, &mut l, &mut r);
        let high_peak = l.iter().fold(0.0_f32, |a, v| a.max(v.abs()));
        assert!(low_peak > 0.1 && high_peak < 1e-6);
    }

    #[test]
    fn filter_plot_matches_engine_at_cutoff() {
        // Butterworth: -3 dB at the cutoff with 0 dB resonance.
        let db = lowpass_mag_db(1000.0, 1000.0, 0.0, 48000.0);
        assert!((db + 3.0).abs() < 0.3, "{}", db);
    }

    #[test]
    fn equal_power_panning_works() {
        let mut p = sine_patch(441);
        p.regions[0].pan = -1.0; // Hard left
        let mut synth = Synth::new(44100.0);
        let (mut l, mut r) = (vec![0.0; 512], vec![0.0; 512]);
        synth.note_on(&p, 12, 48, 100);
        synth.render(&p, &mut l, &mut r);
        let l_energy: f32 = l.iter().map(|v| v * v).sum();
        let r_energy: f32 = r.iter().map(|v| v * v).sum();
        assert!(l_energy > 1.0 && r_energy < 1e-4, "Left should have all energy when pan = -1.0");

        // Hard right
        p.regions[0].pan = 1.0;
        let mut synth = Synth::new(44100.0);
        synth.note_on(&p, 12, 48, 100);
        synth.render(&p, &mut l, &mut r);
        let l_energy: f32 = l.iter().map(|v| v * v).sum();
        let r_energy: f32 = r.iter().map(|v| v * v).sum();
        assert!(r_energy > 1.0 && l_energy < 1e-4, "Right should have all energy when pan = +1.0");
    }

    #[test]
    fn reverb_tail_sustains_sound() {
        let mut p = sine_patch(441);
        p.regions[0].reverb_send = 0.8;
        let mut synth = Synth::new(44100.0);
        let (mut l, mut r) = (vec![0.0; 512], vec![0.0; 512]);
        synth.note_on(&p, 12, 48, 100);
        synth.render(&p, &mut l, &mut r);
        synth.note_off(12);
        // Render 10 blocks: voice releases quickly, but reverb tail rings out
        for _ in 0..10 {
            synth.render(&p, &mut l, &mut r);
        }
        let tail_energy: f32 = l.iter().map(|v| v * v).sum::<f32>() + r.iter().map(|v| v * v).sum::<f32>();
        assert!(tail_energy > 1e-5, "Reverb tail should continue ringing");
    }

    #[test]
    fn test_velocity_to_attenuation_modulator() {
        let mut p = sine_patch(441);
        // Add SF2 default: Velocity -> Attenuation (Concave, Descending, 960 cb = 96 dB)
        p.modulators.push(VHLModulator_v2 {
            src_oper: VHLModulator_v2::pack_source(SF_MOD_SRC_VELOCITY as u8, false, SF_CURVE_CONCAVE, true, false),
            dest_oper: SF_GEN_INITIAL_ATTENUATION,
            amt_src_oper: 0,
            amount: 960,
            transform: SF_TRANSFORM_LINEAR,
            flags: 1,
            index: 1,
        });

        // 1. Strike hard (velocity 127)
        let mut synth1 = Synth::new(44100.0);
        let (mut l1, mut r1) = (vec![0.0; 512], vec![0.0; 512]);
        synth1.note_on(&p, 12, 48, 127);
        synth1.render(&p, &mut l1, &mut r1);
        let max_127 = l1.iter().fold(0.0_f32, |m, &x| m.max(x.abs()));

        // 2. Strike soft (velocity 30)
        let mut synth2 = Synth::new(44100.0);
        let (mut l2, mut r2) = (vec![0.0; 512], vec![0.0; 512]);
        synth2.note_on(&p, 12, 48, 30);
        synth2.render(&p, &mut l2, &mut r2);
        let max_30 = l2.iter().fold(0.0_f32, |m, &x| m.max(x.abs()));

        assert!(max_127 > 0.5, "Velocity 127 should be loud, got {}", max_127);
        assert!(max_30 < max_127 * 0.35, "Velocity 30 should be substantially quieter than 127 (got {} vs {})", max_30, max_127);
    }

    #[test]
    fn test_mod_env_to_reverb_modulator() {
        let mut p = sine_patch(441);
        p.regions[0].reverb_send = 0.0; // Start completely dry
        p.regions[0].mod_env.attack_ms = 40.0; // Mod env attacks over ~40ms
        p.regions[0].mod_env.sustain = 1.0;

        // Add Mod Env -> Reverb Send modulator (amount = 800 = 80%)
        p.modulators.push(VHLModulator_v2 {
            src_oper: VHLModulator_v2::pack_source(SF_MOD_SRC_MOD_ENV as u8, false, SF_CURVE_LINEAR, false, false),
            dest_oper: SF_GEN_REVERB_EFFECTS_SEND,
            amt_src_oper: 0,
            amount: 800,
            transform: SF_TRANSFORM_LINEAR,
            flags: 1,
            index: 1,
        });

        let mut synth = Synth::new(44100.0);
        let (mut l, mut r) = (vec![0.0; 256], vec![0.0; 256]);
        synth.note_on(&p, 12, 48, 100);

        // First 256 samples: envelope just starting, reverb send is nearly zero
        synth.render(&p, &mut l, &mut r);
        let early_rev_energy: f32 = synth.rev_l[..256].iter().map(|v| v * v).sum();

        // Render 10 blocks (~60 ms): envelope reaches 1.0 and opens up 80% reverb!
        for _ in 0..10 {
            synth.render(&p, &mut l, &mut r);
        }
        let late_rev_energy: f32 = synth.rev_l[..256].iter().map(|v| v * v).sum();

        assert!(late_rev_energy > early_rev_energy * 3.0, "Reverb energy must grow as modulation envelope attacks over time");
    }
}