use eframe::egui;
use egui::{Color32, Pos2, Rect, Rounding, Stroke, Vec2};
use crate::theme::ModernRagaTheme;
use crate::vhl::types::*;
use crate::vhl::compiler::{VhlCompiler, VhlProject, VhlSample};
use crate::vhl::decompiler::VhlDecompiler;

#[derive(PartialEq)]
pub enum ActiveTab {
    PerformanceKeyboard,
    SynthesisEnvelopes,
    VhlVhrToolchain,
    EducationalVisualizer,
}

pub struct HarmoniumApp {
    // Navigation
    pub current_tab: ActiveTab,

    // Active VHL Project State (Single Source of Truth)
    pub project: VhlProject,
    pub last_compiled_vhl: Option<Vec<u8>>,

    // Performance & Keyboard State
    pub master_volume: f32,
    pub tempo_bpm: u32,
    pub transpose_semitones: i32,
    pub split_key_index: u8, // Key 0..60 where left zone ends and right begins
    pub last_clicked_key: Option<u8>,
    pub test_audition_count: u32,

    // Envelope Parameters (DAHDSR)
    pub attack_ms: f32,
    pub hold_ms: f32,
    pub decay_ms: f32,
    pub sustain_percent: f32,
    pub release_ms: f32,

    // Filter Parameters
    pub cutoff_hz: f32,
    pub resonance_db: f32,

    // Network & ESP32 status
    pub esp32_ip: String,
    pub is_connected_wifi: bool,

    // Build log
    pub console_logs: Vec<String>,

    // Laptop keyboard keys currently pressed
    pub pressed_laptop_keys: std::collections::HashSet<egui::Key>,

    // Real-time Audio Synthesizer Engine
    pub audio_player: crate::audio_engine::AudioPlayer,

    // Parameter inspection table modal toggle
    pub show_param_table: bool,
}

impl Default for HarmoniumApp {
    fn default() -> Self {
        let mut project = VhlProject::default();
        project.build_tag = "HarmoniumStudio V2\0".to_string();

        // 1. Create Default Piano Instrument (ID 0)
        let mut p_name = [0u8; VHL_NAME_LEN];
        p_name[..5].copy_from_slice(b"Piano");
        project.instruments.push(VHLInstrumentEntry_v2 {
            instrument_id: 0,
            bank: 0,
            program: 0,
            max_polyphony: 6,
            name: p_name,
            region_offset: 0,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 95,
            pan_default: -25,
            reverb_send_pct: 20,
            chorus_send_pct: 0,
            flags: 0,
            _reserved: [0u8; 7],
        });

        // 2. Create Default Harmonium Instrument (ID 1)
        let mut h_name = [0u8; VHL_NAME_LEN];
        h_name[..9].copy_from_slice(b"Harmonium");
        project.instruments.push(VHLInstrumentEntry_v2 {
            instrument_id: 1,
            bank: 0,
            program: 1,
            max_polyphony: 8,
            name: h_name,
            region_offset: 1,
            region_count: 1,
            lookup_table_offset: 0,
            master_volume_pct: 90,
            pan_default: 25,
            reverb_send_pct: 35,
            chorus_send_pct: 15,
            flags: 0,
            _reserved: [0u8; 7],
        });

        // 3. Create Default Regions
        let piano_env = VHLEnvelope {
            delay_ms: 0,
            attack_ms: 5,
            hold_ms: 0,
            decay_ms: 800,
            sustain_level: 200,
            release_ms: 60,
            key_to_hold: 0,
            key_to_decay: 0,
        };

        let harm_env = VHLEnvelope {
            delay_ms: 0,
            attack_ms: 25,
            hold_ms: 10,
            decay_ms: 150,
            sustain_level: 900,
            release_ms: 80,
            key_to_hold: 0,
            key_to_decay: 0,
        };

        let filter = VHLFilter {
            cutoff_hz: 4800,
            resonance_cb: 25,
        };

        let dummy_lfo_mod = VHLModLFO {
            delay_ms: 0,
            freq_mHz: 5000,
            to_pitch_cb: 0,
            to_filter_cb: 0,
            to_volume_cb: 0,
            _reserved: 0,
        };

        let dummy_lfo_vib = VHLVibLFO {
            delay_ms: 0,
            freq_mHz: 6000,
            to_pitch_cb: 0,
            _reserved: 0,
        };

        // Piano Region (Keys 0..127)
        project.regions.push(VHLRegion_v2 {
            key_lo: 0,
            key_hi: 127,
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
            pan: -250,
            reverb_send: 20,
            chorus_send: 0,
            vol_env: piano_env,
            mod_env: piano_env,
            mod_env_to_pitch: 0,
            mod_env_to_filter: 0,
            lfo_mod: dummy_lfo_mod,
            lfo_vib: dummy_lfo_vib,
            filter,
            _reserved_c: 0,
        });

        // Harmonium Region (Keys 0..127)
        project.regions.push(VHLRegion_v2 {
            key_lo: 0,
            key_hi: 127,
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
            pan: 250,
            reverb_send: 35,
            chorus_send: 15,
            vol_env: harm_env,
            mod_env: harm_env,
            mod_env_to_pitch: 0,
            mod_env_to_filter: 0,
            lfo_mod: dummy_lfo_mod,
            lfo_vib: dummy_lfo_vib,
            filter,
            _reserved_c: 0,
        });

        // 4. Default Harmonium Waveform Sample (Band-limited Harmonium Reed synthesis)
        let sample_rate = 44100;
        let num_frames = 441; // 100 Hz fundamental at 44.1 kHz
        let mut reed_pcm = Vec::with_capacity(num_frames);
        for i in 0..num_frames {
            let phase = (i as f32) / (num_frames as f32) * std::f32::consts::TAU;
            // Rich harmonic blend (odd + even harmonics characteristic of brass reed)
            let val = (phase.sin() * 0.5
                + (phase * 2.0).sin() * 0.25
                + (phase * 3.0).sin() * 0.15
                + (phase * 4.0).sin() * 0.08) * 26000.0;
            reed_pcm.push(val as i16);
        }

        project.samples.push(VhlSample {
            name: "Harmonium_C3_Reed".to_string(),
            root_key: 48,
            sample_rate,
            fine_tune_cents: 0,
            loop_start_frame: 0,
            loop_end_frame: (num_frames - 1) as u32,
            channels: 1,
            pcm_frames: reed_pcm,
        });

            let audio_player = crate::audio_engine::AudioPlayer::new();
            if let Some(s) = project.samples.first() {
                if let Some(r) = project.regions.get(1) {
                    audio_player.sync_from_project(&s.pcm_frames, s.root_key, s.loop_start_frame, s.loop_end_frame, &r.vol_env, 4800.0, 2.5, 0.8);
                }
            }

            Self {
                current_tab: ActiveTab::PerformanceKeyboard,
                project,
                last_compiled_vhl: None,
                audio_player,
                show_param_table: false,
                pressed_laptop_keys: std::collections::HashSet::new(),
                master_volume: 80.0,
                tempo_bpm: 120,
                transpose_semitones: 0,
                split_key_index: 24, // Split at C4
                last_clicked_key: None,
                test_audition_count: 0,

                attack_ms: 25.0,
                hold_ms: 10.0,
                decay_ms: 150.0,
                sustain_percent: 90.0,
                release_ms: 80.0,

                cutoff_hz: 4800.0,
                resonance_db: 2.5,

                esp32_ip: "192.168.4.1".to_string(),
                is_connected_wifi: false,

                console_logs: vec![
                    "[SYSTEM] Harmonium Workstation Initialized.".to_string(),
                    "[Audio Output] Real-time audio engine connected (Host DAC / Bluetooth).".to_string(),
                    "[VHL v2.0] Default Project ready: 2 Instruments (Piano & Harmonium), 61 Keys mapped.".to_string(),
                    "[Audio DSP] Target: 44.1 kHz, 16-bit Stereo I²S (PCM5102A).".to_string(),
                ],
            }
    }
}

