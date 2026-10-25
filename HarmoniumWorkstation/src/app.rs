use crate::audio_engine::{mono_pcm, AudioPlayer, Patch};
use crate::icons;
use crate::project::HarmoniumProject;
use crate::theme::{AppTheme, ThemeMode};
use crate::vhl::compiler::{VhlCompiler, VhlProject, VhlSample};
#[allow(unused_imports)]
use crate::vhl::decompiler::VhlDecompiler;
use crate::vhl::types::*;
use crate::vhr::compiler::VhrCompiler;
use crate::vhr::decompiler::VhrDecompiler;
use crate::vhr::midi_converter::MidiConverter;
use crate::vhr::player::{VhrPlayer, VhrPlayerState, VhrRecorder, VhrTickEvent};
#[allow(unused_imports)]
use crate::vhr::types::*;
use crate::visuals::{KeyZones, RegionBar, VisualPainters, WaterNote, WaveformCache, KEY_COUNT, WATERFALL_TRAIL_SECS};
use crate::widgets::{self, caps, mono, TransportKind};
use eframe::egui;
use egui::{Align, Align2, Color32, CursorIcon, FontId, Frame, Layout, Margin, Pos2, Rect, RichText, Rounding, Sense, Stroke, Ui, Vec2};
use std::collections::HashMap;
use std::sync::Arc;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AppView {
    Landing,
    SoundEditor,
    ModulationEditor,
    #[allow(dead_code)]
    LivePlayer,
}

pub struct HarmoniumApp {
    // Active View Mode
    pub current_view: AppView,
    pub modulator_view: crate::modulator_editor::ModulatorEditorView,

    // Active VHL project & session
    pub project_name: String,
    pub project_path: Option<std::path::PathBuf>,
    pub active_sample_idx: usize,
    pub active_region_idx: usize,
    pub project: VhlProject,
    pub last_compiled_vhl: Option<Vec<u8>>,

    // Theme
    pub theme_mode: ThemeMode,
    applied_mode: Option<ThemeMode>, // last mode pushed into egui (apply only on change)

    // Panel visibility
    pub show_left_sidebar: bool,
    pub show_right_inspector: bool,
    pub show_note_waterfall: bool,
    pub show_note_learner_modal: bool,

    // Asset filter
    pub asset_search_query: String,

    // Performance / keyboard state
    pub master_volume: f32,
    pub tempo_bpm: u32,
    pub transpose_semitones: i32,
    pub split_key_index: u8, // key 0..60 where the left zone ends
    pub last_clicked_key: Option<u8>,
    held_midi: [Option<u8>; KEY_COUNT], // MIDI note each key started with (None = key up)
    laptop_notes: HashMap<egui::Key, u8>, // laptop key -> piano key it started
    mouse_key: Option<u8>,

    // Envelope (DAHDSR) + filter of the *selected region* (the DSP rack edits these, then writes them back)
    pub vol_delay_ms: f32,
    pub attack_ms: f32,
    pub hold_ms: f32,
    pub decay_ms: f32,
    pub sustain_percent: f32,
    pub release_ms: f32,
    pub cutoff_hz: f32,
    pub resonance_db: f32,
    pub adsr_layer: usize, // 0 = Volume, 1 = Modulation
    pub mod_delay_ms: f32,
    pub mod_attack_ms: f32,
    pub mod_hold_ms: f32,
    pub mod_decay_ms: f32,
    pub mod_sustain_percent: f32,
    pub mod_release_ms: f32,

    // Network / ESP32
    #[allow(dead_code)]
    pub esp32_ip: String,
    pub is_connected_wifi: bool,

    // Build log
    pub console_logs: Vec<String>,

    // Audio engine
    pub audio_player: AudioPlayer,
    engine_pcm: Vec<Arc<Vec<f32>>>, // mono float copy of every sample, rebuilt only when sample data changes
    pcm_dirty: bool,
    patch_dirty: bool, // something changed that the audio engine must hear: rebuilt once at the end of the frame
    peak_meter: f32,

    // VHR recorder / replayer
    pub vhr_player: VhrPlayer,
    pub vhr_recorder: VhrRecorder,
    pub last_frame_instant: std::time::Instant,
    prev_player_state: VhrPlayerState,
    #[allow(dead_code)]
    pub last_compiled_vhr: Option<Vec<u8>>,

    // Visual caches / UI state
    wave_cache: WaveformCache,
    wave_gen: u64, // bump whenever the sample data is replaced
    waterfall: Vec<WaterNote>,
    pending_delete_region: Option<usize>, // first click on "delete" arms it, second click deletes
    first_frame: bool,
}

// ═══════════════════════════════════════════════════════════════════════════
// Default project
// ═══════════════════════════════════════════════════════════════════════════
fn name_array(s: &str) -> [u8; VHL_NAME_LEN] {
    let mut a = [0u8; VHL_NAME_LEN];
    let b = s.as_bytes();
    let n = b.len().min(VHL_NAME_LEN - 1);
    a[..n].copy_from_slice(&b[..n]);
    a
}

fn instrument_name(inst: &VHLInstrumentEntry_v2) -> String {
    String::from_utf8_lossy(&inst.name).trim_matches('\0').to_string()
}

fn make_region(key_lo: u8, key_hi: u8, loop_mode: u8, pan: i16, reverb: u8, chorus: u8, env: VHLEnvelope) -> VHLRegion_v2 {
    VHLRegion_v2 {
        key_lo,
        key_hi,
        vel_lo: 0,
        vel_hi: 127,
        sample_id: 0,
        exclusive_class: 0,
        loop_mode,
        flags: 0,
        _reserved_a: 0,
        coarse_tune: 0,
        _pad_tune: 0,
        fine_tune: 0,
        scale_tuning: 100,
        _reserved_b: 0,
        attenuation_cb: 0,
        pan,
        reverb_send: reverb,
        chorus_send: chorus,
        vol_env: env,
        mod_env: env,
        mod_env_to_pitch: 0,
        mod_env_to_filter: 0,
        lfo_mod: VHLModLFO { delay_ms: 0, freq_mHz: 5000, to_pitch_cb: 0, to_filter_cb: 0, to_volume_cb: 0, _reserved: 0 },
        lfo_vib: VHLVibLFO { delay_ms: 0, freq_mHz: 6000, to_pitch_cb: 0, _reserved: 0 },
        filter: VHLFilter { cutoff_hz: 4800, resonance_cb: 25 },
        _reserved_c: 0,
    }
}

fn default_project() -> VhlProject {
    let mut project = VhlProject::default();
    project.build_tag = "HarmoniumStudio V2\0".to_string();

    for (id, name, poly, vol, pan, rev, cho, rgn_off) in [(0u8, "Piano", 6u8, 95u8, -25i8, 20u8, 0u8, 0u16), (1u8, "Harmonium", 8u8, 90u8, 25i8, 35u8, 15u8, 1u16)] {
        project.instruments.push(VHLInstrumentEntry_v2 {
            instrument_id: id,
            bank: 0,
            program: id,
            max_polyphony: poly,
            name: name_array(name),
            region_offset: rgn_off,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: vol,
            pan_default: pan,
            reverb_send_pct: rev,
            chorus_send_pct: cho,
            flags: 0,
            _reserved: [0u8; 7],
        });
    }

    let piano_env = VHLEnvelope { delay_ms: 0, attack_ms: 5, hold_ms: 0, decay_ms: 800, sustain_level: 200, release_ms: 60, key_to_hold: 0, key_to_decay: 0 };
    let harm_env = VHLEnvelope { delay_ms: 0, attack_ms: 25, hold_ms: 10, decay_ms: 150, sustain_level: 900, release_ms: 80, key_to_hold: 0, key_to_decay: 0 };
    // Both regions loop: the built-in sample is a single waveform cycle, so without a loop the piano region
    // would only produce a few-millisecond click.
    project.regions.push(make_region(0, 24, VHL_LOOP_CONTINUOUS, -250, 20, 0, piano_env));
    project.regions.push(make_region(25, 60, VHL_LOOP_CONTINUOUS, 250, 35, 15, harm_env));
    project.region_names = vec!["Piano Bass".to_string(), "Harmonium Lead".to_string()];

    // Built-in reed sample: exactly one cycle of a 130.86 Hz tone (44100 / 337), so root key C3 (48) is in tune.
    let period = 337usize;
    let reed: Vec<i16> = (0..period)
        .map(|i| {
            let ph = i as f32 / period as f32 * std::f32::consts::TAU;
            ((ph.sin() * 0.5 + (ph * 2.0).sin() * 0.25 + (ph * 3.0).sin() * 0.15 + (ph * 4.0).sin() * 0.08) * 26000.0) as i16
        })
        .collect();
    project.samples.push(VhlSample {
        name: "Harmonium_C3_Reed".to_string(),
        root_key: 48,
        sample_rate: 44100,
        fine_tune_cents: 0,
        loop_start_frame: 0,
        loop_end_frame: period as u32,
        channels: 1,
        pcm_frames: reed,
    });
    project
}

impl Default for HarmoniumApp {
    fn default() -> Self {
        let mut app = Self {
            current_view: AppView::Landing,
            modulator_view: crate::modulator_editor::ModulatorEditorView::default(),
            project_name: "HarmoniumStudio V2".to_string(),
            project_path: None,
            active_sample_idx: 0,
            active_region_idx: 0,
            project: default_project(),
            last_compiled_vhl: None,
            theme_mode: ThemeMode::Dark,
            applied_mode: None,

            show_left_sidebar: true,
            show_right_inspector: true,
            show_note_waterfall: true,
            show_note_learner_modal: false,

            asset_search_query: String::new(),

            master_volume: 80.0,
            tempo_bpm: 120,
            transpose_semitones: 0,
            split_key_index: 24,
            last_clicked_key: None,
            held_midi: [None; KEY_COUNT],
            laptop_notes: HashMap::new(),
            mouse_key: None,

            vol_delay_ms: 0.0,
            attack_ms: 25.0,
            hold_ms: 10.0,
            decay_ms: 150.0,
            sustain_percent: 90.0,
            release_ms: 80.0,
            cutoff_hz: 4800.0,
            resonance_db: 2.5,
            adsr_layer: 0,
            mod_delay_ms: 0.0,
            mod_attack_ms: 50.0,
            mod_hold_ms: 10.0,
            mod_decay_ms: 200.0,
            mod_sustain_percent: 50.0,
            mod_release_ms: 100.0,

            esp32_ip: "192.168.4.1".to_string(),
            is_connected_wifi: false,

            console_logs: vec![
                "[SYSTEM] Harmonium Workstation Initialized.".to_string(),
                "[VHL v2.0] Default Project ready: 2 Instruments (Piano & Harmonium), 61 Keys mapped.".to_string(),
                "[VHR v2.0] Performance Recorder, Replayer & MIDI Studio ready.".to_string(),
            ],

            audio_player: AudioPlayer::new(),
            engine_pcm: Vec::new(),
            pcm_dirty: true,
            patch_dirty: true,
            peak_meter: 0.0,

            vhr_player: VhrPlayer::new(),
            vhr_recorder: VhrRecorder::new(),
            last_frame_instant: std::time::Instant::now(),
            prev_player_state: VhrPlayerState::Idle,
            last_compiled_vhr: None,

            wave_cache: WaveformCache::default(),
            wave_gen: 0,
            waterfall: Vec::new(),
            pending_delete_region: None,
            first_frame: true,
        };
        if app.audio_player.has_device() {
            app.console_logs.insert(1, format!("[Audio Output] Connected: {} Hz output device.", app.audio_player.host_rate()));
        } else {
            app.console_logs.insert(1, "[Audio Output] No audio output device found (click the red AUDIO OFF in the status bar to retry).".to_string());
        }
        app.pull_region_params();
        app.rebuild_patch();
        app.modulator_view.sync_from_vhl(&app.project.modulators);
        app
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Core logic
// ═══════════════════════════════════════════════════════════════════════════
impl HarmoniumApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        icons::install_fonts(&cc.egui_ctx); // icon font: replaces every "□" glyph
        let mut app = Self::default();
        AppTheme::for_mode(app.theme_mode).apply(&cc.egui_ctx);
        app.applied_mode = Some(app.theme_mode);
        app
    }

    fn log(&mut self, msg: impl Into<String>) {
        self.console_logs.push(msg.into());
        if self.console_logs.len() > 200 {
            self.console_logs.remove(0);
        }
    }

    /// Ask for the audio engine to be refreshed. The real work happens once at the end of the frame,
    /// so dragging a slider that fires many changes costs one rebuild per frame, not one per change.
    #[allow(dead_code)]
    pub fn sync_audio_engine(&mut self) {
        self.patch_dirty = true;
    }

    /// Build the immutable Patch (all samples + all regions) and hand it to the audio thread.
    fn rebuild_patch(&mut self) {
        if self.pcm_dirty || self.engine_pcm.len() != self.project.samples.len() {
            self.engine_pcm = self.project.samples.iter().map(|s| Arc::new(mono_pcm(s))).collect();
            self.pcm_dirty = false;
        }
        self.rebuild_key_map();
        self.audio_player.set_master_volume(self.master_volume / 100.0);
        let mut patch = Patch::from_project(&self.project, &self.engine_pcm);
        patch.custom_curves = self.modulator_view.get_custom_curves();
        self.audio_player.set_patch(patch);
        self.patch_dirty = false;
    }

    /// Sample data was added / removed / replaced.
    fn samples_changed(&mut self) {
        self.pcm_dirty = true;
        self.wave_gen += 1;
        self.patch_dirty = true;
    }

    /// Copy the selected region's envelope + filter into the DSP-rack fields.
    fn pull_region_params(&mut self) {
        if let Some(r) = self.project.regions.get(self.active_region_idx) {
            self.vol_delay_ms = r.vol_env.delay_ms as f32;
            self.attack_ms = r.vol_env.attack_ms as f32;
            self.hold_ms = r.vol_env.hold_ms as f32;
            self.decay_ms = r.vol_env.decay_ms as f32;
            self.sustain_percent = (r.vol_env.sustain_level as f32 / 10.0).clamp(0.0, 100.0);
            self.release_ms = r.vol_env.release_ms as f32;
            self.cutoff_hz = r.filter.cutoff_hz as f32;
            self.resonance_db = r.filter.resonance_cb as f32 / 10.0;

            self.mod_delay_ms = r.mod_env.delay_ms as f32;
            self.mod_attack_ms = r.mod_env.attack_ms as f32;
            self.mod_hold_ms = r.mod_env.hold_ms as f32;
            self.mod_decay_ms = r.mod_env.decay_ms as f32;
            self.mod_sustain_percent = (r.mod_env.sustain_level as f32 / 10.0).clamp(0.0, 100.0);
            self.mod_release_ms = r.mod_env.release_ms as f32;
        }
    }

    /// Make sure the "active" indices point at something that exists (after open / import / delete).
    fn clamp_selection(&mut self) {
        self.active_region_idx = self.active_region_idx.min(self.project.regions.len().saturating_sub(1));
        self.active_sample_idx = self.active_sample_idx.min(self.project.samples.len().saturating_sub(1));
    }

    pub fn select_region(&mut self, idx: usize) {
        if idx < self.project.regions.len() {
            self.active_region_idx = idx;
            self.pending_delete_region = None;
            self.pull_region_params();
            let sample_id = self.project.regions[idx].sample_id as usize;
            if sample_id < self.project.samples.len() && sample_id != self.active_sample_idx {
                self.active_sample_idx = sample_id;
                self.wave_gen += 1;
            }
        }
    }

    // ---- regions & instruments -------------------------------------------------------------

    /// Instrument index that owns region `idx` (instruments reference a contiguous run of regions).
    fn region_owner(&self, idx: usize) -> Option<usize> {
        self.project
            .instruments
            .iter()
            .position(|i| idx >= i.region_offset as usize && idx < i.region_offset as usize + i.region_count as usize)
    }

    #[allow(dead_code)]
    fn region_inst_name(&self, idx: usize) -> String {
        self.region_owner(idx).map(|o| instrument_name(&self.project.instruments[o])).unwrap_or_else(|| "—".to_string())
    }

    pub fn region_color(idx: usize) -> Color32 {
        const REGION_PALETTE: [Color32; 10] = [
            Color32::from_rgb(0, 210, 180),    // 0: Teal
            Color32::from_rgb(255, 107, 107),  // 1: Coral
            Color32::from_rgb(255, 209, 102),  // 2: Gold
            Color32::from_rgb(160, 108, 213),  // 3: Violet
            Color32::from_rgb(6, 214, 160),    // 4: Emerald
            Color32::from_rgb(17, 138, 178),   // 5: Sky Blue
            Color32::from_rgb(239, 71, 111),   // 6: Rose Pink
            Color32::from_rgb(181, 228, 140),  // 7: Lime Green
            Color32::from_rgb(247, 127, 0),    // 8: Tangerine
            Color32::from_rgb(114, 9, 183),    // 9: Deep Purple
        ];
        REGION_PALETTE[idx % REGION_PALETTE.len()]
    }

    pub fn region_text_color(idx: usize) -> Color32 {
        Self::region_color(idx)
    }

    pub fn region_name(&self, idx: usize) -> String {
        if let Some(name) = self.project.region_names.get(idx) {
            if !name.trim().is_empty() {
                return name.clone();
            }
        }
        if let Some(reg) = self.project.regions.get(idx) {
            if let Some(s) = self.project.samples.get(reg.sample_id as usize) {
                return s.name.clone();
            }
        }
        format!("Region #{}", idx)
    }

    fn inst_color(theme: &AppTheme, owner: Option<usize>) -> Color32 {
        match owner {
            Some(0) => theme.teal,
            Some(1) => theme.coral,
            Some(_) => theme.gold,
            None => theme.text_dim,
        }
    }

    #[allow(dead_code)]
    fn inst_text_color(theme: &AppTheme, owner: Option<usize>) -> Color32 {
        match owner {
            Some(0) => theme.teal_text,
            Some(1) => theme.coral_text,
            Some(_) => theme.gold_text,
            None => theme.text_dim,
        }
    }

    /// Adds a copy of the selected region (same sample and settings) right after it, inside the same instrument.
    pub fn add_region(&mut self) {
        if self.project.regions.len() >= 10 {
            self.log("[REGION] Limit reached: Maximum 10 regions allowed.".to_string());
            return;
        }
        if self.project.regions.is_empty() {
            let env = VHLEnvelope { delay_ms: 0, attack_ms: 25, hold_ms: 10, decay_ms: 150, sustain_level: 900, release_ms: 80, key_to_hold: 0, key_to_decay: 0 };
            self.project.regions.push(make_region(0, 60, VHL_LOOP_CONTINUOUS, 0, 20, 0, env));
            self.project.region_names = vec!["Region #0".to_string()];
            self.select_region(0);
            self.patch_dirty = true;
            return;
        }
        let src = self.active_region_idx.min(self.project.regions.len() - 1);
        let copy = self.project.regions[src].clone();
        let owner = self.region_owner(src);
        let at = match owner {
            Some(o) => (self.project.instruments[o].region_offset + self.project.instruments[o].region_count) as usize,
            None => self.project.regions.len(),
        };
        self.project.regions.insert(at, copy);
        let new_name = format!("Region #{}", at);
        if at <= self.project.region_names.len() {
            self.project.region_names.insert(at, new_name);
        } else {
            self.project.region_names.push(new_name);
        }
        for (j, inst) in self.project.instruments.iter_mut().enumerate() {
            if Some(j) == owner {
                inst.region_count += 1;
            } else if inst.region_offset as usize >= at {
                inst.region_offset += 1;
            }
        }
        self.select_region(at);
        self.patch_dirty = true;
        self.log(format!("[REGION] Added Region {} (copy of Region {}). Pick its sample and key range in the inspector.", at, src));
    }

    pub fn delete_region(&mut self, idx: usize) {
        if self.project.regions.len() <= 1 || idx >= self.project.regions.len() {
            return;
        }
        let owner = self.region_owner(idx);
        self.project.regions.remove(idx);
        if idx < self.project.region_names.len() {
            self.project.region_names.remove(idx);
        }
        for (j, inst) in self.project.instruments.iter_mut().enumerate() {
            if Some(j) == owner {
                inst.region_count = inst.region_count.saturating_sub(1);
            } else if inst.region_offset as usize > idx {
                inst.region_offset -= 1;
            }
        }
        self.pending_delete_region = None;
        self.clamp_selection();
        self.select_region(self.active_region_idx);
        self.patch_dirty = true;
        self.log(format!("[REGION] Deleted Region {}", idx));
    }

    pub fn delete_sample(&mut self, idx: usize) {
        if self.project.samples.len() <= 1 || idx >= self.project.samples.len() {
            return;
        }
        let sample_name = self.project.samples.remove(idx).name;
        // Regions that used this sample fall back to sample 0; later samples shift down by one.
        for r in &mut self.project.regions {
            if r.sample_id as usize == idx {
                r.sample_id = 0;
            } else if r.sample_id as usize > idx {
                r.sample_id -= 1;
            }
        }
        if self.active_sample_idx > idx {
            self.active_sample_idx -= 1;
        }
        self.clamp_selection();
        self.samples_changed();
        self.log(format!("[SAMPLE] Removed sample: {} (regions that used it now use sample 0)", sample_name));
    }

    /// Point a region at a sample.
    fn assign_sample(&mut self, region: usize, sample: usize) {
        if region < self.project.regions.len() && sample < self.project.samples.len() {
            self.project.regions[region].sample_id = sample as u16;
            if region == self.active_region_idx {
                self.active_sample_idx = sample;
                self.wave_gen += 1;
            }
            self.patch_dirty = true;
        }
    }

    /// Which instrument answers each key follows from the regions: the first region that covers a key decides.
    /// Keeps the VHL key map (and the legacy split value stored in .hwproj) in step with the region layout.
    fn rebuild_key_map(&mut self) {
        let mut first_zero = 0usize;
        let mut counting = true;
        for i in 0..KEY_COUNT.min(self.project.key_map.len()) {
            let owner = self.project.regions.iter().enumerate().find(|(_, r)| i as u8 >= r.key_lo && i as u8 <= r.key_hi).and_then(|(ri, _)| self.region_owner(ri));
            if let Some(o) = owner {
                let o = o.min(255) as u8;
                self.project.key_map[i].assigned_inst_id = o;
                self.project.key_map[i].zone_id = o.min(1);
            }
            if counting && self.project.key_map[i].zone_id == 0 {
                first_zero = i + 1;
            } else {
                counting = false;
            }
        }
        self.split_key_index = (first_zero.max(1) - 1).min(59) as u8;
    }

    /// Colour of the region that answers each key (None = no region: the key is silent).
    fn key_zones(&self, _theme: &AppTheme) -> KeyZones {
        let mut z: KeyZones = [None; KEY_COUNT];
        for (i, slot) in z.iter_mut().enumerate() {
            *slot = self.project.regions.iter().enumerate().find(|(_, r)| i as u8 >= r.key_lo && i as u8 <= r.key_hi).map(|(ri, _)| Self::region_color(ri));
        }
        z
    }

    fn region_bars(&self, _theme: &AppTheme) -> Vec<RegionBar> {
        self.project
            .regions
            .iter()
            .enumerate()
            .map(|(i, r)| {
                let name = self.region_name(i);
                RegionBar {
                    lo: r.key_lo,
                    hi: r.key_hi,
                    color: Self::region_color(i),
                    text_color: Self::region_text_color(i),
                    label: {
                        let shift = r.coarse_tune as i32;
                        let mut l = format!("R{} {} · {}–{}", i, name, key_note_name(r.key_lo), key_note_name(r.key_hi));
                        if shift != 0 {
                            let p = |k: u8| midi_note_name((k as i32 + 36 + shift).clamp(0, 127) as u8);
                            l.push_str(&format!(" → sounds {}–{}", p(r.key_lo), p(r.key_hi)));
                        }
                        l
                    },
                    active: i == self.active_region_idx,
                }
            })
            .collect()
    }

    /// Recover the split point from a loaded key map (zone 0 keys form the left zone).
    fn derive_split(&self) -> u8 {
        let n = self.project.key_map.iter().take_while(|k| k.zone_id == 0).count();
        (n.max(1) - 1).min(59) as u8
    }

    // ---- project / file operations ----------------------------------------------------------

    pub fn new_project(&mut self) {
        self.vhr_player.stop(&self.audio_player);
        self.vhr_player.recording = None;
        self.audio_player.all_notes_off();
        self.held_midi = [None; KEY_COUNT];
        self.waterfall.clear();
        self.project_name = "Untitled Project".to_string();
        self.project_path = None;
        self.project = default_project();
        self.tempo_bpm = 120;
        self.transpose_semitones = 0;
        self.master_volume = 80.0;
        self.split_key_index = 24;
        self.active_sample_idx = 0;
        self.active_region_idx = 0;
        self.select_region(0);
        self.samples_changed();
        self.modulator_view.sync_from_vhl(&self.project.modulators);
        self.log("[PROJECT] Initialized new project.");
    }

    pub fn open_project_dialog(&mut self) {
        if let Some(path) = rfd::FileDialog::new().set_title("Open Harmonium Project (.hwproj)").add_filter("Harmonium Project", &["hwproj"]).pick_file() {
            match HarmoniumProject::load_from_file(&path) {
                Ok(proj) => {
                    self.audio_player.all_notes_off();
                    self.held_midi = [None; KEY_COUNT];
                    self.waterfall.clear();
                    self.project_name = proj.name;
                    self.project_path = Some(path.clone());
                    self.tempo_bpm = proj.tempo_bpm;
                    self.transpose_semitones = proj.transpose_semitones;
                    self.master_volume = proj.master_volume;
                    self.split_key_index = proj.split_key_index.min(59);
                    self.project = proj.vhl;
                    self.active_sample_idx = 0;
                    self.active_region_idx = 0;
                    self.vhr_player.stop(&self.audio_player);
                    if let Some(first_rec) = proj.recordings.into_iter().next() {
                        self.vhr_player.load(first_rec);
                    } else {
                        self.vhr_player.recording = None;
                    }
                    self.select_region(0);
                    self.samples_changed();
                    self.modulator_view.sync_from_vhl(&self.project.modulators);
                    self.log(format!("[PROJECT] Opened: {} ({:?})", self.project_name, path));
                }
                Err(e) => self.log(format!("[ERROR] Failed to open project: {}", e)),
            }
        }
    }

    pub fn save_project_auto(&mut self) {
        if let Some(path) = self.project_path.clone() {
            self.save_project_to_path(&path);
        } else {
            self.save_project_dialog();
        }
    }

    pub fn save_project_dialog(&mut self) {
        let default_name = format!("{}.hwproj", self.project_name);
        if let Some(path) = rfd::FileDialog::new().set_title("Save Harmonium Project (.hwproj)").add_filter("Harmonium Project", &["hwproj"]).set_file_name(&default_name).save_file() {
            if let Some(stem) = path.file_stem().and_then(|s| s.to_str()) {
                self.project_name = stem.to_string();
            }
            self.project_path = Some(path.clone());
            self.save_project_to_path(&path);
        }
    }

    pub fn save_project_to_path(&mut self, path: &std::path::Path) {
        let mut proj = HarmoniumProject::new(&self.project_name, self.project.clone());
        proj.tempo_bpm = self.tempo_bpm;
        proj.transpose_semitones = self.transpose_semitones;
        proj.master_volume = self.master_volume;
        proj.split_key_index = self.split_key_index;
        if let Some(rec) = &self.vhr_player.recording {
            proj.recordings.push(rec.clone());
        }
        match proj.save_to_file(path) {
            Ok(()) => self.log(format!("[PROJECT] Saved: {:?}", path)),
            Err(e) => self.log(format!("[ERROR] Failed to save project: {}", e)),
        }
    }

    /// Compile the project and write the .vhl package to disk (compiling alone only kept it in memory before).
    fn compile_and_save_vhl(&mut self) {
        match VhlCompiler::compile(&self.project) {
            Ok(bytes) => {
                let sz = bytes.len();
                self.log(format!("[SUCCESS] VHL v2.0 package compiled ({} bytes)", sz));
                let default_name = format!("{}.vhl", self.project_name.trim());
                if let Some(path) = rfd::FileDialog::new().set_title("Save compiled VHL package").add_filter("VHL Package", &["vhl"]).set_file_name(&default_name).save_file() {
                    match std::fs::write(&path, &bytes) {
                        Ok(()) => self.log(format!("[SUCCESS] Saved {:?}", path)),
                        Err(e) => self.log(format!("[ERROR] Could not write VHL file: {}", e)),
                    }
                }
                self.last_compiled_vhl = Some(bytes);
            }
            Err(e) => self.log(format!("[ERROR] Compile failed: {}", e)),
        }
    }

    fn import_vhl_package(&mut self) {
        if let Some(path) = rfd::FileDialog::new().set_title("Select VHL Package").add_filter("VHL Package", &["vhl"]).pick_file() {
            match std::fs::read(&path) {
                Ok(bytes) => match VhlDecompiler::decompile(&bytes) {
                    Ok(proj) => {
                        self.audio_player.all_notes_off();
                        self.held_midi = [None; KEY_COUNT];
                        self.project = proj;
                        self.split_key_index = self.derive_split();
                        self.active_sample_idx = 0;
                        self.active_region_idx = 0;
                        self.select_region(0);
                        self.samples_changed();
                        self.log(format!("[SUCCESS] Loaded VHL package: {} ({} bytes)", path.file_name().unwrap_or_default().to_string_lossy(), bytes.len()));
                    }
                    Err(e) => self.log(format!("[ERROR] Failed to decompile VHL: {}", e)),
                },
                Err(e) => self.log(format!("[ERROR] Could not read file: {}", e)),
            }
        }
    }