impl HarmoniumApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        ModernRagaTheme::apply(&cc.egui_ctx);
        Self::default()
    }

    fn log(&mut self, msg: impl Into<String>) {
        let timestamp = chrono_like_now();
        self.console_logs.push(format!("[{}] {}", timestamp, msg.into()));
    }
}

fn chrono_like_now() -> &'static str {
    "LOG"
}

impl eframe::App for HarmoniumApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // ── Laptop Keyboard → Piano Mapping (Polyphone Layout) ──────────────
        // Process all key events this frame so notes start on press and stop on release.
        // We must collect events before borrowing self mutably for audio calls.
        let key_events: Vec<(egui::Key, bool, egui::Modifiers)> = ctx.input(|i| {
            i.events.iter().filter_map(|e| {
                if let egui::Event::Key { key, pressed, repeat, modifiers, .. } = e {
                    if !repeat { Some((*key, *pressed, *modifiers)) } else { None }
                } else {
                    None
                }
            }).collect()
        });

        for (key, pressed, modifiers) in key_events {
            if let Some(key_idx) = laptop_key_to_piano_key(key, modifiers.shift) {
                if pressed {
                    // Only fire note_on once (first press, not repeat)
                    if self.pressed_laptop_keys.insert(key) {
                        let note_midi = (key_idx as i32 + 36 + self.transpose_semitones).clamp(0, 127) as u8;
                        self.audio_player.note_on(key_idx, note_midi, 110);
                        self.last_clicked_key = Some(key_idx);
                        self.log(format!(
                            "Keyboard: Key '{:?}' → Piano #{}, MIDI {} ({}{})",
                            key, key_idx, note_midi,
                            NOTE_NAMES[(note_midi % 12) as usize],
                            note_midi / 12
                        ));
                    }
                } else {
                    self.pressed_laptop_keys.remove(&key);
                    self.audio_player.note_off(key_idx);
                    if self.last_clicked_key == Some(key_idx) {
                        self.last_clicked_key = None;
                    }
                }
            }
        }

        // Keep repainting while keys are held so the key highlight stays visible
        if !self.pressed_laptop_keys.is_empty() {
            ctx.request_repaint();
        }

        // Top Ribbon Header
        egui::TopBottomPanel::top("top_panel")
            .frame(egui::Frame::none()
                .fill(ModernRagaTheme::PANEL_SURFACE)
                .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
                .inner_margin(egui::Margin::symmetric(14.0, 10.0)))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    // Logo / Title
                    ui.vertical(|ui| {
                        ui.heading(
                            egui::RichText::new("HARMONIUM WORKSTATION")
                                .color(ModernRagaTheme::PEACOCK_TEAL)
                                .strong(),
                        );
                        ui.label(
                            egui::RichText::new("ESP32 DSP Synthesis • 61-Key Revival Studio")
                                .color(ModernRagaTheme::TEXT_MUTED)
                                .small(),
                        );
                    });

                    ui.add_space(20.0);
                    ui.separator();
                    ui.add_space(15.0);

                    // Navigation Tabs
                    if ui.selectable_label(self.current_tab == ActiveTab::PerformanceKeyboard, "🎹 61-Key Split & Keys").clicked() {
                        self.current_tab = ActiveTab::PerformanceKeyboard;
                    }
                    if ui.selectable_label(self.current_tab == ActiveTab::SynthesisEnvelopes, "🎛️ Synthesis & ADSR").clicked() {
                        self.current_tab = ActiveTab::SynthesisEnvelopes;
                    }
                    if ui.selectable_label(self.current_tab == ActiveTab::VhlVhrToolchain, "📦 VHL / VHR Compiler").clicked() {
                        self.current_tab = ActiveTab::VhlVhrToolchain;
                    }
                    if ui.selectable_label(self.current_tab == ActiveTab::EducationalVisualizer, "📖 Note Learner").clicked() {
                        self.current_tab = ActiveTab::EducationalVisualizer;
                    }

                    ui.add_space(8.0);
                    if ui.selectable_label(self.show_param_table, "📊 Parameter Table").clicked() {
                        self.show_param_table = !self.show_param_table;
                    }

                    ui.add_space(15.0);
                    ui.separator();
                    ui.add_space(10.0);

                    // Master Volume
                    ui.label(egui::RichText::new("Vol:").small().color(ModernRagaTheme::TEXT_MUTED));
                    ui.add(egui::Slider::new(&mut self.master_volume, 0.0..=100.0).show_value(false));

                    // Tempo BPM
                    ui.label(egui::RichText::new("BPM:").small().color(ModernRagaTheme::TEXT_MUTED));
                    ui.add(egui::DragValue::new(&mut self.tempo_bpm).range(40..=240).speed(1));

                    // Transpose
                    ui.label(egui::RichText::new("Semi:").small().color(ModernRagaTheme::TEXT_MUTED));
                    ui.add(egui::DragValue::new(&mut self.transpose_semitones).range(-12..=12).speed(1));

                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        // ESP32 Wi-Fi connection indicator pill
                        let (wifi_color, wifi_text) = if self.is_connected_wifi {
                            (ModernRagaTheme::EMERALD_ACTIVE, "ESP32 Online")
                        } else {
                            (ModernRagaTheme::CORAL_VERMILLION, "ESP32 Offline (Wi-Fi)")
                        };

                        egui::Frame::none()
                            .fill(ModernRagaTheme::CARD_SURFACE)
                            .stroke(Stroke::new(1.0_f32, wifi_color))
                            .rounding(Rounding::same(12.0))
                            .inner_margin(egui::Margin::symmetric(10.0, 4.0))
                            .show(ui, |ui| {
                                ui.label(egui::RichText::new(wifi_text).color(wifi_color).small().strong());
                            });

                        ui.add_space(8.0);
                        ui.label(egui::RichText::new("44.1 kHz / I²S DMA").color(ModernRagaTheme::TEXT_MUTED).small());
                    });
                });
            });

        // Bottom Diagnostics & Status Bar
        egui::TopBottomPanel::bottom("bottom_status_panel")
            .frame(egui::Frame::none()
                .fill(ModernRagaTheme::BG_DEEP)
                .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
                .inner_margin(egui::Margin::symmetric(12.0, 6.0)))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label(egui::RichText::new("Target: ESP32-WROOM-32 (Xtensa Dual-Core 240MHz)").color(ModernRagaTheme::TEXT_MUTED).small());
                    ui.separator();
                    ui.label(egui::RichText::new("Matrix: 61-Keys (8x8 Scan)").color(ModernRagaTheme::TEXT_MUTED).small());
                    ui.separator();
                    ui.label(egui::RichText::new(format!("Active Note: {}", 
                        self.last_clicked_key.map_or("None".to_string(), |k| format!("Key #{} (MIDI {})", k, k + 36))))
                    .color(ModernRagaTheme::MARIGOLD_GOLD).small());

                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.label(egui::RichText::new("Theme: Modern Raga Studio").color(ModernRagaTheme::PEACOCK_TEAL).small());
                    });
                });
            });

        // Central Workspace Area
        egui::CentralPanel::default()
            .frame(egui::Frame::none().fill(ModernRagaTheme::BG_DEEP).inner_margin(egui::Margin::same(16.0)))
            .show(ctx, |ui| {
                // Render Active Tab
                match self.current_tab {
                    ActiveTab::PerformanceKeyboard => self.render_keyboard_tab(ui),
                    ActiveTab::SynthesisEnvelopes => self.render_synthesis_tab(ui),
                    ActiveTab::VhlVhrToolchain => self.render_toolchain_tab(ui),
                    ActiveTab::EducationalVisualizer => self.render_educational_tab(ui),
                }

                // Render Parameter Inspection Table Window if toggled
                if self.show_param_table {
                    self.render_parameter_table_window(ctx);
                }
            });
    }
}