    /// Compile active project and upload to ESP32 over Wi-Fi
    pub fn sync_to_esp32(&mut self) {
        self.log("[SYNC] Compiling active VHL v2.0 project for ESP32...".to_string());
        let bytes = match VhlCompiler::compile(&self.project) {
            Ok(b) => b,
            Err(e) => {
                self.log(format!("[SYNC ERROR] Compilation failed: {}", e));
                return;
            }
        };

        let sz = bytes.len();
        self.last_compiled_vhl = Some(bytes.clone());
        let ip = self.esp32_ip.trim().to_string();
        self.log(format!("[SYNC] Uploading package ({} bytes) to http://{}/api/upload_vhl ...", sz, ip));

        match Self::send_vhl_http(&ip, &bytes) {
            Ok(msg) => {
                self.is_connected_wifi = true;
                self.log(format!("[SYNC SUCCESS] {}", msg));
            }
            Err(e) => {
                self.is_connected_wifi = false;
                self.log(format!("[SYNC ERROR] Failed to send VHL to {}: {}", ip, e));
            }
        }
    }

    /// Check if the ESP32 HTTP endpoint is reachable
    pub fn check_esp32_connection(&mut self) {
        let ip = self.esp32_ip.trim().to_string();
        self.log(format!("[WIFI] Probing ESP32 at http://{}:80/api/status ...", ip));
        let online = (|| {
            use std::io::{Read, Write};
            use std::net::{TcpStream, ToSocketAddrs};
            use std::time::Duration;

            let addr_str = format!("{}:80", ip);
            let mut addrs = addr_str.to_socket_addrs().ok()?;
            let sock = addrs.next()?;
            let mut stream = TcpStream::connect_timeout(&sock, Duration::from_millis(1500)).ok()?;
            stream.set_read_timeout(Some(Duration::from_millis(2000))).ok();
            stream.set_write_timeout(Some(Duration::from_millis(1500))).ok();
            let req = format!("GET /api/status HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n", ip);
            stream.write_all(req.as_bytes()).ok()?;
            stream.flush().ok()?;
            let mut resp = [0u8; 512];
            let n = stream.read(&mut resp).ok()?;
            if n > 0 && String::from_utf8_lossy(&resp[..n]).contains("200 OK") {
                Some(true)
            } else {
                None
            }
        })().unwrap_or(false);

        self.is_connected_wifi = online;
        if online {
            self.log(format!("[WIFI ONLINE] ESP32 at {} is connected and responsive.", ip));
        } else {
            self.log(format!("[WIFI OFFLINE] Could not reach ESP32 at {}. Check that laptop Wi-Fi is connected to \"Harmonium-ESP32\".", ip));
        }
    }

    fn send_vhl_http(ip: &str, data: &[u8]) -> Result<String, String> {
        use std::io::{Read, Write};
        use std::net::{TcpStream, ToSocketAddrs};
        use std::time::Duration;

        let addr_str = format!("{}:80", ip);
        let mut addrs = addr_str.to_socket_addrs()
            .map_err(|e| format!("Cannot resolve '{}': {}", addr_str, e))?;
        let sock = addrs.next()
            .ok_or_else(|| format!("Could not resolve address '{}'", addr_str))?;

        let mut stream = TcpStream::connect_timeout(&sock, Duration::from_secs(4))
            .map_err(|e| format!("Could not connect to {}: {}. Ensure you are connected to the 'Harmonium-ESP32' Wi-Fi network.", addr_str, e))?;

        stream.set_read_timeout(Some(Duration::from_secs(12))).ok();
        stream.set_write_timeout(Some(Duration::from_secs(12))).ok();

        let header = format!(
            "POST /api/upload_vhl HTTP/1.1\r\n\
             Host: {}\r\n\
             Content-Type: application/octet-stream\r\n\
             Content-Length: {}\r\n\
             Connection: close\r\n\r\n",
            ip,
            data.len()
        );

        stream.write_all(header.as_bytes())
            .map_err(|e| format!("Failed to send headers: {}", e))?;
        stream.write_all(data)
            .map_err(|e| format!("Failed to send VHL body: {}", e))?;
        stream.flush().map_err(|e| format!("Flush error: {}", e))?;

        let mut response = String::new();
        match stream.read_to_string(&mut response) {
            Ok(_) => {
                if response.contains("200 OK") {
                    Ok(format!("ESP32 received and loaded {} bytes successfully!", data.len()))
                } else {
                    let first_line = response.lines().next().unwrap_or("Unknown response");
                    Err(format!("ESP32 error: {}", first_line))
                }
            }
            Err(e) => {
                Err(format!("Response read error: {}. (ESP32 might have restarted or closed connection)", e))
            }
        }
    }

    /// Import one or more WAV files. The first imported sample is assigned to the selected region.
    pub fn import_wav_samples_dialog(&mut self) {
        let Some(paths) = rfd::FileDialog::new().set_title("Import WAV Samples").add_filter("WAV Audio", &["wav", "wave"]).pick_files() else { return };
        let first_new = self.project.samples.len();
        let mut count = 0;
        for path in paths {
            match crate::vhl::wav_reader::WavReader::read_from_file(&path) {
                Ok(info) => {
                    let sample_name = path.file_stem().and_then(|s| s.to_str()).unwrap_or("Imported_Sample").to_string();
                    self.project.samples.push(crate::vhl::wav_reader::WavReader::to_vhl_sample(info, &sample_name));
                    count += 1;
                }
                Err(e) => self.log(format!("[ERROR] {:?}: {}", path.file_name().unwrap_or_default(), e)),
            }
        }
        if count > 0 {
            self.samples_changed();
            let region = self.active_region_idx;
            self.assign_sample(region, first_new);
            self.active_sample_idx = first_new;
            self.log(format!("[ASSETS] Imported {} WAV sample(s); \"{}\" is now assigned to Region {}.", count, self.project.samples[first_new].name, region));
        }
    }

    // ---- notes ------------------------------------------------------------------------------

    /// A key goes down (laptop keyboard or mouse).
    pub fn key_on(&mut self, now: f64, key_idx: u8, vel: u8) {
        let k = key_idx as usize;
        if k >= KEY_COUNT || self.held_midi[k].is_some() {
            return;
        }
        let midi = (key_idx as i32 + 36 + self.transpose_semitones).clamp(0, 127) as u8;
        self.held_midi[k] = Some(midi);
        self.audio_player.note_on(key_idx, midi, vel);
        self.vhr_recorder.record_note_on(midi, vel);
        self.waterfall.push(WaterNote { key: key_idx, t_on: now, t_off: None });
        self.last_clicked_key = Some(key_idx);
    }

    /// A key comes up. Uses the MIDI note it *started* with, so changing
    /// transpose while a key is held can't leave a note hanging.
    pub fn key_off(&mut self, now: f64, key_idx: u8) {
        let k = key_idx as usize;
        if k >= KEY_COUNT {
            return;
        }
        if let Some(midi) = self.held_midi[k].take() {
            self.audio_player.note_off(key_idx);
            self.vhr_recorder.record_note_off(midi);
            if let Some(n) = self.waterfall.iter_mut().rev().find(|n| n.key == key_idx && n.t_off.is_none()) {
                n.t_off = Some(now);
            }
            if self.last_clicked_key == Some(key_idx) {
                self.last_clicked_key = None;
            }
        }
    }

    fn release_all_visual_notes(&mut self, now: f64) {
        self.held_midi = [None; KEY_COUNT];
        self.last_clicked_key = None;
        for n in &mut self.waterfall {
            if n.t_off.is_none() {
                n.t_off = Some(now);
            }
        }
    }

    fn handle_laptop_keys(&mut self, ctx: &egui::Context, now: f64) {
        // Window lost focus: key-up events never arrive, so release everything or notes would hang.
        if !ctx.input(|i| i.focused) {
            let held: Vec<u8> = self.laptop_notes.drain().map(|(_, idx)| idx).collect();
            for idx in held {
                self.key_off(now, idx);
            }
            return;
        }
        let typing = ctx.wants_keyboard_input(); // typing in the asset filter must not play notes
        let events: Vec<(egui::Key, bool, egui::Modifiers)> = ctx.input(|i| {
            i.events
                .iter()
                .filter_map(|e| match e {
                    egui::Event::Key { key, pressed, repeat: false, modifiers, .. } => Some((*key, *pressed, *modifiers)),
                    _ => None,
                })
                .collect()
        });
        for (key, pressed, modifiers) in events {
            if pressed {
                if typing || modifiers.ctrl || modifiers.command || modifiers.alt || self.laptop_notes.contains_key(&key) {
                    continue;
                }
                if let Some(idx) = laptop_key_to_piano_key(key, modifiers.shift) {
                    self.laptop_notes.insert(key, idx);
                    let vel = if self.current_view == AppView::ModulationEditor {
                        (self.modulator_view.test_velocity as u8).max(1)
                    } else {
                        110
                    };
                    self.key_on(now, idx, vel);
                }
            } else if let Some(idx) = self.laptop_notes.remove(&key) {
                // Releases are always honoured, even if a text box grabbed focus while the key was down.
                self.key_off(now, idx);
            }
        }
    }

    fn handle_mouse_key(&mut self, now: f64, pressed: Option<u8>) {
        if pressed != self.mouse_key {
            if let Some(old) = self.mouse_key.take() {
                self.key_off(now, old);
            }
            if let Some(k) = pressed {
                let vel = if self.current_view == AppView::ModulationEditor {
                    (self.modulator_view.test_velocity as u8).max(1)
                } else {
                    100
                };
                self.key_on(now, k, vel);
                self.mouse_key = Some(k);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Frame
// ═══════════════════════════════════════════════════════════════════════════
impl eframe::App for HarmoniumApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if self.applied_mode != Some(self.theme_mode) {
            AppTheme::for_mode(self.theme_mode).apply(ctx);
            self.applied_mode = Some(self.theme_mode);
        }
        if self.first_frame {
            self.first_frame = false;
            if ctx.screen_rect().width() < 1100.0 {
                self.show_left_sidebar = false; // small window: give the workspace the room
            }
        }
        let theme = AppTheme::for_mode(self.theme_mode);
        let now = ctx.input(|i| i.time);

        // VHR replayer tick
        let t = std::time::Instant::now();
        let elapsed_us = t.duration_since(self.last_frame_instant).as_micros().min(u32::MAX as u128) as u32;
        self.last_frame_instant = t;
        let vhr_events = self.vhr_player.tick(elapsed_us, &self.audio_player);
        for ev in vhr_events {
            match ev {
                VhrTickEvent::NoteOn { key_idx, note_midi, .. } => {
                    let k = key_idx as usize;
                    if k < KEY_COUNT {
                        self.held_midi[k] = Some(note_midi);
                        self.last_clicked_key = Some(key_idx);
                        self.waterfall.push(WaterNote { key: key_idx, t_on: now, t_off: None });
                    }
                }
                VhrTickEvent::NoteOff { key_idx, .. } => {
                    let k = key_idx as usize;
                    if k < KEY_COUNT {
                        self.held_midi[k] = None;
                        if self.last_clicked_key == Some(key_idx) {
                            self.last_clicked_key = None;
                        }
                        if let Some(n) = self.waterfall.iter_mut().rev().find(|n| n.key == key_idx && n.t_off.is_none()) {
                            n.t_off = Some(now);
                        }
                    }
                }
            }
        }
        // Playback just ended: the engine silenced itself, make the keys/waterfall follow.
        if self.prev_player_state == VhrPlayerState::Playing && self.vhr_player.state != VhrPlayerState::Playing && self.laptop_notes.is_empty() && self.mouse_key.is_none() {
            self.release_all_visual_notes(now);
        }
        self.prev_player_state = self.vhr_player.state;

        self.handle_laptop_keys(ctx, now);

        // Waterfall housekeeping: drop faded notes
        self.waterfall.retain(|n| n.t_off.map_or(true, |t| now - t < WATERFALL_TRAIL_SECS as f64));

        let screen_h = ctx.screen_rect().height();

        match self.current_view {
            AppView::Landing => {
                let action = crate::landing::LandingPage::show(
                    ctx,
                    &theme,
                    &self.project_name,
                    self.project.samples.len(),
                    self.project.regions.len(),
                    self.audio_player.host_rate(),
                    self.audio_player.is_ok(),
                );
                match action {
                    crate::landing::LandingAction::OpenSoundEditor => {
                        self.current_view = AppView::SoundEditor;
                    }
                    crate::landing::LandingAction::OpenProject => {
                        self.open_project_dialog();
                        self.current_view = AppView::SoundEditor;
                    }
                    crate::landing::LandingAction::OpenModulationEditor => {
                        self.current_view = AppView::ModulationEditor;
                    }
                    crate::landing::LandingAction::None => {}
                }
            }
            AppView::SoundEditor => {
                // 1. Header ----------------------------------------------------------
                egui::TopBottomPanel::top("top_header_panel")
                    .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::symmetric(14.0, 8.0)))
                    .show(ctx, |ui| self.ui_header(ui, &theme));

                // 2. Status bar ------------------------------------------------------
                egui::TopBottomPanel::bottom("bottom_status_panel")
                    .frame(Frame::none().fill(theme.window).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::symmetric(14.0, 4.0)))
                    .show(ctx, |ui| self.ui_status(ui, &theme));

                // 3. Dock: tape deck + waterfall + keyboard (full width, above status bar)
                egui::TopBottomPanel::bottom("dock_panel")
                    .resizable(false)
                    .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin { left: 12.0, right: 12.0, top: 10.0, bottom: 12.0 }))
                    .show(ctx, |ui| self.ui_dock(ui, &theme, now, screen_h));

                // 4. Library (left) --------------------------------------------------
                if self.show_left_sidebar {
                    egui::SidePanel::left("left_asset_sidebar")
                        .default_width(220.0)
                        .width_range(180.0..=320.0)
                        .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)))
                        .show(ctx, |ui| self.ui_library(ui, &theme));
                }

                // 5. Inspector (right) ------------------------------------------------
                if self.show_right_inspector {
                    egui::SidePanel::right("right_parameter_inspector")
                        .default_width(320.0)
                        .width_range(290.0..=440.0)
                        .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::same(12.0)))
                        .show(ctx, |ui| self.ui_inspector(ui, &theme));
                }

                // 6. Workspace --------------------------------------------------------
                egui::CentralPanel::default().frame(Frame::none().fill(theme.window)).show(ctx, |ui| {
                    egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                        Frame::none().inner_margin(Margin::same(12.0)).show(ui, |ui| self.ui_workspace(ui, &theme));
                    });
                });
            }
            AppView::ModulationEditor => {
                // 1. Header
                egui::TopBottomPanel::top("top_header_panel")
                    .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::symmetric(14.0, 8.0)))
                    .show(ctx, |ui| self.ui_header(ui, &theme));

                // 2. Status bar
                egui::TopBottomPanel::bottom("bottom_status_panel")
                    .frame(Frame::none().fill(theme.window).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::symmetric(14.0, 4.0)))
                    .show(ctx, |ui| self.ui_status(ui, &theme));

                // 3. Dock: keyboard & waterfall to test modulations in real time
                egui::TopBottomPanel::bottom("dock_panel")
                    .resizable(false)
                    .frame(Frame::none().fill(theme.panel).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin { left: 12.0, right: 12.0, top: 10.0, bottom: 12.0 }))
                    .show(ctx, |ui| self.ui_dock(ui, &theme, now, screen_h));

                // 4. Central Modulation Rack
                egui::CentralPanel::default().frame(Frame::none().fill(theme.window)).show(ctx, |ui| {
                    egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                        Frame::none().inner_margin(Margin::same(12.0)).show(ui, |ui| {
                            let reg_names: Vec<String> = self.project.regions.iter().enumerate().map(|(i, _)| {
                                format!("Region #{}: {}", i, self.region_name(i))
                            }).collect();
                            let action = self.modulator_view.show(ui, &theme, &reg_names);
                            let new_mods = self.modulator_view.to_vhl();
                            if new_mods != self.project.modulators || ui.input(|i| i.pointer.any_down()) {
                                self.project.modulators = new_mods;
                                self.patch_dirty = true;
                            }
                            self.audio_player.control_change(1, self.modulator_view.test_mod_wheel as u8);
                            self.audio_player.control_change(11, self.modulator_view.test_expression as u8);
                            self.audio_player.pitch_bend(self.modulator_view.test_pitch_bend as i16);
                            match action {
                                crate::modulator_editor::ModulatorViewAction::BackToLanding => {
                                    self.current_view = AppView::Landing;
                                }
                                crate::modulator_editor::ModulatorViewAction::OpenSoundEditor => {
                                    self.current_view = AppView::SoundEditor;
                                }
                                crate::modulator_editor::ModulatorViewAction::None => {}
                            }
                        });
                    });
                });
            }
            AppView::LivePlayer => {
                self.current_view = AppView::Landing;
            }
        }

        // 7. Raga learner modal -------------------------------------------------
        if self.show_note_learner_modal {
            let mut open = true;
            egui::Window::new(format!("{} Note & Raga Learner", icons::BOOK_OPEN)).open(&mut open).default_size([560.0, 200.0]).show(ctx, |ui| {
                ui.label(RichText::new("Indian Classical Swara Mapping (Bilawal Thaat)").strong().color(theme.teal_text));
                ui.add_space(8.0);
                ui.horizontal_wrapped(|ui| {
                    for (swara, note) in [("Sa", "C4"), ("Re", "D4"), ("Ga", "E4"), ("Ma", "F4"), ("Pa", "G4"), ("Dha", "A4"), ("Ni", "B4"), ("Sa'", "C5")] {
                        widgets::card(ui, &theme, |ui| {
                            ui.set_min_width(0.0);
                            ui.vertical_centered(|ui| {
                                ui.label(RichText::new(swara).heading().strong().color(theme.gold_text));
                                ui.label(mono(note, 11.5, theme.text_dim));
                            });
                        });
                    }
                });
            });
            self.show_note_learner_modal = open;
        }

        // All edits of this frame reach the audio engine in one go.
        if self.patch_dirty {
            self.rebuild_patch();
        }

        // Output meter (peak with decay) and repaint policy. Requested *after* the UI so that a click on
        // PLAY/REC in this very frame already keeps the animation running.
        let peak = self.audio_player.take_peak();
        self.peak_meter = peak.max(self.peak_meter * 0.90);
        let animating = self.vhr_player.state == VhrPlayerState::Playing
            || self.vhr_recorder.is_recording
            || (self.show_note_waterfall && !self.waterfall.is_empty())
            || self.peak_meter > 0.004
            || self.held_midi.iter().any(|m| m.is_some());
        if animating {
            ctx.request_repaint();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Header
// ═══════════════════════════════════════════════════════════════════════════
impl HarmoniumApp {
    fn ui_header(&mut self, ui: &mut Ui, theme: &AppTheme) {
        // The header is laid out against an exact width budget, so nothing can overlap:
        //   t0 >= 1620 everything · t1 >= 1400 no subtitle/kHz · t2 >= 1260 short buttons · below: icons only
        let w = ui.available_width();
        let (t0, t1, t2) = (w >= 1620.0, w >= 1400.0, w >= 1260.0);
        let sp = 8.0;
        let h = 34.0;

        let (pill_color, pill_fill, pill_text_color, pill_txt) = if self.is_connected_wifi {
            (theme.teal, AppTheme::alpha(theme.teal, 26), theme.teal_text, if t1 { "ESP32 online · Wi-Fi" } else if t2 { "Online" } else { "" })
        } else {
            (
                theme.rec_red,
                AppTheme::alpha(theme.rec_red, 24),
                if theme.mode == ThemeMode::Dark { Color32::from_rgb(255, 138, 147) } else { Color32::from_rgb(196, 43, 58) },
                if t1 { "ESP32 offline · Wi-Fi" } else if t2 { "Offline" } else { "" },
            )
        };
        let pill_w = widgets::status_pill_width(ui, pill_txt);
        let right_w = 4.0 * 32.0 + 4.0 * sp + pill_w;

        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = sp;
            let left_w = (w - right_w - sp).max(120.0);
            ui.allocate_ui_with_layout(Vec2::new(left_w, h), Layout::left_to_right(Align::Center), |ui| {
                ui.set_clip_rect(ui.max_rect().intersect(ui.clip_rect())); // too wide = clipped, never overlapping
                ui.spacing_mut().item_spacing.x = sp;
                self.header_left(ui, theme, t0, t1, t2);
            });
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.spacing_mut().item_spacing.x = sp;
                let theme_icon = if self.theme_mode == ThemeMode::Dark { icons::SUN } else { icons::MOON };
                let theme_tip = if self.theme_mode == ThemeMode::Dark { "Switch to light theme" } else { "Switch to dark theme" };
                if widgets::icon_toggle(ui, theme, theme_icon, theme_tip, false) {
                    self.theme_mode = self.theme_mode.toggled();
                }
                if widgets::icon_toggle(ui, theme, icons::SLIDERS_HORIZONTAL, "Parameter inspector", self.show_right_inspector) {
                    self.show_right_inspector = !self.show_right_inspector;
                }
                if widgets::icon_toggle(ui, theme, icons::WAVES, "Note waterfall", self.show_note_waterfall) {
                    self.show_note_waterfall = !self.show_note_waterfall;
                }
                if widgets::icon_toggle(ui, theme, icons::SIDEBAR_LEFT, "Asset library", self.show_left_sidebar) {
                    self.show_left_sidebar = !self.show_left_sidebar;
                }
                if widgets::status_pill(ui, pill_color, pill_fill, pill_text_color, pill_txt)
                    .on_hover_text(if self.is_connected_wifi {
                        format!("ESP32 connected at {} (Click to re-check)", self.esp32_ip)
                    } else {
                        format!("ESP32 offline at {} (Click to test connection)", self.esp32_ip)
                    })
                    .clicked()
                {
                    self.check_esp32_connection();
                }
            });
        });
    }

    fn header_left(&mut self, ui: &mut Ui, theme: &AppTheme, t0: bool, t1: bool, t2: bool) {
        // Logo + title
        let (logo, _) = ui.allocate_exact_size(Vec2::new(22.0, 26.0), Sense::hover());
        let p = ui.painter_at(logo);
        for (dx, top, h) in [(1.0, 5.0, 16.0), (8.5, 2.0, 22.0), (16.0, 8.0, 12.0)] {
            p.rect_filled(Rect::from_min_size(Pos2::new(logo.min.x + dx, logo.min.y + top), Vec2::new(4.5, h)), Rounding::same(2.0), theme.teal);
        }
        if t2 {
            ui.vertical(|ui| {
                ui.spacing_mut().item_spacing.y = 0.0;
                ui.label(RichText::new("Harmonium Workstation").strong().size(15.0).color(theme.text));
                if t0 {
                    ui.label(RichText::new("ESP32 DSP synthesis · 61-key revival studio").size(11.0).color(theme.text_dim));
                }
            });
        }
        ui.separator();

        // View navigation switcher (Home / Sound Editor / Modulator Editor)
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 4.0;
            let home_active = self.current_view == AppView::Landing;
            let editor_active = self.current_view == AppView::SoundEditor;
            let mod_active = self.current_view == AppView::ModulationEditor;

            let (h_fill, h_stroke, h_text) = if home_active {
                (theme.teal.gamma_multiply(0.18), Stroke::new(1.0_f32, theme.teal), theme.teal)
            } else {
                (Color32::TRANSPARENT, Stroke::new(1.0_f32, theme.hairline), theme.text_dim)
            };
            if ui.add(egui::Button::new(RichText::new("🏠 Home").size(12.0).color(h_text))
                .fill(h_fill)
                .stroke(h_stroke)
                .rounding(Rounding::same(4.0))
            ).on_hover_text("Return to landing page").clicked() {
                self.current_view = AppView::Landing;
            }

            let (e_fill, e_stroke, e_text) = if editor_active {
                (theme.teal.gamma_multiply(0.18), Stroke::new(1.0_f32, theme.teal), theme.teal)
            } else {
                (Color32::TRANSPARENT, Stroke::new(1.0_f32, theme.hairline), theme.text_dim)
            };
            if ui.add(egui::Button::new(RichText::new("🎹 Sound Editor").size(12.0).color(e_text))
                .fill(e_fill)
                .stroke(e_stroke)
                .rounding(Rounding::same(4.0))
            ).on_hover_text("Synthesizer & multi-sample editor").clicked() {
                self.current_view = AppView::SoundEditor;
            }

            let (m_fill, m_stroke, m_text) = if mod_active {
                (theme.coral.gamma_multiply(0.18), Stroke::new(1.0_f32, theme.coral), theme.coral)
            } else {
                (Color32::TRANSPARENT, Stroke::new(1.0_f32, theme.hairline), theme.text_dim)
            };
            if ui.add(egui::Button::new(RichText::new("⚡ Modulators").size(12.0).color(m_text))
                .fill(m_fill)
                .stroke(m_stroke)
                .rounding(Rounding::same(4.0))
            ).on_hover_text("Dynamic SoundFont 2 modulator routing matrix").clicked() {
                self.current_view = AppView::ModulationEditor;
            }
        });
        ui.separator();

        // Project menu
        if t1 {
            ui.label(caps(theme, "PROJECT"));
        }
        let proj_title = if self.project_name.trim().is_empty() { "Untitled".to_string() } else { self.project_name.clone() };
        let short: String = if proj_title.chars().count() > 16 { format!("{}…", proj_title.chars().take(15).collect::<String>()) } else { proj_title };
        let menu_label = if t2 { format!("{}  {}  {}", icons::FOLDER, short, icons::CARET_DOWN) } else { icons::FOLDER.to_string() };
        ui.menu_button(RichText::new(menu_label).strong().size(12.5).color(theme.text), |ui| {
            ui.set_min_width(220.0);
            if ui.button(format!("{} New Project", icons::FILE)).clicked() {
                self.new_project();
                ui.close_menu();
            }
            if ui.button(format!("{} Open Project (.hwproj)...", icons::OPEN)).clicked() {
                self.open_project_dialog();
                ui.close_menu();
            }
            ui.separator();
            if ui.button(format!("{} Save Project", icons::SAVE)).clicked() {
                self.save_project_auto();
                ui.close_menu();
            }
            if ui.button(format!("{} Save Project As...", icons::SAVE)).clicked() {
                self.save_project_dialog();
                ui.close_menu();
            }
            ui.separator();
            if ui.button(format!("{} Compile && export VHL package...", icons::COMPILE)).clicked() {
                self.compile_and_save_vhl();
                ui.close_menu();
            }
            if ui.button(format!("{} Import VHL package...", icons::IMPORT)).clicked() {
                self.import_vhl_package();
                ui.close_menu();
            }
        });

        // Build / import / sync
        let compile_txt = if t1 { format!("{} Compile VHL", icons::COMPILE) } else { icons::COMPILE.to_string() };
        if widgets::primary_button(ui, theme, &compile_txt).on_hover_text("Compile the project to a .vhl package and save it").clicked() {
            self.compile_and_save_vhl();
        }
        let import_txt = if t1 { format!("{} Import VHL", icons::IMPORT) } else { icons::IMPORT.to_string() };
        if widgets::ghost_button(ui, &import_txt).on_hover_text("Open and decompile a .vhl package").clicked() {
            self.import_vhl_package();
        }
        let sync_txt = if t1 { format!("{} Sync to ESP32", icons::SYNC) } else { icons::SYNC.to_string() };
        if widgets::ghost_button(ui, &sync_txt).on_hover_text("Send the active project to the ESP32 via Wi-Fi").clicked() {
            self.sync_to_esp32();
        }

        ui.separator();

        // Master volume + real output meter
        if t1 {
            ui.label(caps(theme, "MASTER"));
        } else {
            ui.label(RichText::new(icons::SPEAKER_HIGH).size(15.0).color(theme.text_dim));
        }
        let slider_w = if t1 { 84.0 } else { 64.0 };
        if ui.add_sized([slider_w, 20.0], egui::Slider::new(&mut self.master_volume, 0.0..=100.0).show_value(false)).changed() {
            self.audio_player.set_master_volume(self.master_volume / 100.0);
        }
        if t1 {
            let (vu, _) = ui.allocate_exact_size(Vec2::new(46.0, 10.0), Sense::hover());
            let p = ui.painter_at(vu);
            p.rect_filled(vu, Rounding::same(3.0), theme.deep_well);
            let f = self.peak_meter.clamp(0.0, 1.0);
            let col = if self.peak_meter > 0.98 { theme.rec_red } else if self.peak_meter > 0.75 { theme.gold } else { theme.teal };
            p.rect_filled(Rect::from_min_size(vu.min, Vec2::new(vu.width() * f, vu.height())), Rounding::same(3.0), col);
        }

        // BPM + transpose
        ui.label(caps(theme, "BPM"));
        ui.add(egui::DragValue::new(&mut self.tempo_bpm).range(40..=240).speed(1));
        ui.label(caps(theme, if t1 { "TRANSPOSE" } else { "TRANS" }));
        ui.add(egui::DragValue::new(&mut self.transpose_semitones).range(-12..=12).suffix(" st"));

        if t0 {
            ui.label(mono(format!("{:.1} kHz", self.audio_player.host_rate() as f32 / 1000.0), 11.5, theme.text_dim));
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Status bar
    // ═══════════════════════════════════════════════════════════════════════
    fn ui_status(&mut self, ui: &mut Ui, theme: &AppTheme) {
        let dim = |s: &str| mono(s, 11.0, theme.text_dim);
        let w = ui.available_width();
        let mut restart_audio = false;
        ui.horizontal(|ui| {
            if self.audio_player.is_ok() {
                ui.label(mono(format!("Audio {:.1} kHz", self.audio_player.host_rate() as f32 / 1000.0), 11.0, theme.teal_text));
            } else if ui
                .add(egui::Label::new(RichText::new(format!("{} AUDIO OFF · click to restart", icons::WARNING)).monospace().size(11.0).color(theme.rec_red)).sense(Sense::click()))
                .on_hover_text("No audio output (device unplugged or stream error). Click to reopen the default output device.")
                .clicked()
            {
                restart_audio = true;
            }
            ui.separator();
            if w >= 1000.0 {
                ui.label(dim("ESP32-WROOM-32 · Xtensa dual-core 240 MHz"));
                ui.separator();
            }
            if w >= 1250.0 {
                ui.label(dim("Matrix 61 keys · 8x8 scan"));
                ui.separator();
            }

            let mut names: Vec<String> = Vec::new();
            for m in self.held_midi.iter().flatten().take(6) {
                names.push(format!("{}{}", NOTE_NAMES[(*m % 12) as usize], *m as i32 / 12 - 1));
            }
            let active = if names.is_empty() { "None".to_string() } else { names.join(" ") };
            ui.label(mono(format!("Active: {}", active), 11.0, theme.gold_text));

            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.label(dim(&format!("Voices {}", self.audio_player.voice_count())));
                if w >= 1100.0 {
                    ui.separator();
                    ui.label(dim("I2S DMA · 256 smp · 5.8 ms"));
                }
                if w >= 1000.0 {
                    ui.separator();
                    ui.label(dim(&format!("Theme: {}", if self.theme_mode == ThemeMode::Dark { "Dark" } else { "Light" })));
                }
            });
        });
        if restart_audio {
            self.audio_player.restart();
            self.patch_dirty = true;
            let ok = self.audio_player.is_ok();
            self.log(if ok { "[Audio Output] Reconnected.".to_string() } else { "[Audio Output] Still no output device.".to_string() });
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Dock: VHR tape deck + waterfall + keyboard
// ═══════════════════════════════════════════════════════════════════════════
impl HarmoniumApp {
    fn ui_dock(&mut self, ui: &mut Ui, theme: &AppTheme, now: f64, screen_h: f32) {
        let total_w = ui.available_width();
        let labelled = total_w >= 1400.0; // file buttons with text vs icon-only
        let is_playing = self.vhr_player.state == VhrPlayerState::Playing;
        let has_rec = self.vhr_player.recording.is_some();

        // Exact width budget: REC + 3 transport + timecode + scrub + speed + 5 file buttons (12 items, 11 gaps).
        let gap = 6.0;
        let (rec_w, tr_w, tc_w, speed_w) = (100.0, 82.0, 178.0, 104.0);
        let file_w = if labelled { 106.0 } else { 34.0 };
        let fixed = rec_w + 3.0 * tr_w + tc_w + speed_w + 5.0 * file_w + 11.0 * gap;
        let scrub_w = (total_w - fixed).max(60.0);

        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = gap;

            // REC / STOP REC (same width in both states)
            if !self.vhr_recorder.is_recording {
                if widgets::transport_button_w(ui, theme, TransportKind::Record, &format!("{} REC", icons::RECORD), true, false, rec_w).clicked() {
                    self.vhr_recorder.set_context(self.tempo_bpm as f32, self.transpose_semitones as i8, (self.master_volume as u8).min(100));
                    self.vhr_recorder.start();
                    self.log("[VHR] Recording... play some notes, then press STOP REC.");
                }
            } else if widgets::transport_button_w(ui, theme, TransportKind::Record, &format!("{} STOP REC", icons::STOP), true, true, rec_w).clicked() {
                let rec = self.vhr_recorder.stop();
                let count = rec.events.len();
                self.vhr_player.stop(&self.audio_player);
                self.vhr_player.load(rec);
                self.log(format!("[VHR] Recorded {} events.", count));
            }

            if widgets::transport_button_w(ui, theme, TransportKind::Play, &format!("{} PLAY", icons::PLAY), has_rec && !is_playing, false, tr_w).clicked() {
                self.vhr_player.play();
            }
            if widgets::transport_button_w(ui, theme, TransportKind::Neutral, &format!("{} PAUSE", icons::PAUSE), is_playing, false, tr_w).clicked() {
                self.vhr_player.pause();
                self.audio_player.all_notes_off();
                self.release_all_visual_notes(now);
            }
            if widgets::transport_button_w(ui, theme, TransportKind::Neutral, &format!("{} STOP", icons::STOP), has_rec, false, tr_w).clicked() {
                self.vhr_player.stop(&self.audio_player);
                self.release_all_visual_notes(now);
            }

            // Timecode
            let cur_s = self.vhr_player.current_position_ms as f32 / 1000.0;
            let tot_s = self.vhr_player.total_duration_ms as f32 / 1000.0;
            let ev_cur = self.vhr_player.cursor_idx;
            let ev_tot = self.vhr_player.recording.as_ref().map(|r| r.events.len()).unwrap_or(0);
            let (tc, _) = ui.allocate_exact_size(Vec2::new(tc_w, 40.0), Sense::hover());
            let p = ui.painter_at(tc);
            p.rect_filled(tc, Rounding::same(6.0), theme.deep_well);
            p.rect_stroke(tc, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));
            p.text(Pos2::new(tc.min.x + 12.0, tc.min.y + 14.0), Align2::LEFT_CENTER, format!("{:02}:{:06.3}", (cur_s / 60.0) as u32, cur_s % 60.0), FontId::monospace(20.0), theme.gold_text);
            p.text(Pos2::new(tc.min.x + 12.0, tc.max.y - 9.0), Align2::LEFT_CENTER, format!("/ {:.1} s · event {}/{}", tot_s, ev_cur, ev_tot), FontId::monospace(9.5), theme.text_dim);

            // Scrub bar (click / drag to seek)
            let (sb, resp_sb) = ui.allocate_exact_size(Vec2::new(scrub_w, 40.0), Sense::click_and_drag());
            let bar = Rect::from_min_size(Pos2::new(sb.min.x + 10.0, sb.center().y - 3.0), Vec2::new((sb.width() - 20.0).max(1.0), 6.0));
            if (resp_sb.dragged() || resp_sb.clicked()) && self.vhr_player.total_duration_ms > 0 {
                if let Some(pos) = resp_sb.interact_pointer_pos() {
                    let f = ((pos.x - bar.min.x) / bar.width()).clamp(0.0, 1.0);
                    let target_ms = (f * self.vhr_player.total_duration_ms as f32) as u32;
                    self.vhr_player.seek_to_ms(target_ms, &self.audio_player);
                    self.release_all_visual_notes(now);
                }
            }
            if resp_sb.hovered() && has_rec {
                ui.ctx().set_cursor_icon(CursorIcon::PointingHand);
            }
            let p = ui.painter_at(sb);
            p.rect_filled(sb, Rounding::same(6.0), theme.deep_well);
            p.rect_stroke(sb, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));
            let frac = if self.vhr_player.total_duration_ms > 0 { (self.vhr_player.current_position_ms as f32 / self.vhr_player.total_duration_ms as f32).clamp(0.0, 1.0) } else { 0.0 };
            p.rect_filled(bar, Rounding::same(3.0), theme.raised);
            p.rect_filled(Rect::from_min_size(bar.min, Vec2::new(bar.width() * frac, 6.0)), Rounding::same(3.0), theme.gold);
            let px = bar.min.x + bar.width() * frac;
            p.circle_filled(Pos2::new(px, bar.center().y), 6.0, theme.text);
            p.circle_stroke(Pos2::new(px, bar.center().y), 6.0, Stroke::new(1.5_f32, theme.gold));

            // Speed
            let (sp, _) = ui.allocate_exact_size(Vec2::new(speed_w, 40.0), Sense::hover());
            let mut spd = self.vhr_player.speed_multiplier;
            ui.allocate_new_ui(egui::UiBuilder::new().max_rect(sp), |ui| {
                Frame::none().fill(theme.deep_well).stroke(Stroke::new(1.0_f32, theme.hairline)).rounding(Rounding::same(6.0)).inner_margin(Margin::symmetric(10.0, 3.0)).show(ui, |ui| {
                    ui.set_width(speed_w - 22.0);
                    ui.spacing_mut().item_spacing.y = 0.0;
                    ui.label(caps(theme, "SPEED"));
                    let w = &mut ui.visuals_mut().widgets;
                    for s in [&mut w.inactive, &mut w.hovered, &mut w.active] {
                        s.bg_fill = Color32::TRANSPARENT;
                        s.weak_bg_fill = Color32::TRANSPARENT;
                        s.bg_stroke = Stroke::NONE;
                    }
                    if ui.add(egui::DragValue::new(&mut spd).range(0.25..=2.5).speed(0.01).suffix("x").fixed_decimals(2)).changed() {
                        self.vhr_player.set_speed(spd);
                    }
                });
            });

            // File buttons (5): Open / Save .vhr, Import / Export MIDI, Export WAV
            let label = |icon: &str, text: &str| if labelled { format!("{} {}", icon, text) } else { icon.to_string() };
            let btn = |ui: &mut Ui, txt: String, tip: &str| -> bool { ui.add(egui::Button::new(RichText::new(txt).size(12.5)).min_size(Vec2::new(file_w, 36.0))).on_hover_text(tip).clicked() };

            if btn(ui, label(icons::OPEN, "Open .vhr"), "Open a .vhr recording") {
                if let Some(path) = rfd::FileDialog::new().add_filter("VHR", &["vhr"]).pick_file() {
                    match std::fs::read(&path).map_err(|e| e.to_string()).and_then(|d| VhrDecompiler::decompile(&d)) {
                        Ok(rec) => {
                            self.vhr_player.stop(&self.audio_player);
                            self.vhr_player.load(rec);
                            self.log(format!("[VHR] Loaded {:?}", path.file_name().unwrap_or_default()));
                        }
                        Err(e) => self.log(format!("[ERROR] Could not open .vhr: {}", e)),
                    }
                }
            }
            if btn(ui, label(icons::SAVE, "Save .vhr"), "Save the recording as .vhr") {
                if let Some(rec) = &self.vhr_player.recording {
                    match VhrCompiler::compile(rec) {
                        Ok(bytes) => {
                            if let Some(path) = rfd::FileDialog::new().add_filter("VHR", &["vhr"]).set_file_name("performance.vhr").save_file() {
                                match std::fs::write(&path, &bytes) {
                                    Ok(()) => self.log(format!("[VHR] Saved {:?}", path)),
                                    Err(e) => self.log(format!("[ERROR] Could not write .vhr: {}", e)),
                                }
                            }
                        }
                        Err(e) => self.log(format!("[ERROR] VHR compile failed: {}", e)),
                    }
                } else {
                    self.log("[VHR] Nothing to save yet: press REC, play, then STOP REC.");
                }
            }
            if btn(ui, label(icons::IMPORT, "Import MIDI"), "Import a MIDI file as VHR") {
                if let Some(path) = rfd::FileDialog::new().add_filter("MIDI", &["mid", "midi"]).pick_file() {
                    let title = path.file_stem().and_then(|s| s.to_str()).unwrap_or("Imported").to_string();
                    match std::fs::read(&path).map_err(|e| e.to_string()).and_then(|d| MidiConverter::midi_to_vhr(&d, &title)) {
                        Ok(rec) => {
                            self.vhr_player.stop(&self.audio_player);
                            self.vhr_player.load(rec);
                            self.log(format!("[MIDI] Imported {:?} as VHR.", path.file_name().unwrap_or_default()));
                        }
                        Err(e) => self.log(format!("[ERROR] MIDI import failed: {}", e)),
                    }
                }
            }
            if btn(ui, label(icons::EXPORT, "Export MIDI"), "Export the recording as a standard MIDI file") {
                if let Some(rec) = &self.vhr_player.recording {
                    match MidiConverter::vhr_to_midi(rec) {
                        Ok(bytes) => {
                            if let Some(path) = rfd::FileDialog::new().add_filter("MIDI", &["mid"]).set_file_name("performance.mid").save_file() {
                                match std::fs::write(&path, &bytes) {
                                    Ok(()) => self.log(format!("[MIDI] Exported {:?}", path)),
                                    Err(e) => self.log(format!("[ERROR] Could not write MIDI: {}", e)),
                                }
                            }
                        }
                        Err(e) => self.log(format!("[ERROR] MIDI export failed: {}", e)),
                    }
                } else {
                    self.log("[MIDI] No recording loaded to export.");
                }
            }
            if btn(ui, label(icons::WAV, "Export WAV"), "Render the performance to a 16-bit 44.1 kHz stereo .wav (uses the current sounds)") {
                if let Some(rec) = &self.vhr_player.recording {
                    let wav = self.audio_player.render_vhr_to_wav(rec);
                    if let Some(path) = rfd::FileDialog::new().add_filter("WAV Audio", &["wav"]).set_file_name("performance.wav").save_file() {
                        match std::fs::write(&path, &wav) {
                            Ok(()) => self.log(format!("[EXPORT] Rendered performance to WAV ({} bytes)", wav.len())),
                            Err(e) => self.log(format!("[ERROR] Could not write WAV: {}", e)),
                        }
                    }
                } else {
                    self.log("[EXPORT] No recording loaded to export.");
                }
            }
        });

        ui.add_space(10.0);

        // ── Waterfall, zone strip and keys share one geometry ─────────────────
        let kb_h = (screen_h * 0.13).clamp(84.0, 140.0);
        let wf_h = (screen_h * 0.10).clamp(48.0, 110.0);
        ui.spacing_mut().item_spacing.y = 0.0;

        let zones = self.key_zones(theme);
        if self.show_note_waterfall {
            VisualPainters::draw_note_waterfall(ui, theme, &self.waterfall, now, &zones, wf_h);
        }
        // One lane per region (overlapping regions get their own lane), aligned with the keys below.
        let bars = self.region_bars(theme);
        let lane_h = if screen_h < 800.0 { 13.0 } else { 16.0 };
        let lanes = VisualPainters::draw_region_lanes(ui, theme, &bars, lane_h, 3, false);
        if let Some(i) = lanes.select {
            self.select_region(i);
        }

        let mut sounding = [false; KEY_COUNT];
        for (i, m) in self.held_midi.iter().enumerate() {
            sounding[i] = m.is_some();
        }
        let pressed = VisualPainters::draw_keyboard(ui, theme, kb_h, &zones, &sounding);
        self.handle_mouse_key(now, pressed);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Library (left)