impl HarmoniumApp {
    /// Render Tab 1: 61-Key Split & Interactive Keyboard
    fn render_keyboard_tab(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.heading(egui::RichText::new("61-Key Zone Management & Live Preview").color(ModernRagaTheme::TEXT_PRIMARY));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button(egui::RichText::new("🔊 Audition C4 Note").strong()).clicked() {
                    self.test_audition_count += 1;
                    let count = self.test_audition_count;
                    // Trigger C4 (MIDI 60)
                    self.audio_player.note_on(24, 60, 110);
                    self.log(format!("Audition triggered! Playing C4 (MIDI 60), Test count = {}", count));
                }
            });
        });

        ui.add_space(8.0);

        // Split Configuration Card
        egui::Frame::none()
            .fill(ModernRagaTheme::CARD_SURFACE)
            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
            .rounding(Rounding::same(6.0))
            .inner_margin(egui::Margin::same(12.0))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label(egui::RichText::new("Keyboard Split Point:").strong());
                    let mut split_val = self.split_key_index as i32;
                    if ui.add(egui::Slider::new(&mut split_val, 0..=60).text("Key Index")).changed() {
                        self.split_key_index = split_val as u8;
                        let k = self.split_key_index;
                        // Update VHL key mapping table
                        for i in 0..VHL_TOTAL_PHYSICAL_KEYS {
                            if (i as u8) <= k {
                                self.project.key_map[i].zone_id = 0; // Zone 0 = Left (Piano)
                                self.project.key_map[i].assigned_inst_id = 0;
                            } else {
                                self.project.key_map[i].zone_id = 1; // Zone 1 = Right (Harmonium)
                                self.project.key_map[i].assigned_inst_id = 1;
                            }
                        }
                        self.log(format!("VHL Keybed updated: Keys 0..{} -> Piano (Inst 0), Keys {}..60 -> Harmonium (Inst 1)", k, k + 1));
                    }

                    ui.separator();

                    // Left Zone Info
                    ui.colored_label(ModernRagaTheme::PEACOCK_TEAL, format!("■ Left Zone (Keys 0..{}): Accompaniment / Piano", self.split_key_index));
                    // Right Zone Info
                    ui.colored_label(ModernRagaTheme::CORAL_VERMILLION, format!("■ Right Zone (Keys {}..60): Lead Harmonium", self.split_key_index + 1));
                });
            });

        ui.add_space(16.0);

        // 61-Key Interactive Virtual Keyboard Canvas
        ui.horizontal(|ui| {
            ui.label(egui::RichText::new("Interactive 61-Key Virtual Bed (Click to play pitch-shifted audio on Laptop/Bluetooth):").small().color(ModernRagaTheme::TEXT_MUTED));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button(egui::RichText::new("⏹ All Notes Off").small()).clicked() {
                    for k in 0..61 {
                        self.audio_player.note_off(k);
                    }
                    self.last_clicked_key = None;
                }
            });
        });
        ui.add_space(4.0);

        let available_width = ui.available_width().max(800.0);
        let keyboard_height = 140.0;
        let (rect, response) = ui.allocate_exact_size(Vec2::new(available_width, keyboard_height), egui::Sense::click_and_drag());

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect);

            // Background frame
            painter.rect_filled(rect, Rounding::same(6.0), Color32::from_rgb(15, 17, 21));
            painter.rect_stroke(rect, Rounding::same(6.0), Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR));

            // 61 keys span 5 full octaves (C2 to C7)
            // 36 White keys total
            let white_key_count = 36;
            let white_key_width = (available_width - 12.0) / (white_key_count as f32);
            let white_key_height = keyboard_height - 12.0;
            let start_x = rect.min.x + 6.0;
            let start_y = rect.min.y + 6.0;

            // Mapping for 61 keys
            let is_black_key = [
                false, true, false, true, false, false, true, false, true, false, true, false, // Octave 1
                false, true, false, true, false, false, true, false, true, false, true, false, // Octave 2
                false, true, false, true, false, false, true, false, true, false, true, false, // Octave 3
                false, true, false, true, false, false, true, false, true, false, true, false, // Octave 4
                false, true, false, true, false, false, true, false, true, false, true, false, // Octave 5
                false // 61st key (C7)
            ];

            let mut white_key_indices = Vec::new();
            let mut black_key_indices = Vec::new();

            for (idx, &is_black) in is_black_key.iter().enumerate() {
                if is_black {
                    black_key_indices.push(idx as u8);
                } else {
                    white_key_indices.push(idx as u8);
                }
            }

            // Draw White Keys first
            for (w_pos, &key_idx) in white_key_indices.iter().enumerate() {
                let k_x = start_x + (w_pos as f32) * white_key_width;
                let k_rect = Rect::from_min_size(Pos2::new(k_x, start_y), Vec2::new(white_key_width - 1.5, white_key_height));

                // Zone coloring
                let is_active = self.last_clicked_key == Some(key_idx);
                let key_fill = if is_active {
                    ModernRagaTheme::MARIGOLD_GOLD
                } else if key_idx <= self.split_key_index {
                    Color32::from_rgb(235, 243, 245) // Subtle teal tint
                } else {
                    Color32::from_rgb(248, 240, 240) // Subtle coral tint
                };

                painter.rect_filled(k_rect, Rounding::same(3.0), key_fill);
                painter.rect_stroke(k_rect, Rounding::same(3.0), Stroke::new(1.0_f32, Color32::from_rgb(180, 190, 200)));

                // Check click and play real-time audio
                if response.clicked() || response.dragged() {
                    if let Some(pos) = response.interact_pointer_pos() {
                        if k_rect.contains(pos) && self.last_clicked_key != Some(key_idx) {
                            self.last_clicked_key = Some(key_idx);
                            let note_midi = (key_idx as i32 + 36 + self.transpose_semitones).clamp(0, 127) as u8;
                            self.audio_player.note_on(key_idx, note_midi, 100);
                            self.log(format!("Key Pressed: #{}, MIDI {} -> Playing Audio", key_idx, note_midi));
                        }
                    }
                }
            }

            // Draw Black Keys on top
            let black_key_width = white_key_width * 0.65;
            let black_key_height = white_key_height * 0.62;

            for &key_idx in &black_key_indices {
                let preceding_whites = is_black_key[0..key_idx as usize].iter().filter(|&&b| !b).count();
                let b_x = start_x + (preceding_whites as f32) * white_key_width - (black_key_width * 0.5);
                let b_rect = Rect::from_min_size(Pos2::new(b_x, start_y), Vec2::new(black_key_width, black_key_height));

                let is_active = self.last_clicked_key == Some(key_idx);
                let b_fill = if is_active {
                    ModernRagaTheme::MARIGOLD_GOLD
                } else if key_idx <= self.split_key_index {
                    Color32::from_rgb(10, 40, 48) // Dark teal
                } else {
                    Color32::from_rgb(48, 15, 20) // Dark coral
                };

                painter.rect_filled(b_rect, Rounding::same(2.0), b_fill);
                painter.rect_stroke(b_rect, Rounding::same(2.0), Stroke::new(1.0_f32, Color32::from_rgb(80, 90, 100)));

                if response.clicked() || response.dragged() {
                    if let Some(pos) = response.interact_pointer_pos() {
                        if b_rect.contains(pos) && self.last_clicked_key != Some(key_idx) {
                            self.last_clicked_key = Some(key_idx);
                            let note_midi = (key_idx as i32 + 36 + self.transpose_semitones).clamp(0, 127) as u8;
                            self.audio_player.note_on(key_idx, note_midi, 100);
                            self.log(format!("Black Key Pressed: #{}, MIDI {} -> Playing Audio", key_idx, note_midi));
                        }
                    }
                }
            }
        }

        ui.add_space(16.0);

        // ── Laptop Keyboard Hint Legend ──────────────────────────────────────
        egui::Frame::none()
            .fill(ModernRagaTheme::CARD_SURFACE)
            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
            .rounding(Rounding::same(6.0))
            .inner_margin(egui::Margin::same(10.0))
            .show(ui, |ui| {
                ui.label(egui::RichText::new("⌨ Laptop Keyboard → Piano Mapping (Polyphone Layout)").strong().color(ModernRagaTheme::MARIGOLD_GOLD));
                ui.add_space(4.0);
                ui.horizontal_wrapped(|ui| {
                    ui.spacing_mut().item_spacing.x = 6.0;
                    // Octave 1 (MIDI 36-47) - no shift
                    for (key, note) in [("Z","C2"),("S","C#"),("X","D"),("D","D#"),("C","E"),("V","F"),
                                        ("G","F#"),("B","G"),("H","G#"),("N","A"),("J","A#"),("M","B")] {
                        egui::Frame::none()
                            .fill(Color32::from_rgb(28, 35, 42))
                            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
                            .rounding(Rounding::same(4.0))
                            .inner_margin(egui::Margin::symmetric(6.0, 3.0))
                            .show(ui, |ui| {
                                ui.label(egui::RichText::new(format!("{}\n{}", key, note)).monospace().small().color(ModernRagaTheme::TEXT_MUTED));
                            });
                    }
                    ui.add_space(10.0);
                    // Octave 2 (MIDI 48-59) - no shift
                    for (key, note) in [("Q","C3"),("2","C#"),("W","D"),("3","D#"),("E","E"),("R","F"),
                                        ("5","F#"),("T","G"),("6","G#"),("Y","A"),("7","A#"),("U","B")] {
                        egui::Frame::none()
                            .fill(Color32::from_rgb(28, 35, 42))
                            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
                            .rounding(Rounding::same(4.0))
                            .inner_margin(egui::Margin::symmetric(6.0, 3.0))
                            .show(ui, |ui| {
                                ui.label(egui::RichText::new(format!("{}\n{}", key, note)).monospace().small().color(ModernRagaTheme::PEACOCK_TEAL));
                            });
                    }
                });
                ui.add_space(3.0);
                ui.label(egui::RichText::new("Hold Shift for Octave 3 (Shift+Z..M) and Octave 4 (Shift+Q..U) | Sound starts on press, stops on release").small().color(ModernRagaTheme::TEXT_MUTED));
            });


        ui.label(egui::RichText::new("Workstation Activity Log:").strong());
        egui::Frame::none()
            .fill(ModernRagaTheme::CARD_SURFACE)
            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
            .rounding(Rounding::same(6.0))
            .inner_margin(egui::Margin::same(10.0))
            .show(ui, |ui| {
                egui::ScrollArea::vertical().max_height(140.0).stick_to_bottom(true).show(ui, |ui| {
                    for entry in &self.console_logs {
                        ui.label(egui::RichText::new(entry).monospace().small().color(ModernRagaTheme::TEXT_MUTED));
                    }
                });
            });
    }

    /// Render Tab 2: Synthesis & Envelopes
    fn render_synthesis_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(egui::RichText::new("Synthesis Engine & Envelope Shaping").color(ModernRagaTheme::TEXT_PRIMARY));
        ui.label(egui::RichText::new("Adjust pre-baked DAHDSR envelopes & Direct Form II biquad filter settings.").color(ModernRagaTheme::TEXT_MUTED).small());
        ui.add_space(12.0);

        ui.columns(2, |columns| {
            // Column 1: Amplitude DAHDSR
            columns[0].group(|ui| {
                ui.heading(egui::RichText::new("Amplitude DAHDSR Envelope").color(ModernRagaTheme::PEACOCK_TEAL));
                ui.add_space(6.0);

                ui.add(egui::Slider::new(&mut self.attack_ms, 0.0..=500.0).text("Attack (ms)"));
                ui.add(egui::Slider::new(&mut self.hold_ms, 0.0..=200.0).text("Hold (ms)"));
                ui.add(egui::Slider::new(&mut self.decay_ms, 10.0..=1000.0).text("Decay (ms)"));
                ui.add(egui::Slider::new(&mut self.sustain_percent, 0.0..=100.0).text("Sustain (%)"));
                ui.add(egui::Slider::new(&mut self.release_ms, 5.0..=1000.0).text("Release (ms)"));
            });

            // Column 2: Filter & Modulation
            columns[1].group(|ui| {
                ui.heading(egui::RichText::new("Direct Form II Biquad Filter").color(ModernRagaTheme::CORAL_VERMILLION));
                ui.add_space(6.0);

                ui.add(egui::Slider::new(&mut self.cutoff_hz, 200.0..=12000.0).text("Cutoff (Hz)"));
                ui.add(egui::Slider::new(&mut self.resonance_db, 0.0..=18.0).text("Resonance (dB)"));
                ui.add_space(10.0);
                ui.label(egui::RichText::new("Filter Topology: 2-Pole Direct Form II Low-Pass").small().color(ModernRagaTheme::TEXT_MUTED));
            });
        });

        ui.add_space(16.0);
        if ui.button(egui::RichText::new("⚡ Apply Envelope Parameters to Active VHL").strong()).clicked() {
            // Apply parameters to region 1 (Harmonium)
            if self.project.regions.len() > 1 {
                self.project.regions[1].vol_env.attack_ms = self.attack_ms as u16;
                self.project.regions[1].vol_env.hold_ms = self.hold_ms as u16;
                self.project.regions[1].vol_env.decay_ms = self.decay_ms as u16;
                self.project.regions[1].vol_env.sustain_level = ((self.sustain_percent / 100.0) * 1000.0) as u16;
                self.project.regions[1].vol_env.release_ms = self.release_ms as u16;
                self.project.regions[1].filter.cutoff_hz = self.cutoff_hz as u16;
                self.project.regions[1].filter.resonance_cb = (self.resonance_db * 10.0) as u16;

                // Also sync to real-time audio player
                if let Some(s) = self.project.samples.first() {
                    self.audio_player.sync_from_project(
                        &s.pcm_frames,
                        s.root_key,
                        s.loop_start_frame,
                        s.loop_end_frame,
                        &self.project.regions[1].vol_env,
                        self.cutoff_hz,
                        self.resonance_db,
                        self.master_volume / 100.0,
                    );
                }
            }
            self.log(format!("Applied ADSR & Filter to Harmonium: A={:.0}ms, D={:.0}ms, S={:.0}%, R={:.0}ms | Cutoff={:.0}Hz", 
                self.attack_ms, self.decay_ms, self.sustain_percent, self.release_ms, self.cutoff_hz));
        }
    }

    /// Render Tab 3: Toolchain (VHL / VHR / MIDI / Wi-Fi)
    fn render_toolchain_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(egui::RichText::new("VHL & VHR Toolchain Pipeline").color(ModernRagaTheme::TEXT_PRIMARY));
        ui.add_space(8.0);

        // Project summary card
        egui::Frame::none()
            .fill(ModernRagaTheme::CARD_SURFACE)
            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
            .rounding(Rounding::same(6.0))
            .inner_margin(egui::Margin::same(12.0))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label(egui::RichText::new("Active Project:").strong());
                    ui.colored_label(ModernRagaTheme::PEACOCK_TEAL, self.project.build_tag.trim_matches('\0'));
                    ui.separator();
                    ui.label(format!("Instruments: {}", self.project.instruments.len()));
                    ui.separator();
                    ui.label(format!("Regions: {}", self.project.regions.len()));
                    ui.separator();
                    ui.label(format!("Samples: {}", self.project.samples.len()));
                    ui.separator();
                    let pcm_total_frames: usize = self.project.samples.iter().map(|s| s.pcm_frames.len()).sum();
                    ui.label(format!("Waveform Size: {} KB", (pcm_total_frames * 2) / 1024));
                });
            });

        ui.add_space(16.0);

        ui.horizontal(|ui| {
            if ui.button(egui::RichText::new("🔨 Compile VHL Package").strong()).clicked() {
                match VhlCompiler::compile(&self.project) {
                    Ok(bytes) => {
                        let size = bytes.len();
                        let crc = u32::from_le_bytes([bytes[58], bytes[59], bytes[60], bytes[61]]);
                        self.last_compiled_vhl = Some(bytes);
                        self.log(format!("[SUCCESS] VHL v2.0 package compiled: {} bytes | CRC-32: 0x{:08X}", size, crc));
                    }
                    Err(e) => {
                        self.log(format!("[ERROR] VHL Compilation failed: {}", e));
                    }
                }
            }

            if ui.button(egui::RichText::new("💾 Save .vhl File...").strong()).clicked() {
                if let Some(buf) = &self.last_compiled_vhl {
                    if let Some(path) = rfd::FileDialog::new()
                        .set_title("Save Compiled VHL v2.0 Binary")
                        .add_filter("VHL v2.0 Binary", &["vhl"])
                        .set_file_name("harmonium_preset.vhl")
                        .save_file()
                    {
                        match std::fs::write(&path, buf) {
                            Ok(_) => self.log(format!("[FILE EXPORT] Successfully saved {} bytes to {:?}", buf.len(), path)),
                            Err(e) => self.log(format!("[ERROR] Failed to save VHL file: {}", e)),
                        }
                    }
                } else {
                    self.log("[WARN] Please compile VHL package first before saving.");
                }
            }

            if ui.button(egui::RichText::new("📂 Open & Decompile .vhl File...").strong()).clicked() {
                if let Some(path) = rfd::FileDialog::new()
                    .set_title("Select VHL v2.0 Binary to Decompile")
                    .add_filter("VHL v2.0 Binary", &["vhl", "bin"])
                    .pick_file()
                {
                    match std::fs::read(&path) {
                        Ok(data) => {
                            match VhlDecompiler::decompile(&data) {
                                Ok(decompiled) => {
                                    let tag = decompiled.build_tag.clone();
                                    let inst_cnt = decompiled.instruments.len();
                                    let reg_cnt = decompiled.regions.len();
                                    let smpl_cnt = decompiled.samples.len();
                                    self.project = decompiled;
                                    self.last_compiled_vhl = Some(data);
                                    self.log(format!("[VHL IMPORT] Successfully loaded {:?}: Tag='{}', {} inst, {} reg, {} smpl",
                                        path.file_name().unwrap_or_default(), tag, inst_cnt, reg_cnt, smpl_cnt));
                                }
                                Err(e) => self.log(format!("[ERROR] Decompilation error for {:?}: {}", path, e)),
                            }
                        }
                        Err(e) => self.log(format!("[ERROR] Failed to read file: {}", e)),
                    }
                }
            }

            if ui.button(egui::RichText::new("📁 Import WAV Sample...").strong()).clicked() {
                if let Some(path) = rfd::FileDialog::new()
                    .set_title("Select WAV Sample (PCM 8/16/24-bit or 32-bit Float)")
                    .add_filter("WAV Audio", &["wav", "wave"])
                    .pick_file()
                {
                    match crate::vhl::wav_reader::WavReader::read_from_file(&path) {
                        Ok(info) => {
                            let sample_name = path.file_stem().and_then(|s| s.to_str()).unwrap_or("Imported_Sample");
                            let vhl_sample = crate::vhl::wav_reader::WavReader::to_vhl_sample(info, sample_name);
                            let frames_count = vhl_sample.pcm_frames.len();
                            let root = vhl_sample.root_key;
                            let sr = vhl_sample.sample_rate;

                            if !self.project.samples.is_empty() {
                                self.project.samples[0] = vhl_sample;
                            } else {
                                self.project.samples.push(vhl_sample);
                            }

                            // Sync audio player immediately
                            if let Some(s) = self.project.samples.first() {
                                if let Some(r) = self.project.regions.get(1) {
                                    self.audio_player.sync_from_project(
                                        &s.pcm_frames,
                                        s.root_key,
                                        s.loop_start_frame,
                                        s.loop_end_frame,
                                        &r.vol_env,
                                        self.cutoff_hz,
                                        self.resonance_db,
                                        self.master_volume / 100.0,
                                    );
                                }
                            }

                            self.log(format!("[WAV IMPORT] Imported '{}': {} frames, Root Key MIDI {}, Sample Rate {} Hz -> Synced to Audio Engine",
                                sample_name, frames_count, root, sr));
                        }
                        Err(e) => {
                            self.log(format!("[ERROR] Failed to import WAV: {}", e));
                        }
                    }
                }
            }

            if ui.button(egui::RichText::new("📡 Sync via Wi-Fi to ESP32").strong()).clicked() {
                if let Some(buf) = &self.last_compiled_vhl {
                    self.log(format!("Transmitting {} bytes of VHL v2.0 payload to ESP32 at http://{}/api/vhl/upload", buf.len(), self.esp32_ip));
                } else {
                    self.log("[WARN] Please compile VHL package before Wi-Fi transmission.");
                }
            }
        });

        ui.add_space(16.0);

        // Waveform Visualizer & Inspector (Polyphone Style)
        if let Some(sample) = self.project.samples.first() {
            ui.horizontal(|ui| {
                ui.heading(egui::RichText::new(format!("🌊 Sample Waveform: {}", sample.name)).color(ModernRagaTheme::PEACOCK_TEAL).small());
                ui.label(format!("| Root Key: MIDI {} | Loop: {} .. {} frames | Channels: {}", 
                    sample.root_key, sample.loop_start_frame, sample.loop_end_frame, sample.channels));
            });
            ui.add_space(4.0);

            let wave_width = ui.available_width().max(600.0);
            let wave_height = 130.0;
            let (wave_rect, _) = ui.allocate_exact_size(Vec2::new(wave_width, wave_height), egui::Sense::hover());

            if ui.is_rect_visible(wave_rect) {
                let painter = ui.painter_at(wave_rect);

                // Background
                painter.rect_filled(wave_rect, Rounding::same(6.0), Color32::from_rgb(12, 14, 18));
                painter.rect_stroke(wave_rect, Rounding::same(6.0), Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR));

                // Zero-crossing center line
                let center_y = wave_rect.min.y + wave_height / 2.0;
                painter.line_segment(
                    [Pos2::new(wave_rect.min.x, center_y), Pos2::new(wave_rect.max.x, center_y)],
                    Stroke::new(1.0_f32, Color32::from_rgb(45, 55, 70)),
                );

                // Min-Max Decimation Waveform Plotting (Polyphone GraphicsWavePainter algorithm)
                let num_frames = sample.pcm_frames.len();
                if num_frames > 0 {
                    let pixel_count = (wave_width - 8.0).max(1.0) as usize;
                    let step = (num_frames as f32) / (pixel_count as f32);
                    let half_h = (wave_height / 2.0) - 8.0;

                    for px in 0..pixel_count {
                        let start_frame = ((px as f32) * step) as usize;
                        let end_frame = ((((px + 1) as f32) * step) as usize).min(num_frames);

                        let mut min_val = i16::MAX;
                        let mut max_val = i16::MIN;

                        if start_frame < num_frames {
                            for f_idx in start_frame..end_frame.max(start_frame + 1) {
                                let val = sample.pcm_frames[f_idx];
                                if val < min_val { min_val = val; }
                                if val > max_val { max_val = val; }
                            }
                        } else {
                            min_val = 0;
                            max_val = 0;
                        }

                        let x = wave_rect.min.x + 4.0 + (px as f32);
                        let y_top = center_y - ((max_val as f32) / 32768.0) * half_h;
                        let y_bot = center_y - ((min_val as f32) / 32768.0) * half_h;

                        painter.line_segment(
                            [Pos2::new(x, y_top), Pos2::new(x, y_bot.max(y_top + 1.0))],
                            Stroke::new(1.0_f32, ModernRagaTheme::PEACOCK_TEAL),
                        );
                    }

                    // Render Loop Markers (Polyphone Loop boundaries)
                    if sample.loop_end_frame > sample.loop_start_frame && sample.loop_end_frame <= num_frames as u32 {
                        let loop_start_x = wave_rect.min.x + 4.0 + ((sample.loop_start_frame as f32 / num_frames as f32) * (wave_width - 8.0));
                        let loop_end_x = wave_rect.min.x + 4.0 + ((sample.loop_end_frame as f32 / num_frames as f32) * (wave_width - 8.0));

                        // Loop shaded region
                        let loop_rect = Rect::from_min_max(
                            Pos2::new(loop_start_x, wave_rect.min.y + 2.0),
                            Pos2::new(loop_end_x, wave_rect.max.y - 2.0),
                        );
                        painter.rect_filled(loop_rect, Rounding::ZERO, Color32::from_rgba_unmultiplied(244, 162, 97, 30));

                        // Loop boundary lines
                        painter.line_segment(
                            [Pos2::new(loop_start_x, wave_rect.min.y), Pos2::new(loop_start_x, wave_rect.max.y)],
                            Stroke::new(1.5_f32, ModernRagaTheme::MARIGOLD_GOLD),
                        );
                        painter.line_segment(
                            [Pos2::new(loop_end_x, wave_rect.min.y), Pos2::new(loop_end_x, wave_rect.max.y)],
                            Stroke::new(1.5_f32, ModernRagaTheme::CORAL_VERMILLION),
                        );
                    }
                }
            }
        }

        ui.add_space(16.0);

        // Binary status card
        if let Some(buf) = &self.last_compiled_vhl {
            egui::Frame::none()
                .fill(ModernRagaTheme::CARD_SURFACE)
                .stroke(Stroke::new(1.0_f32, ModernRagaTheme::EMERALD_ACTIVE))
                .rounding(Rounding::same(6.0))
                .inner_margin(egui::Margin::same(12.0))
                .show(ui, |ui| {
                    ui.vertical(|ui| {
                        ui.label(egui::RichText::new("Binary Payload Ready for ESP32 Deployment:").color(ModernRagaTheme::EMERALD_ACTIVE).strong());
                        ui.label(format!("• Total binary size: {} bytes ({:.2} KB)", buf.len(), (buf.len() as f32) / 1024.0));
                        ui.label(format!("• Header size: 128 bytes (Section 0)"));
                        ui.label(format!("• DMA block size: {} frames", self.project.dma_block_size));
                        ui.label(format!("• Sample rate: {} Hz (16-bit PCM)", self.project.target_sample_rate));
                    });
                });
        }
    }

    /// Render Tab 4: Educational Note Learner
    fn render_educational_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(egui::RichText::new("Educational Note & Raga Visualizer").color(ModernRagaTheme::TEXT_PRIMARY));
        ui.label(egui::RichText::new("Visualizing notes in sequential order for students learning Indian Classical ragas.").color(ModernRagaTheme::TEXT_MUTED).small());
        ui.add_space(12.0);

        egui::Frame::none()
            .fill(ModernRagaTheme::CARD_SURFACE)
            .stroke(Stroke::new(1.0_f32, ModernRagaTheme::BORDER_COLOR))
            .rounding(Rounding::same(6.0))
            .inner_margin(egui::Margin::same(16.0))
            .show(ui, |ui| {
                ui.horizontal(|ui| {
                    for (swara, note) in [("Sa", "C4"), ("Re", "D4"), ("Ga", "E4"), ("Ma", "F4"), ("Pa", "G4"), ("Dha", "A4"), ("Ni", "B4"), ("Sa'", "C5")] {
                        ui.vertical(|ui| {
                            ui.label(egui::RichText::new(swara).heading().color(ModernRagaTheme::MARIGOLD_GOLD));
                            ui.label(egui::RichText::new(note).small().color(ModernRagaTheme::TEXT_MUTED));
                        });
                        ui.add_space(16.0);
                    }
                });
            });
    }

    /// Render Floating Parameter Inspection Table Window (Polyphone-style, fully editable)
    fn render_parameter_table_window(&mut self, ctx: &egui::Context) {
        let mut is_open = self.show_param_table;
        let mut need_audio_sync = false;

        // Collect region count before entering closure (avoids borrow conflicts)
        let region_count = self.project.regions.len();

        egui::Window::new("📊 VHL Parameter Inspector")
            .open(&mut is_open)
            .default_size([920.0, 600.0])
            .min_width(700.0)
            .min_height(400.0)
            .resizable(true)
            .show(ctx, |ui| {
                // ── Top info / action bar ────────────────────────────────────
                ui.horizontal(|ui| {
                    ui.label(egui::RichText::new("VHL Parameter Inspector").strong().color(ModernRagaTheme::PEACOCK_TEAL));
                    ui.separator();
                    let sname = self.project.samples.first().map(|s| s.name.clone()).unwrap_or_default();
                    ui.label(egui::RichText::new(&sname).color(ModernRagaTheme::MARIGOLD_GOLD));
                    ui.separator();
                    ui.label(egui::RichText::new(format!("{} regions", region_count)).color(ModernRagaTheme::TEXT_MUTED).small());
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.button(egui::RichText::new("🔄 Sync to Audio").strong()).clicked() {
                            need_audio_sync = true;
                        }
                        ui.label(egui::RichText::new("Drag values to edit · changes apply instantly").small().color(ModernRagaTheme::TEXT_MUTED));
                    });
                });
                ui.separator();

                egui::ScrollArea::both().auto_shrink([false, false]).show(ui, |ui| {

                    // ── Column widths: param label | one col per region ──────
                    let param_col_w = 190.0_f32;
                    let val_col_w   = 155.0_f32;

                    // ── Helper: section divider ──────────────────────────────
                    let section = |ui: &mut egui::Ui, label: &str, color: egui::Color32| {
                        ui.add_space(6.0);
                        ui.horizontal(|ui| {
                            ui.label(egui::RichText::new(label).strong().small().color(color));
                            ui.separator();
                        });
                        ui.add_space(2.0);
                    };

                    // ════════════════════════════════════════════════════════
                    // SAMPLE PROPERTIES
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ SAMPLE", ModernRagaTheme::PEACOCK_TEAL);

                    egui::Grid::new("poly_sample_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            // Header
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            ui.label(egui::RichText::new("Global").strong().color(ModernRagaTheme::TEXT_MUTED));
                            if let Some(s) = self.project.samples.first() {
                                ui.label(egui::RichText::new(&s.name).strong().color(ModernRagaTheme::PEACOCK_TEAL));
                            }
                            ui.end_row();

                            // Sample root key
                            ui.label(egui::RichText::new("Sample root key").color(ModernRagaTheme::TEXT_MUTED));
                            ui.label("—");
                            if let Some(s) = self.project.samples.first_mut() {
                                let mut rk = s.root_key;
                                if ui.add(egui::DragValue::new(&mut rk).range(0..=127_u8).prefix("MIDI ")).changed() {
                                    s.root_key = rk;
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // Loop start
                            ui.label(egui::RichText::new("Loop start (frame)").color(ModernRagaTheme::TEXT_MUTED));
                            ui.label("—");
                            if let Some(s) = self.project.samples.first_mut() {
                                let mut ls = s.loop_start_frame;
                                if ui.add(egui::DragValue::new(&mut ls).speed(100.0)).changed() {
                                    s.loop_start_frame = ls;
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // Loop end
                            ui.label(egui::RichText::new("Loop end (frame)").color(ModernRagaTheme::TEXT_MUTED));
                            ui.label("—");
                            if let Some(s) = self.project.samples.first_mut() {
                                let mut le = s.loop_end_frame;
                                if ui.add(egui::DragValue::new(&mut le).speed(100.0)).changed() {
                                    s.loop_end_frame = le;
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // REGION PARAMETERS  — one column per region
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ KEY / VELOCITY / ROUTING", ModernRagaTheme::CORAL_VERMILLION);

                    egui::Grid::new("poly_region_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {

                            // ── Header row ──────────────────────────────────
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            let header_colors = [ModernRagaTheme::PEACOCK_TEAL, ModernRagaTheme::CORAL_VERMILLION, ModernRagaTheme::MARIGOLD_GOLD];
                            for i in 0..region_count {
                                let lbl = match i { 0 => "Region 0 (Piano)".to_string(), 1 => "Region 1 (Harmonium)".to_string(), n => format!("Region {}", n) };
                                ui.label(egui::RichText::new(lbl).strong().color(header_colors[i.min(2)]));
                            }
                            ui.end_row();

                            // ── Key range ───────────────────────────────────
                            ui.label(egui::RichText::new("Key range").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut lo = { self.project.regions[i].key_lo };
                                let mut hi = { self.project.regions[i].key_hi };
                                ui.horizontal(|ui| {
                                    if ui.add(egui::DragValue::new(&mut lo).range(0..=127_u8).speed(1)).changed() { self.project.regions[i].key_lo = lo; }
                                    ui.label("‥");
                                    if ui.add(egui::DragValue::new(&mut hi).range(0..=127_u8).speed(1)).changed() { self.project.regions[i].key_hi = hi; }
                                });
                            }
                            ui.end_row();

                            // ── Velocity range ──────────────────────────────
                            ui.label(egui::RichText::new("Velocity range").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut lo = { self.project.regions[i].vel_lo };
                                let mut hi = { self.project.regions[i].vel_hi };
                                ui.horizontal(|ui| {
                                    if ui.add(egui::DragValue::new(&mut lo).range(0..=127_u8).speed(1)).changed() { self.project.regions[i].vel_lo = lo; }
                                    ui.label("‥");
                                    if ui.add(egui::DragValue::new(&mut hi).range(0..=127_u8).speed(1)).changed() { self.project.regions[i].vel_hi = hi; }
                                });
                            }
                            ui.end_row();

                            // ── Attenuation ─────────────────────────────────
                            ui.label(egui::RichText::new("Attenuation (dB)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].attenuation_cb } as f32 / 10.0;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=144.0_f32).speed(0.1).suffix(" dB")).changed() {
                                    self.project.regions[i].attenuation_cb = (val * 10.0) as u16;
                                }
                            }
                            ui.end_row();

                            // ── Pan ─────────────────────────────────────────
                            ui.label(egui::RichText::new("Pan (-500..500)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].pan };
                                if ui.add(egui::DragValue::new(&mut val).range(-500..=500_i16).speed(1)).changed() {
                                    self.project.regions[i].pan = val;
                                }
                            }
                            ui.end_row();

                            // ── Loop mode ───────────────────────────────────
                            ui.label(egui::RichText::new("Loop playback").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].loop_mode };
                                egui::ComboBox::from_id_salt(format!("loop_mode_combo_{}", i))
                                    .selected_text(match val { 0 => "No Loop", 1 => "Continuous", _ => "Sustain Loop" })
                                    .width(130.0)
                                    .show_ui(ui, |ui| {
                                        ui.selectable_value(&mut val, 0, "No Loop");
                                        ui.selectable_value(&mut val, 1, "Continuous");
                                        ui.selectable_value(&mut val, 3, "Sustain Loop");
                                    });
                                self.project.regions[i].loop_mode = val;
                            }
                            ui.end_row();

                            // ── Reverb / Chorus send ─────────────────────────
                            ui.label(egui::RichText::new("Reverb send (%)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].reverb_send } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(0..=100_i32).speed(1).suffix("%")).changed() {
                                    self.project.regions[i].reverb_send = val as u8;
                                }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Chorus send (%)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].chorus_send } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(0..=100_i32).speed(1).suffix("%")).changed() {
                                    self.project.regions[i].chorus_send = val as u8;
                                }
                            }
                            ui.end_row();
                        }); // end key/vel grid

                    // ════════════════════════════════════════════════════════
                    // TUNING
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ TUNING", ModernRagaTheme::MARIGOLD_GOLD);

                    egui::Grid::new("poly_tune_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            ui.label(egui::RichText::new("Tuning (semi-tones)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].coarse_tune } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(-48..=48_i32)).changed() {
                                    self.project.regions[i].coarse_tune = val as i8;
                                }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Tuning (cents)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].fine_tune } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(-100..=100_i32).suffix(" ¢")).changed() {
                                    self.project.regions[i].fine_tune = val as i16;
                                }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Scale tuning").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].scale_tuning } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(0..=200_i32)).changed() {
                                    self.project.regions[i].scale_tuning = val as u8;
                                }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // FILTER
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ FILTER  (affects audio immediately)", ModernRagaTheme::CORAL_VERMILLION);

                    egui::Grid::new("poly_filter_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            ui.label(egui::RichText::new("Filter, cutoff (Hz)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].filter.cutoff_hz } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(20.0..=20000.0_f32).speed(10.0).suffix(" Hz")).changed() {
                                    self.project.regions[i].filter.cutoff_hz = val as u16;
                                    // Mirror to the GUI slider state for region 1 (primary)
                                    if i == 1 || region_count == 1 { self.cutoff_hz = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Filter, resonance (dB)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].filter.resonance_cb } as f32 / 10.0;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=40.0_f32).speed(0.1).suffix(" dB")).changed() {
                                    self.project.regions[i].filter.resonance_cb = (val * 10.0) as u16;
                                    if i == 1 || region_count == 1 { self.resonance_db = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // VOLUME ENVELOPE  (affects audio immediately)
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ VOL ENVELOPE  (affects audio immediately)", ModernRagaTheme::PEACOCK_TEAL);

                    egui::Grid::new("poly_volenv_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            // delay
                            ui.label(egui::RichText::new("Vol env delay (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.delay_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=5000.0_f32).suffix(" ms")).changed() {
                                    self.project.regions[i].vol_env.delay_ms = val as u16;
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // attack
                            ui.label(egui::RichText::new("Vol env attack (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.attack_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=10000.0_f32).speed(1.0).suffix(" ms")).changed() {
                                    self.project.regions[i].vol_env.attack_ms = val as u16;
                                    if i == 1 || region_count == 1 { self.attack_ms = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // hold
                            ui.label(egui::RichText::new("Vol env hold (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.hold_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=5000.0_f32).suffix(" ms")).changed() {
                                    self.project.regions[i].vol_env.hold_ms = val as u16;
                                    if i == 1 || region_count == 1 { self.hold_ms = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // decay
                            ui.label(egui::RichText::new("Vol env decay (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.decay_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=10000.0_f32).speed(1.0).suffix(" ms")).changed() {
                                    self.project.regions[i].vol_env.decay_ms = val as u16;
                                    if i == 1 || region_count == 1 { self.decay_ms = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // sustain  (stored as 0..1000 ‰, displayed as %)
                            ui.label(egui::RichText::new("Vol env sustain (%)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.sustain_level } as f32 / 10.0;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=100.0_f32).speed(0.5).suffix(" %")).changed() {
                                    self.project.regions[i].vol_env.sustain_level = (val * 10.0) as u16;
                                    if i == 1 || region_count == 1 { self.sustain_percent = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // release
                            ui.label(egui::RichText::new("Vol env release (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.release_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(1.0..=10000.0_f32).speed(1.0).suffix(" ms")).changed() {
                                    self.project.regions[i].vol_env.release_ms = val as u16;
                                    if i == 1 || region_count == 1 { self.release_ms = val; }
                                    need_audio_sync = true;
                                }
                            }
                            ui.end_row();

                            // key→hold
                            ui.label(egui::RichText::new("Key → Vol env hold").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.key_to_hold } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(-127..=127_i32)).changed() {
                                    self.project.regions[i].vol_env.key_to_hold = val as i8;
                                }
                            }
                            ui.end_row();

                            // key→decay
                            ui.label(egui::RichText::new("Key → Vol env decay").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].vol_env.key_to_decay } as i32;
                                if ui.add(egui::DragValue::new(&mut val).range(-127..=127_i32)).changed() {
                                    self.project.regions[i].vol_env.key_to_decay = val as i8;
                                }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // MOD ENVELOPE
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ MOD ENVELOPE", ModernRagaTheme::MARIGOLD_GOLD);

                    egui::Grid::new("poly_modenv_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            macro_rules! mod_env_row {
                                ($ui:expr, $label:expr, $field:ident, $min:expr, $max:expr, $suffix:expr) => {
                                    $ui.label(egui::RichText::new($label).color(ModernRagaTheme::TEXT_MUTED));
                                    for i in 0..region_count {
                                        let mut val = { self.project.regions[i].mod_env.$field } as f32;
                                        if $ui.add(egui::DragValue::new(&mut val).range($min..=$max as f32).suffix($suffix)).changed() {
                                            self.project.regions[i].mod_env.$field = val as _;
                                        }
                                    }
                                    $ui.end_row();
                                };
                            }

                            mod_env_row!(ui, "Mod env attack (ms)",  attack_ms,        0.0, 10000.0, " ms");
                            mod_env_row!(ui, "Mod env hold (ms)",    hold_ms,          0.0, 5000.0,  " ms");
                            mod_env_row!(ui, "Mod env decay (ms)",   decay_ms,         0.0, 10000.0, " ms");
                            mod_env_row!(ui, "Mod env release (ms)", release_ms,       1.0, 10000.0, " ms");

                            // sustain separately (‰ → %)
                            ui.label(egui::RichText::new("Mod env sustain (%)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].mod_env.sustain_level } as f32 / 10.0;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=100.0_f32).speed(0.5).suffix(" %")).changed() {
                                    self.project.regions[i].mod_env.sustain_level = (val * 10.0) as u16;
                                }
                            }
                            ui.end_row();

                            // mod env routing
                            ui.label(egui::RichText::new("Mod env → pitch (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].mod_env_to_pitch };
                                if ui.add(egui::DragValue::new(&mut val).range(-12000..=12000_i16)).changed() {
                                    self.project.regions[i].mod_env_to_pitch = val;
                                }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod env → filter (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].mod_env_to_filter };
                                if ui.add(egui::DragValue::new(&mut val).range(-12000..=12000_i16)).changed() {
                                    self.project.regions[i].mod_env_to_filter = val;
                                }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // MOD LFO
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ MOD LFO", ModernRagaTheme::CORAL_VERMILLION);

                    egui::Grid::new("poly_modlfo_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod LFO delay (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_mod.delay_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=5000.0_f32).suffix(" ms")).changed() { self.project.regions[i].lfo_mod.delay_ms = val as u16; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod LFO freq (mHz)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_mod.freq_mHz } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=20000.0_f32).suffix(" mHz")).changed() { self.project.regions[i].lfo_mod.freq_mHz = val as u16; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod LFO → pitch (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_mod.to_pitch_cb };
                                if ui.add(egui::DragValue::new(&mut val).range(-12000..=12000_i16)).changed() { self.project.regions[i].lfo_mod.to_pitch_cb = val; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod LFO → filter (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_mod.to_filter_cb };
                                if ui.add(egui::DragValue::new(&mut val).range(-12000..=12000_i16)).changed() { self.project.regions[i].lfo_mod.to_filter_cb = val; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Mod LFO → volume (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_mod.to_volume_cb };
                                if ui.add(egui::DragValue::new(&mut val).range(-960..=960_i16)).changed() { self.project.regions[i].lfo_mod.to_volume_cb = val; }
                            }
                            ui.end_row();
                        });

                    // ════════════════════════════════════════════════════════
                    // VIB LFO
                    // ════════════════════════════════════════════════════════
                    section(ui, "▶ VIB LFO", egui::Color32::from_rgb(140, 140, 170));

                    egui::Grid::new("poly_viblfo_grid")
                        .striped(true)
                        .min_col_width(param_col_w)
                        .max_col_width(val_col_w)
                        .show(ui, |ui| {
                            ui.label(egui::RichText::new("Parameter").strong().color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count { ui.label(format!("Region {}", i)); }
                            ui.end_row();

                            ui.label(egui::RichText::new("Vib LFO delay (ms)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_vib.delay_ms } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=5000.0_f32).suffix(" ms")).changed() { self.project.regions[i].lfo_vib.delay_ms = val as u16; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Vib LFO freq (mHz)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_vib.freq_mHz } as f32;
                                if ui.add(egui::DragValue::new(&mut val).range(0.0..=20000.0_f32).suffix(" mHz")).changed() { self.project.regions[i].lfo_vib.freq_mHz = val as u16; }
                            }
                            ui.end_row();

                            ui.label(egui::RichText::new("Vib LFO → pitch (cb)").color(ModernRagaTheme::TEXT_MUTED));
                            for i in 0..region_count {
                                let mut val = { self.project.regions[i].lfo_vib.to_pitch_cb };
                                if ui.add(egui::DragValue::new(&mut val).range(-12000..=12000_i16)).changed() { self.project.regions[i].lfo_vib.to_pitch_cb = val; }
                            }
                            ui.end_row();
                        });

                }); // end ScrollArea
            }); // end Window

        self.show_param_table = is_open;

        // ── Audio sync after any parameter change ────────────────────────────
        if need_audio_sync && !self.project.samples.is_empty() {
            let (pcm, root, ls, le) = {
                let s = &self.project.samples[0];
                (s.pcm_frames.clone(), s.root_key, s.loop_start_frame, s.loop_end_frame)
            };
            // Use region 1 (harmonium) if available, else region 0
            let env_copy: crate::vhl::types::VHLEnvelope = if self.project.regions.len() > 1 {
                self.project.regions[1].vol_env
            } else if !self.project.regions.is_empty() {
                self.project.regions[0].vol_env
            } else {
                return;
            };
            let cutoff = self.cutoff_hz;
            let resonance = self.resonance_db;
            let vol = self.master_volume / 100.0;
            self.audio_player.sync_from_project(&pcm, root, ls, le, &env_copy, cutoff, resonance, vol);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Laptop Keyboard → Piano Mapping  (Polyphone Layout)
// ═══════════════════════════════════════════════════════════════════════════

/// Note name table for log messages (index 0 = C, 11 = B)
const NOTE_NAMES: [&str; 12] = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];

/// Maps a laptop key + shift modifier to a piano key index (0..60).
/// Piano key 0 = MIDI 36 (C2), key 60 = MIDI 96 (C7).
///
/// Mirrors the Polyphone keyboard layout ("First C = MIDI 36"):
///
/// | No Shift | Octave 1 (MIDI 36-47) : Z S X D C  V G B H N J M      |
/// |          | Octave 2 (MIDI 48-59) : Q 2 W 3 E  R 5 T 6 Y 7 U      |
/// | Shift    | Octave 3 (MIDI 60-71) : same Z-row + Shift              |
/// |          | Octave 4 (MIDI 72-83) : same Q-row + Shift              |
///
/// Number keys 2,3,5,6,7 produce the black/sharp notes.
/// In egui, Shift+2 still reports as `Key::Num2` with `modifiers.shift=true`.
fn laptop_key_to_piano_key(key: egui::Key, shift: bool) -> Option<u8> {
    use egui::Key::*;

    // Shift bumps everything up by 2 octaves (24 semitones / key-index units)
    let oct_base: u8 = if shift { 24 } else { 0 };

    let semi: Option<u8> = match key {
        // ── Z-row: Octave 1 (no shift) / Octave 3 (shift) ────────────────
        Z    => Some(0),   // C
        S    => Some(1),   // C#
        X    => Some(2),   // D
        D    => Some(3),   // D#
        C    => Some(4),   // E
        V    => Some(5),   // F
        G    => Some(6),   // F#
        B    => Some(7),   // G
        H    => Some(8),   // G#
        N    => Some(9),   // A
        J    => Some(10),  // A#
        M    => Some(11),  // B
        // ── Q-row: Octave 2 (no shift) / Octave 4 (shift) ────────────────
        Q    => Some(12),  // C
        Num2 => Some(13),  // C#  (@ with shift)
        W    => Some(14),  // D
        Num3 => Some(15),  // D#  (# with shift)
        E    => Some(16),  // E
        R    => Some(17),  // F
        Num5 => Some(18),  // F#  (% with shift)
        T    => Some(19),  // G
        Num6 => Some(20),  // G#  (^ with shift)
        Y    => Some(21),  // A
        Num7 => Some(22),  // A#  (& with shift)
        U    => Some(23),  // B
        // ── Extra high-C keys ─────────────────────────────────────────────
        I    => Some(24),  // C (one octave above U)
        _    => None,
    };

    semi.map(|s| (oct_base + s).min(60))
}