// ═══════════════════════════════════════════════════════════════════════════
#[derive(Default)]
struct SampleRowAction {
    select: bool,
    audition: bool,
    delete: bool,
    assign: bool,
}

impl HarmoniumApp {
    fn ui_library(&mut self, ui: &mut Ui, theme: &AppTheme) {
        // Activity log, pinned to the bottom
        egui::TopBottomPanel::bottom("activity_log")
            .exact_height(104.0)
            .resizable(false)
            .frame(Frame::none().fill(theme.window).stroke(Stroke::new(1.0_f32, theme.hairline)).inner_margin(Margin::symmetric(12.0, 8.0)))
            .show_inside(ui, |ui| {
                ui.label(caps(theme, "ACTIVITY"));
                ui.add_space(2.0);
                egui::ScrollArea::vertical().auto_shrink([false, false]).stick_to_bottom(true).show(ui, |ui| {
                    ui.spacing_mut().item_spacing.y = 1.0;
                    for line in &self.console_logs {
                        let col = if line.contains("[ERROR]") { theme.rec_red } else { theme.text_dim };
                        ui.add(egui::Label::new(mono(line, 10.5, col)).wrap());
                    }
                });
            });

        Frame::none().inner_margin(Margin { left: 10.0, right: 10.0, top: 10.0, bottom: 0.0 }).show(ui, |ui| {
            ui.add(egui::TextEdit::singleline(&mut self.asset_search_query).hint_text(format!("{}  Filter assets", icons::SEARCH)).desired_width(f32::INFINITY));
        });
        ui.add_space(4.0);

        let q = self.asset_search_query.to_lowercase();
        let show = |s: &str| q.is_empty() || s.to_lowercase().contains(&q);

        egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            Frame::none().inner_margin(Margin::symmetric(8.0, 0.0)).show(ui, |ui| {
                ui.spacing_mut().item_spacing.y = 2.0;
                let group = |ui: &mut Ui, title: &str, count: Option<usize>, add: &mut dyn FnMut(&mut Ui)| {
                    let right = count.map(|n| n.to_string()).unwrap_or_default();
                    widgets::section(ui, theme, title, title, &right, true, |ui| add(ui));
                };

                // SAMPLES -----------------------------------------------------
                let mut import_samples = false;
                let mut select_smp: Option<usize> = None;
                let mut audition_smp: Option<usize> = None;
                let mut delete_smp: Option<usize> = None;
                let mut assign_smp: Option<usize> = None;
                let active_region = self.active_region_idx;
                group(ui, "SAMPLES", Some(self.project.samples.len()), &mut |ui| {
                    if ui.add(egui::Button::new(RichText::new(format!("{} Import Sample...", icons::PLUS)).size(11.5))).on_hover_text("Import WAV files (the first one is assigned to the selected region)").clicked() {
                        import_samples = true;
                    }
                    ui.add_space(3.0);
                    let can_del = self.project.samples.len() > 1;
                    for (i, s) in self.project.samples.iter().enumerate() {
                        if show(&s.name) {
                            let used = self.project.regions.iter().any(|r| r.sample_id as usize == i);
                            let meta = format!("{}{}", if used { "" } else { "unused · " }, fmt_bytes(s.pcm_frames.len() * 2));
                            let a = sample_asset_row(ui, theme, i, &s.name, &meta, i == self.active_sample_idx, can_del, active_region);
                            if a.select {
                                select_smp = Some(i);
                            }
                            if a.audition {
                                audition_smp = Some(i);
                            }
                            if a.delete {
                                delete_smp = Some(i);
                            }
                            if a.assign {
                                assign_smp = Some(i);
                            }
                        }
                    }
                });
                if import_samples {
                    self.import_wav_samples_dialog();
                }
                if let Some(i) = select_smp {
                    self.active_sample_idx = i;
                    self.wave_gen += 1;
                }
                if let Some(i) = assign_smp {
                    let r = self.active_region_idx;
                    self.assign_sample(r, i);
                    self.log(format!("[REGION] Region {} now plays sample {} \"{}\".", r, i, self.project.samples[i].name));
                }
                if let Some(i) = audition_smp {
                    self.active_sample_idx = i;
                    self.wave_gen += 1;
                    self.audition_sample(i);
                }
                if let Some(i) = delete_smp {
                    self.delete_sample(i);
                }

                // INSTRUMENTS ---------------------------------------------------
                group(ui, "INSTRUMENTS", Some(self.project.instruments.len()), &mut |ui| {
                    for (idx, inst) in self.project.instruments.iter().enumerate() {
                        let label = format!("{} · {}", inst.instrument_id, instrument_name(inst));
                        if show(&label) {
                            let rc = inst.region_count; // copy out of the packed struct (a reference to it would be unaligned)
                            tree_row(ui, theme, Self::inst_color(theme, Some(idx)), &label, &format!("{} rgn", rc), false, theme.text_dim);
                        }
                    }
                });

                // REGIONS ---------------------------------------------------------
                let mut select_reg: Option<usize> = None;
                group(ui, "REGIONS", Some(self.project.regions.len()), &mut |ui| {
                    for (idx, r) in self.project.regions.iter().enumerate() {
                        let rname = self.region_name(idx);
                        let label = format!("R{} {} · {}-{}", idx, rname, key_note_name(r.key_lo), key_note_name(r.key_hi));
                        if show(&label) {
                            let owner = self.region_owner(idx);
                            let meta = owner.map(|o| instrument_name(&self.project.instruments[o])).unwrap_or_default();
                            if tree_row(ui, theme, Self::region_color(idx), &label, &meta, idx == self.active_region_idx, theme.text_dim).clicked() {
                                select_reg = Some(idx);
                            }
                        }
                    }
                });
                if let Some(idx) = select_reg {
                    self.select_region(idx);
                }

                // VHR RECORDINGS ----------------------------------------------------
                let vhr_count = if self.vhr_player.recording.is_some() { 1 } else { 0 };
                group(ui, "VHR RECORDINGS", Some(vhr_count), &mut |ui| {
                    if let Some(rec) = &self.vhr_player.recording {
                        if show(&rec.title) {
                            tree_row(ui, theme, theme.gold, &rec.title, &format!("{} ev", rec.events.len()), false, theme.gold_text);
                        }
                    } else {
                        ui.horizontal(|ui| {
                            ui.add_space(22.0);
                            ui.label(RichText::new("No recordings loaded").size(11.5).color(theme.text_dim));
                        });
                    }
                });

                // TOOLS -----------------------------------------------------------------
                let mut open_learner = false;
                let mut import_wav = false;
                let mut import_vhl = false;
                group(ui, "TOOLS", None, &mut |ui| {
                    if tree_row(ui, theme, theme.text_dim, &format!("{}  Note Learner (Ragas)", icons::BOOK_OPEN), "", false, theme.text_dim).clicked() {
                        open_learner = true;
                    }
                    if tree_row(ui, theme, theme.text_dim, &format!("{}  Import WAV sample…", icons::IMPORT), "", false, theme.text_dim).clicked() {
                        import_wav = true;
                    }
                    if tree_row(ui, theme, theme.text_dim, &format!("{}  Import VHL package…", icons::OPEN), "", false, theme.text_dim).clicked() {
                        import_vhl = true;
                    }
                });
                if open_learner {
                    self.show_note_learner_modal = true;
                }
                if import_wav {
                    self.import_wav_samples_dialog();
                }
                if import_vhl {
                    self.import_vhl_package();
                }
            });
        });
    }

    /// Play a sample once at its root key, regardless of regions (the ▶ button in the library).
    fn audition_sample(&mut self, sample_idx: usize) {
        // The audition rides on the normal engine: it needs a region that points at this sample, so use a
        // temporary one covering key 0 only and play that key.
        if let Some(s) = self.project.samples.get(sample_idx) {
            let root = s.root_key.min(127);
            let mut tmp = self.project.clone();
            if let Some(mut r) = tmp.regions.get(self.active_region_idx).cloned() {
                r.sample_id = sample_idx as u16;
                r.key_lo = 0;
                r.key_hi = 0;
                r.vel_lo = 0;
                r.vel_hi = 127;
                r.loop_mode = VHL_LOOP_NONE;
                tmp.regions.push(r);
            }
            // Build an audition patch that contains every real region plus the temporary one, trigger it,
            // then the normal patch is restored by the dirty flag below once the note has been started.
            if self.pcm_dirty || self.engine_pcm.len() != tmp.samples.len() {
                self.engine_pcm = tmp.samples.iter().map(|s| Arc::new(mono_pcm(s))).collect();
                self.pcm_dirty = false;
            }
            self.audio_player.set_patch(Patch::from_project(&tmp, &self.engine_pcm));
            self.audio_player.note_on(0, root, 110);
            self.patch_dirty = true; // restore the real patch at the end of the frame (the voice keeps its own sample copy)
        }
    }
}

fn fmt_bytes(n: usize) -> String {
    if n < 1024 {
        format!("{} B", n)
    } else if n < 1024 * 1024 {
        format!("{} KB", n / 1024)
    } else {
        format!("{:.1} MB", n as f32 / 1048576.0)
    }
}

/// One sample asset row: audition button, name, size, delete. Right-click for "Assign to Region N".
fn sample_asset_row(ui: &mut Ui, theme: &AppTheme, idx: usize, label: &str, meta: &str, selected: bool, can_delete: bool, active_region: usize) -> SampleRowAction {
    let total_w = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(Vec2::new(total_w, 24.0), Sense::hover());
    let mut act = SampleRowAction::default();

    if ui.is_rect_visible(rect) {
        // The whole row is one clickable area (also gives the right-click menu); buttons sit on top of it.
        let row_resp = ui.interact(rect, ui.id().with(("smp_row", idx)), Sense::click());
        let p = ui.painter();
        if selected {
            p.rect_filled(rect, Rounding::same(5.0), AppTheme::alpha(theme.teal, 38));
        } else if row_resp.hovered() {
            p.rect_filled(rect, Rounding::same(5.0), AppTheme::alpha(theme.text, 12));
        }

        let aud_rect = Rect::from_center_size(Pos2::new(rect.min.x + 12.0, rect.center().y), Vec2::new(18.0, 18.0));
        let aud_resp = ui.interact(aud_rect, ui.id().with(("smp_aud", idx)), Sense::click()).on_hover_text("Audition");
        if aud_resp.hovered() {
            p.rect_filled(aud_rect, Rounding::same(3.0), AppTheme::alpha(theme.gold, 50));
        }
        p.text(aud_rect.center(), Align2::CENTER_CENTER, icons::PLAY, FontId::proportional(11.0), if aud_resp.hovered() { theme.gold_text } else { theme.text_dim });
        if aud_resp.clicked() {
            act.audition = true;
        }

        let mut right_reserve = 6.0;
        if can_delete {
            let del_rect = Rect::from_center_size(Pos2::new(rect.max.x - 12.0, rect.center().y), Vec2::new(16.0, 16.0));
            let del_resp = ui.interact(del_rect, ui.id().with(("smp_del", idx)), Sense::click()).on_hover_text("Remove sample");
            if del_resp.hovered() {
                p.rect_filled(del_rect, Rounding::same(3.0), AppTheme::alpha(theme.rec_red, 60));
            }
            p.text(del_rect.center(), Align2::CENTER_CENTER, icons::X, FontId::proportional(10.0), if del_resp.hovered() { theme.rec_red } else { theme.text_dim });
            if del_resp.clicked() {
                act.delete = true;
            }
            right_reserve += 20.0;
        }

        let meta_w = if meta.is_empty() { 0.0 } else { p.layout_no_wrap(meta.to_string(), FontId::monospace(10.5), theme.text_dim).size().x + 8.0 };
        if !meta.is_empty() {
            p.text(Pos2::new(rect.max.x - right_reserve, rect.center().y), Align2::RIGHT_CENTER, meta, FontId::monospace(10.5), theme.text_dim);
        }
        let name_rect = Rect::from_min_max(Pos2::new(rect.min.x + 24.0, rect.min.y), Pos2::new(rect.max.x - right_reserve - meta_w, rect.max.y));
        ui.painter_at(name_rect).text(Pos2::new(name_rect.min.x, rect.center().y), Align2::LEFT_CENTER, label, FontId::proportional(12.0), if selected { theme.teal_text } else { theme.text });

        if row_resp.clicked() {
            act.select = true;
        }
        row_resp.context_menu(|ui| {
            if ui.button(format!("Assign to Region {}", active_region)).clicked() {
                act.assign = true;
                ui.close_menu();
            }
        });
    }
    act
}

/// One tree row: coloured dot, name, right-aligned meta. Returns the click response.
fn tree_row(ui: &mut Ui, theme: &AppTheme, dot: Color32, label: &str, meta: &str, selected: bool, meta_color: Color32) -> egui::Response {
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 22.0), Sense::click());
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        if selected {
            p.rect_filled(rect, Rounding::same(5.0), AppTheme::alpha(theme.teal, 38));
        } else if resp.hovered() {
            p.rect_filled(rect, Rounding::same(5.0), AppTheme::alpha(theme.text, 12));
        }
        p.rect_filled(Rect::from_center_size(Pos2::new(rect.min.x + 9.0, rect.center().y), Vec2::splat(7.0)), Rounding::same(2.0), dot);

        let meta_w = if meta.is_empty() { 0.0 } else { p.layout_no_wrap(meta.to_string(), FontId::monospace(10.5), meta_color).size().x + 10.0 };
        let clipped = ui.painter_at(Rect::from_min_max(rect.min, Pos2::new(rect.max.x - meta_w - 4.0, rect.max.y)));
        clipped.text(Pos2::new(rect.min.x + 22.0, rect.center().y), Align2::LEFT_CENTER, label, FontId::proportional(12.5), if selected { theme.teal_text } else { theme.text });
        if !meta.is_empty() {
            p.text(Pos2::new(rect.max.x - 6.0, rect.center().y), Align2::RIGHT_CENTER, meta, FontId::monospace(10.5), meta_color);
        }
    }
    resp
}

// ═══════════════════════════════════════════════════════════════════════════
// Inspector (right): one region at a time, so it works for any number of regions
// ═══════════════════════════════════════════════════════════════════════════
const LABEL_W: f32 = 100.0;

impl HarmoniumApp {
    fn ui_inspector(&mut self, ui: &mut Ui, theme: &AppTheme) {
        let n = self.project.regions.len();
        let mut do_add = false;
        let mut do_select: Option<usize> = None;
        let mut do_delete: Option<usize> = None;
        let mut arm_delete: Option<usize> = None;

        ui.horizontal(|ui| {
            ui.vertical(|ui| {
                ui.spacing_mut().item_spacing.y = 1.0;
                ui.label(RichText::new("Parameter inspector").strong().size(14.0).color(theme.text));
                ui.add(egui::Label::new(RichText::new("Edits the selected region").size(11.0).color(theme.text_dim)).truncate());
            });
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let can_add = n < 10;
                let add_btn = egui::Button::new(RichText::new(format!("{} Add Region ({}/10)", icons::PLUS, n)).size(12.0));
                if ui.add_enabled(can_add, add_btn)
                    .on_hover_text(if can_add { "Add a copy of the selected region" } else { "Maximum limit of 10 regions reached" })
                    .clicked() {
                    do_add = true;
                }
            });
        });
        ui.add_space(8.0);

        // Region selector chips (wrap onto more rows when there are many regions)
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing = Vec2::new(6.0, 6.0);
            for i in 0..n {
                let text = format!("R{} {}", i, self.region_name(i));
                let w = (text.chars().count() as f32 * 6.6 + 30.0).clamp(54.0, 160.0);
                let (r, resp) = ui.allocate_exact_size(Vec2::new(w, 26.0), Sense::click());
                let active = i == self.active_region_idx;
                let p = ui.painter();
                let col = Self::region_color(i);
                p.rect(r, Rounding::same(13.0), if active { AppTheme::alpha(col, 40) } else if resp.hovered() { theme.raised } else { Color32::TRANSPARENT }, Stroke::new(1.0_f32, if active { col } else { theme.hairline }));
                p.circle_filled(Pos2::new(r.min.x + 12.0, r.center().y), 3.5, col);
                ui.painter_at(r).text(Pos2::new(r.min.x + 22.0, r.center().y), Align2::LEFT_CENTER, text, FontId::proportional(11.5), if active { theme.text } else { theme.text_dim });
                if resp.clicked() {
                    do_select = Some(i);
                }
            }
        });
        ui.add_space(6.0);

        let ri = self.active_region_idx.min(n.saturating_sub(1));
        if n > 0 {
            ui.horizontal(|ui| {
                let col = Self::region_color(ri);
                let (r, _) = ui.allocate_exact_size(Vec2::splat(10.0), Sense::hover());
                ui.painter().rect_filled(r, Rounding::same(2.5), col);
                ui.label(RichText::new(format!("REGION {}", ri)).strong().size(12.0).color(col));

                while self.project.region_names.len() <= ri {
                    self.project.region_names.push(self.region_name(self.project.region_names.len()));
                }
                let mut cur_name = self.project.region_names[ri].clone();
                let name_resp = ui.add(egui::TextEdit::singleline(&mut cur_name).desired_width(140.0).hint_text("Region Name..."));
                if name_resp.changed() {
                    self.project.region_names[ri] = cur_name;
                    self.patch_dirty = true;
                }

                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if n > 1 {
                        let armed = self.pending_delete_region == Some(ri);
                        let txt = if armed { format!("{} Click again to delete", icons::TRASH) } else { format!("{} Delete", icons::TRASH) };
                        let b = egui::Button::new(RichText::new(txt).size(11.5).color(if armed { Color32::WHITE } else { theme.text_dim })).fill(if armed { theme.rec_red } else { Color32::TRANSPARENT });
                        if ui.add(b).on_hover_text("Delete this region").clicked() {
                            if armed {
                                do_delete = Some(ri);
                            } else {
                                arm_delete = Some(ri);
                            }
                        }
                    }
                });
            });
            ui.add_space(4.0);
        }

        let sample_names: Vec<String> = self.project.samples.iter().map(|s| s.name.clone()).collect();
        let mut changed = false;
        let mut new_sample: Option<usize> = None;

        if n > 0 {
            egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                ui.spacing_mut().item_spacing.y = 4.0;
                let reg = &mut self.project.regions[ri];

                section(ui, theme, "ROUTING", true, |ui| {
                    // Which sample this region plays: every region has its own.
                    changed |= prow(ui, theme, "Sample", |ui, w| {
                        let mut s_id = (reg.sample_id as usize).min(sample_names.len().saturating_sub(1));
                        let cur = sample_names.get(s_id).cloned().unwrap_or_else(|| "—".to_string());
                        let before = s_id;
                        egui::ComboBox::from_id_salt(("reg_sample_combo", ri)).selected_text(cur).width(w - 8.0).show_ui(ui, |ui| {
                            for (idx, name) in sample_names.iter().enumerate() {
                                ui.selectable_value(&mut s_id, idx, format!("{}: {}", idx, name));
                            }
                        });
                        if s_id != before {
                            new_sample = Some(s_id);
                            true
                        } else {
                            false
                        }
                    });
                    changed |= prow(ui, theme, "Key range", |ui, w| range_pair(ui, w, &mut reg.key_lo, &mut reg.key_hi, RangeKind::Note));
                    // MIDI range = the pitches this region *sounds at*. Key range says which keys trigger it; this row
                    // says what pitch those keys produce, so two regions can play different pitch ranges.
                    // Stored as the region's semitone offset (coarse tune), which the engine and the ESP32 both honour.
                    changed |= prow(ui, theme, "MIDI range", |ui, w| {
                        let span = (reg.key_hi - reg.key_lo) as i32;
                        let base = reg.key_lo as i32 + 36;
                        let before = reg.coarse_tune as i32;
                        let (mut lo, mut hi) = (base + before, base + before + span);
                        let (olo, ohi) = (lo, hi);
                        ui.horizontal(|ui| {
                            ui.spacing_mut().item_spacing.x = 3.0;
                            let cw = ((w - 16.0) / 2.0).max(30.0);
                            let v = ui.visuals_mut();
                            v.widgets.inactive.bg_fill = v.extreme_bg_color;
                            v.widgets.inactive.weak_bg_fill = v.extreme_bg_color;
                            v.widgets.inactive.bg_stroke = Stroke::new(1.0_f32, v.widgets.noninteractive.bg_stroke.color);
                            ui.style_mut().override_font_id = Some(FontId::monospace(12.0));
                            ui.add_sized([cw, 22.0], egui::DragValue::new(&mut lo).range(0..=(127 - span).max(0)).speed(0.2));
                            ui.label("–");
                            ui.add_sized([cw, 22.0], egui::DragValue::new(&mut hi).range(span..=127).speed(0.2));
                        });
                        if hi != ohi {
                            lo = hi - span; // editing either end moves the whole range (one key = one semitone)
                        }
                        let ch = lo != olo;
                        if ch {
                            reg.coarse_tune = (lo - base).clamp(-127, 127) as i8;
                        }
                        ch
                    });
                    {
                        let (kl, kh, shift) = (reg.key_lo, reg.key_hi, reg.coarse_tune as i32);
                        let (pl, ph) = ((kl as i32 + 36 + shift).clamp(0, 127) as u8, (kh as i32 + 36 + shift).clamp(0, 127) as u8);
                        ui.horizontal(|ui| {
                            ui.add_space(LABEL_W + 6.0);
                            ui.label(mono(format!("keys {}–{} sound as {}–{} ({:+} st)", key_note_name(kl), key_note_name(kh), midi_note_name(pl), midi_note_name(ph), shift), 10.5, theme.text_dim));
                        });
                    }
                    changed |= prow(ui, theme, "Velocity range", |ui, w| range_pair(ui, w, &mut reg.vel_lo, &mut reg.vel_hi, RangeKind::Vel));
                    changed |= prow(ui, theme, "Attenuation", |ui, w| {
                        let mut att = reg.attenuation_cb as f32 / 10.0;
                        let ch = drag(ui, w, &mut att, 0.0..=96.0, " dB");
                        if ch {
                            reg.attenuation_cb = (att * 10.0).round() as u16;
                        }
                        ch
                    });
                    changed |= prow(ui, theme, "Pan", |ui, w| {
                        let mut pan = reg.pan;
                        let ch = drag(ui, w, &mut pan, -500..=500, "");
                        if ch {
                            reg.pan = pan;
                        }
                        ch
                    });
                    changed |= prow(ui, theme, "Loop playback", |ui, w| {
                        let mut mode = reg.loop_mode;
                        egui::ComboBox::from_id_salt(("loop_mode", ri))
                            .selected_text(match mode {
                                0 => "No loop",
                                1 => "Continuous",
                                _ => "While held",
                            })
                            .width(w - 8.0)
                            .show_ui(ui, |ui| {
                                ui.selectable_value(&mut mode, 0, "No loop");
                                ui.selectable_value(&mut mode, 1, "Continuous");
                                ui.selectable_value(&mut mode, 3, "While held");
                            });
                        let ch = mode != reg.loop_mode;
                        reg.loop_mode = mode;
                        ch
                    });
                    changed |= prow(ui, theme, "Reverb send", |ui, w| {
                        let mut v = reg.reverb_send;
                        let ch = drag(ui, w, &mut v, 0..=100, " %");
                        reg.reverb_send = v;
                        ch
                    });
                    changed |= prow(ui, theme, "Chorus send", |ui, w| {
                        let mut v = reg.chorus_send;
                        let ch = drag(ui, w, &mut v, 0..=100, " %");
                        reg.chorus_send = v;
                        ch
                    });
                });

                section(ui, theme, "TUNING", true, |ui| {
                    changed |= prow(ui, theme, "Semitones", |ui, w| {
                        let mut v = reg.coarse_tune as i32;
                        let ch = drag(ui, w, &mut v, -96..=96, "");
                        reg.coarse_tune = v.clamp(-127, 127) as i8;
                        ch
                    });
                    changed |= prow(ui, theme, "Cents", |ui, w| {
                        let mut v = reg.fine_tune as i32;
                        let ch = drag(ui, w, &mut v, -100..=100, " c");
                        reg.fine_tune = v as i16;
                        ch
                    });
                    changed |= prow(ui, theme, "Scale tuning", |ui, w| {
                        let mut v = reg.scale_tuning as i32;
                        let ch = drag(ui, w, &mut v, 0..=200, "");
                        reg.scale_tuning = v as u8;
                        ch
                    });
                });

                section(ui, theme, "FILTER · LIVE", true, |ui| {
                    changed |= prow(ui, theme, "Cutoff", |ui, w| {
                        let mut v = reg.filter.cutoff_hz as f32;
                        let ch = drag(ui, w, &mut v, 100.0..=12000.0, " Hz");
                        reg.filter.cutoff_hz = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Resonance", |ui, w| {
                        let mut v = reg.filter.resonance_cb as f32 / 10.0;
                        let ch = drag(ui, w, &mut v, 0.0..=18.0, " dB");
                        reg.filter.resonance_cb = (v * 10.0).round() as u16;
                        ch
                    });
                });

                section(ui, theme, "VOL ENVELOPE · LIVE", false, |ui| {
                    changed |= prow(ui, theme, "Delay", |ui, w| {
                        let mut v = reg.vol_env.delay_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=1000.0, " ms");
                        reg.vol_env.delay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Attack", |ui, w| {
                        let mut v = reg.vol_env.attack_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=1000.0, " ms");
                        reg.vol_env.attack_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Hold", |ui, w| {
                        let mut v = reg.vol_env.hold_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=500.0, " ms");
                        reg.vol_env.hold_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Decay", |ui, w| {
                        let mut v = reg.vol_env.decay_ms as f32;
                        let ch = drag(ui, w, &mut v, 10.0..=2000.0, " ms");
                        reg.vol_env.decay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Sustain", |ui, w| {
                        let mut v = (reg.vol_env.sustain_level as f32 / 10.0).clamp(0.0, 100.0);
                        let ch = drag(ui, w, &mut v, 0.0..=100.0, " %");
                        reg.vol_env.sustain_level = (v * 10.0).round() as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Release", |ui, w| {
                        let mut v = reg.vol_env.release_ms as f32;
                        let ch = drag(ui, w, &mut v, 5.0..=2000.0, " ms");
                        reg.vol_env.release_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Key → Hold", |ui, w| {
                        let mut v = reg.vol_env.key_to_hold as i32;
                        let ch = drag(ui, w, &mut v, -120..=120, " c/k");
                        reg.vol_env.key_to_hold = v.clamp(-128, 127) as i8;
                        ch
                    });
                    changed |= prow(ui, theme, "Key → Decay", |ui, w| {
                        let mut v = reg.vol_env.key_to_decay as i32;
                        let ch = drag(ui, w, &mut v, -120..=120, " c/k");
                        reg.vol_env.key_to_decay = v.clamp(-128, 127) as i8;
                        ch
                    });
                });

                section(ui, theme, "MOD ENVELOPE · LIVE", false, |ui| {
                    changed |= prow(ui, theme, "Delay", |ui, w| {
                        let mut v = reg.mod_env.delay_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=1000.0, " ms");
                        reg.mod_env.delay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Attack", |ui, w| {
                        let mut v = reg.mod_env.attack_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=1000.0, " ms");
                        reg.mod_env.attack_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Hold", |ui, w| {
                        let mut v = reg.mod_env.hold_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=500.0, " ms");
                        reg.mod_env.hold_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Decay", |ui, w| {
                        let mut v = reg.mod_env.decay_ms as f32;
                        let ch = drag(ui, w, &mut v, 10.0..=2000.0, " ms");
                        reg.mod_env.decay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Sustain", |ui, w| {
                        let mut v = (reg.mod_env.sustain_level as f32 / 10.0).clamp(0.0, 100.0);
                        let ch = drag(ui, w, &mut v, 0.0..=100.0, " %");
                        reg.mod_env.sustain_level = (v * 10.0).round() as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Release", |ui, w| {
                        let mut v = reg.mod_env.release_ms as f32;
                        let ch = drag(ui, w, &mut v, 5.0..=2000.0, " ms");
                        reg.mod_env.release_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Key → Hold", |ui, w| {
                        let mut v = reg.mod_env.key_to_hold as i32;
                        let ch = drag(ui, w, &mut v, -120..=120, " c/k");
                        reg.mod_env.key_to_hold = v.clamp(-128, 127) as i8;
                        ch
                    });
                    changed |= prow(ui, theme, "Key → Decay", |ui, w| {
                        let mut v = reg.mod_env.key_to_decay as i32;
                        let ch = drag(ui, w, &mut v, -120..=120, " c/k");
                        reg.mod_env.key_to_decay = v.clamp(-128, 127) as i8;
                        ch
                    });
                    changed |= prow(ui, theme, "Env → Pitch", |ui, w| {
                        let mut v = reg.mod_env_to_pitch as i32;
                        let ch = drag(ui, w, &mut v, -12000..=12000, " c");
                        reg.mod_env_to_pitch = v as i16;
                        ch
                    });
                    changed |= prow(ui, theme, "Env → Filter", |ui, w| {
                        let mut v = reg.mod_env_to_filter as i32;
                        let ch = drag(ui, w, &mut v, -12000..=12000, " c");
                        reg.mod_env_to_filter = v as i16;
                        ch
                    });
                });

                section(ui, theme, "MOD LFO · LIVE", false, |ui| {
                    changed |= prow(ui, theme, "Delay", |ui, w| {
                        let mut v = reg.lfo_mod.delay_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=2000.0, " ms");
                        reg.lfo_mod.delay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Frequency", |ui, w| {
                        let mut v = reg.lfo_mod.freq_mHz as f32 / 1000.0;
                        let ch = drag(ui, w, &mut v, 0.1..=20.0, " Hz");
                        reg.lfo_mod.freq_mHz = (v * 1000.0).round() as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "LFO → Pitch", |ui, w| {
                        let mut v = reg.lfo_mod.to_pitch_cb as i32;
                        let ch = drag(ui, w, &mut v, -1200..=1200, " c");
                        reg.lfo_mod.to_pitch_cb = v as i16;
                        ch
                    });
                    changed |= prow(ui, theme, "LFO → Filter", |ui, w| {
                        let mut v = reg.lfo_mod.to_filter_cb as i32;
                        let ch = drag(ui, w, &mut v, -12000..=12000, " c");
                        reg.lfo_mod.to_filter_cb = v as i16;
                        ch
                    });
                    changed |= prow(ui, theme, "LFO → Volume", |ui, w| {
                        let mut v = reg.lfo_mod.to_volume_cb as f32 / 10.0;
                        let ch = drag(ui, w, &mut v, -24.0..=24.0, " dB");
                        reg.lfo_mod.to_volume_cb = (v * 10.0).round() as i16;
                        ch
                    });
                });

                section(ui, theme, "VIB LFO · LIVE", false, |ui| {
                    changed |= prow(ui, theme, "Delay", |ui, w| {
                        let mut v = reg.lfo_vib.delay_ms as f32;
                        let ch = drag(ui, w, &mut v, 0.0..=2000.0, " ms");
                        reg.lfo_vib.delay_ms = v as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Frequency", |ui, w| {
                        let mut v = reg.lfo_vib.freq_mHz as f32 / 1000.0;
                        let ch = drag(ui, w, &mut v, 0.1..=20.0, " Hz");
                        reg.lfo_vib.freq_mHz = (v * 1000.0).round() as u16;
                        ch
                    });
                    changed |= prow(ui, theme, "Vib → Pitch", |ui, w| {
                        let mut v = reg.lfo_vib.to_pitch_cb as i32;
                        let ch = drag(ui, w, &mut v, -1200..=1200, " c");
                        reg.lfo_vib.to_pitch_cb = v as i16;
                        ch
                    });
                });
            });
        }

        // Apply what happened this frame (never while a region list is being drawn)
        if let Some(s) = new_sample {
            self.assign_sample(ri, s);
        }
        if changed {
            self.pull_region_params();
            self.patch_dirty = true;
        }
        if let Some(i) = do_select {
            self.select_region(i);
        }
        if do_add {
            self.add_region();
        }
        if let Some(i) = arm_delete {
            self.pending_delete_region = Some(i);
        }
        if let Some(i) = do_delete {
            self.delete_region(i);
        }
    }
}

/// Inspector section (flat, no indent).
fn section(ui: &mut Ui, theme: &AppTheme, title: &str, open: bool, add: impl FnOnce(&mut Ui)) {
    widgets::section(ui, theme, title, title, "", open, |ui| {
        ui.spacing_mut().item_spacing.y = 4.0;
        add(ui);
        ui.add_space(4.0);
    });
}

/// One inspector row: fixed-width label, then a control that gets the rest of the width.
fn prow(ui: &mut Ui, theme: &AppTheme, label: &str, add: impl FnOnce(&mut Ui, f32) -> bool) -> bool {
    let mut changed = false;
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 6.0;
        let (r, _) = ui.allocate_exact_size(Vec2::new(LABEL_W, 24.0), Sense::hover());
        ui.painter_at(r).text(r.left_center(), Align2::LEFT_CENTER, label, FontId::proportional(12.5), AppTheme::alpha(theme.text, 200));
        let w = ui.available_width().max(60.0);
        changed = add(ui, w);
    });
    changed
}

#[derive(Clone, Copy, PartialEq)]
enum RangeKind {
    Note, // key index shown as a note name (C2..C7)
    Vel,
}

/// Two linked number fields (low – high) with low <= high enforced. Key ranges are stored as key index 0..60.
fn range_pair(ui: &mut Ui, w: f32, lo: &mut u8, hi: &mut u8, kind: RangeKind) -> bool {
    let off: u8 = if kind == RangeKind::Vel { 0 } else { 36 };
    let max: u8 = if kind == RangeKind::Vel { 127 } else { 96 };
    let (mut a, mut b) = (lo.saturating_add(off), hi.saturating_add(off));
    let (ia, ib) = (a, b);
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 3.0;
        let cw = ((w - 16.0) / 2.0).max(30.0);
        let v = ui.visuals_mut();
        v.widgets.inactive.bg_fill = v.extreme_bg_color;
        v.widgets.inactive.weak_bg_fill = v.extreme_bg_color;
        v.widgets.inactive.bg_stroke = Stroke::new(1.0_f32, v.widgets.noninteractive.bg_stroke.color);
        ui.style_mut().override_font_id = Some(FontId::monospace(12.0));
        for (val, is_hi) in [(&mut a, false), (&mut b, true)] {
            let mut dv = egui::DragValue::new(val).range(off..=max).speed(0.2);
            if kind == RangeKind::Note {
                dv = dv.custom_formatter(|n, _| midi_note_name(n as u8)).custom_parser(|s| parse_note(s).map(|m| m as f64));
            }
            ui.add_sized([cw, 22.0], dv);
            if !is_hi {
                ui.label("–");
            }
        }
    });
    if a != ia && a > b {
        b = a;
    }
    if b != ib && b < a {
        a = b;
    }
    let changed = a != ia || b != ib;
    if changed {
        *lo = a - off;
        *hi = b - off;
    }
    changed
}

/// MIDI note number -> "C#3" (C2 = 36, the left-most key of the 61-key board).
fn midi_note_name(m: u8) -> String {
    format!("{}{}", NOTE_NAMES[(m % 12) as usize], m as i32 / 12 - 1)
}

/// Key index (0..60) -> note name.
fn key_note_name(k: u8) -> String {
    midi_note_name(k.saturating_add(36))
}

/// "c#3", "Db4", "C2" or a plain MIDI number -> MIDI note.
fn parse_note(s: &str) -> Option<u8> {
    let t = s.trim();
    if let Ok(n) = t.parse::<i32>() {
        return Some(n.clamp(0, 127) as u8);
    }
    let mut ch = t.chars();
    let base = match ch.next()?.to_ascii_uppercase() {
        'C' => 0,
        'D' => 2,
        'E' => 4,
        'F' => 5,
        'G' => 7,
        'A' => 9,
        'B' => 11,
        _ => return None,
    };
    let rest: String = ch.collect();
    let (acc, oct_s) = if let Some(r) = rest.strip_prefix('#') {
        (1, r)
    } else if let Some(r) = rest.strip_prefix('b') {
        (-1, r)
    } else {
        (0, rest.as_str())
    };
    let oct: i32 = oct_s.trim().parse().ok()?;
    Some(((oct + 1) * 12 + base + acc).clamp(0, 127) as u8)
}

/// Number field in a dark well, fixed width.
fn drag<N: egui::emath::Numeric>(ui: &mut Ui, w: f32, val: &mut N, range: std::ops::RangeInclusive<N>, suffix: &str) -> bool {
    let v = ui.visuals_mut();
    v.widgets.inactive.bg_fill = v.extreme_bg_color;
    v.widgets.inactive.weak_bg_fill = v.extreme_bg_color;
    v.widgets.inactive.bg_stroke = Stroke::new(1.0_f32, v.widgets.noninteractive.bg_stroke.color);
    ui.style_mut().override_font_id = Some(FontId::monospace(12.0));
    ui.add_sized([w.max(30.0), 22.0], egui::DragValue::new(val).range(range).suffix(suffix)).changed()
}

// ═══════════════════════════════════════════════════════════════════════════
// Workspace (centre)
// ═══════════════════════════════════════════════════════════════════════════
impl HarmoniumApp {
    fn ui_workspace(&mut self, ui: &mut Ui, theme: &AppTheme) {
        ui.spacing_mut().item_spacing.y = 12.0;

        // 1. Sample Waveform & Loop Editor
        let s_idx = self.active_sample_idx.min(self.project.samples.len().saturating_sub(1));
        let s_name = self.project.samples.get(s_idx).map(|s| s.name.clone()).unwrap_or_default();
        let wf_title = if s_name.is_empty() { "SAMPLE WAVEFORM & LOOP EDITOR".to_string() } else { format!("SAMPLE WAVEFORM & LOOP EDITOR · {} [Sample #{}]", s_name, s_idx) };
        let users: Vec<usize> = self.project.regions.iter().enumerate().filter(|(_, r)| r.sample_id as usize == s_idx).map(|(i, _)| i).collect();
        let mut wf_sync = false;
        widgets::section(ui, theme, "workspace_waveform", &wf_title, "", true, |ui| {
            if let Some(s) = self.project.samples.get_mut(s_idx) {
                let mut r_key = s.root_key;
                let mut l_start = s.loop_start_frame;
                let mut l_end = s.loop_end_frame;
                let wave_cache = &mut self.wave_cache;
                let wave_gen = self.wave_gen;
                let changed = widgets::card(ui, theme, |ui| {
                    let used = if users.is_empty() { "Not used by any region · assign it in the inspector (Sample)".to_string() } else { format!("Played by region {}", users.iter().map(|i| format!("R{}", i)).collect::<Vec<_>>().join(", ")) };
                    ui.label(mono(used, 10.5, if users.is_empty() { theme.coral_text } else { theme.text_dim }));
                    ui.add_space(4.0);
                    VisualPainters::draw_waveform(ui, theme, wave_cache, wave_gen, &s.name, &mut r_key, s.sample_rate, &s.pcm_frames, &mut l_start, &mut l_end, s.channels)
                });
                if changed {
                    s.root_key = r_key;
                    s.loop_start_frame = l_start;
                    s.loop_end_frame = l_end;
                    wf_sync = true;
                }
            }
        });
        if wf_sync {
            self.patch_dirty = true;
        }

        // 2. Envelope + Filter DSP Rack (edits the selected region)
        let reg_idx = self.active_region_idx.min(self.project.regions.len().saturating_sub(1));
        let dsp_title = format!("DSP SYNTHESIS RACK (DAHDSR & BIQUAD FILTER) · REGION #{}", reg_idx);
        let host_rate = self.audio_player.host_rate() as f32;
        widgets::section(ui, theme, "workspace_dsp", &dsp_title, "", true, |ui| {
            ui.columns(2, |cols| {
                widgets::card(&mut cols[0], theme, |ui| {
                    if VisualPainters::draw_adsr_curve(
                        ui,
                        theme,
                        &mut self.adsr_layer,
                        &mut self.vol_delay_ms,
                        &mut self.attack_ms,
                        &mut self.hold_ms,
                        &mut self.decay_ms,
                        &mut self.sustain_percent,
                        &mut self.release_ms,
                        &mut self.mod_delay_ms,
                        &mut self.mod_attack_ms,
                        &mut self.mod_hold_ms,
                        &mut self.mod_decay_ms,
                        &mut self.mod_sustain_percent,
                        &mut self.mod_release_ms,
                    ) {
                        if let Some(r) = self.project.regions.get_mut(reg_idx) {
                            r.vol_env.delay_ms = self.vol_delay_ms as u16;
                            r.vol_env.attack_ms = self.attack_ms as u16;
                            r.vol_env.hold_ms = self.hold_ms as u16;
                            r.vol_env.decay_ms = self.decay_ms as u16;
                            r.vol_env.sustain_level = ((self.sustain_percent / 100.0) * 1000.0).round() as u16;
                            r.vol_env.release_ms = self.release_ms as u16;

                            r.mod_env.delay_ms = self.mod_delay_ms as u16;
                            r.mod_env.attack_ms = self.mod_attack_ms as u16;
                            r.mod_env.hold_ms = self.mod_hold_ms as u16;
                            r.mod_env.decay_ms = self.mod_decay_ms as u16;
                            r.mod_env.sustain_level = ((self.mod_sustain_percent / 100.0) * 1000.0).round() as u16;
                            r.mod_env.release_ms = self.mod_release_ms as u16;
                        }
                        self.patch_dirty = true;
                    }
                });
                widgets::card(&mut cols[1], theme, |ui| {
                    if VisualPainters::draw_filter_response(ui, theme, &mut self.cutoff_hz, &mut self.resonance_db, host_rate) {
                        if let Some(r) = self.project.regions.get_mut(reg_idx) {
                            r.filter.cutoff_hz = self.cutoff_hz as u16;
                            r.filter.resonance_cb = (self.resonance_db * 10.0).round() as u16;
                        }
                        self.patch_dirty = true;
                    }
                });
            });
        });

        // 3. Region map: every region as a bar over the keyboard range
        let bars = self.region_bars(theme);
        let mut map_ev = crate::visuals::LaneEvent::default();
        widgets::section(ui, theme, "workspace_regions", "KEYBOARD REGION MAP", &format!("{} regions", bars.len()), true, |ui| {
            widgets::card(ui, theme, |ui| {
                ui.label(RichText::new("One bar per region. Click a bar to select it, drag its edges to change the key range. Bars on separate rows overlap, so those regions sound together (layers).").size(11.5).color(theme.text_dim));
                ui.add_space(6.0);
                map_ev = VisualPainters::draw_region_lanes(ui, theme, &bars, 22.0, 12, true);
                ui.add_space(4.0);
                let uncovered = (0..KEY_COUNT).filter(|&k| !self.project.regions.iter().any(|r| k as u8 >= r.key_lo && k as u8 <= r.key_hi)).count();
                if uncovered > 0 {
                    ui.label(mono(format!("{} keys have no region and will be silent (shown grey on the keyboard).", uncovered), 10.5, theme.coral_text));
                }
            });
        });
        if let Some(i) = map_ev.select {
            if i != self.active_region_idx {
                self.select_region(i);
            }
        }
        if let Some((i, lo, hi)) = map_ev.range {
            if let Some(r) = self.project.regions.get_mut(i) {
                r.key_lo = lo;
                r.key_hi = hi;
            }
            self.patch_dirty = true;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Keyboard helpers
// ═══════════════════════════════════════════════════════════════════════════
const NOTE_NAMES: [&str; 12] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

fn laptop_key_to_piano_key(key: egui::Key, shift: bool) -> Option<u8> {
    use egui::Key::*;
    let oct_base: u8 = if shift { 24 } else { 0 };
    let semi: Option<u8> = match key {
        Z => Some(0),
        S => Some(1),
        X => Some(2),
        D => Some(3),
        C => Some(4),
        V => Some(5),
        G => Some(6),
        B => Some(7),
        H => Some(8),
        N => Some(9),
        J => Some(10),
        M => Some(11),
        Q => Some(12),
        Num2 => Some(13),
        W => Some(14),
        Num3 => Some(15),
        E => Some(16),
        R => Some(17),
        Num5 => Some(18),
        T => Some(19),
        Num6 => Some(20),
        Y => Some(21),
        Num7 => Some(22),
        U => Some(23),
        I => Some(24),
        _ => None,
    };
    semi.map(|s| (oct_base + s).min(60))
}