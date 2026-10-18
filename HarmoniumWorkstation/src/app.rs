use eframe::egui;
use egui::{Color32, Pos2, Rect, Rounding, Stroke, Vec2};
use crate::theme::ModernRagaTheme;

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
}

impl Default for HarmoniumApp {
    fn default() -> Self {
        Self {
            current_tab: ActiveTab::PerformanceKeyboard,
            master_volume: 80.0,
            tempo_bpm: 120,
            transpose_semitones: 0,
            split_key_index: 24, // Split at C4
            last_clicked_key: None,
            test_audition_count: 0,

            attack_ms: 15.0,
            hold_ms: 5.0,
            decay_ms: 120.0,
            sustain_percent: 85.0,
            release_ms: 45.0,

            cutoff_hz: 4500.0,
            resonance_db: 4.5,

            esp32_ip: "192.168.4.1".to_string(),
            is_connected_wifi: false,

            console_logs: vec![
                "[SYSTEM] Harmonium Workstation Initialized.".to_string(),
                "[VHL Engine] Ready for compilation pipeline.".to_string(),
                "[VHR Engine] MIDI v2.0 bidirectional translator ready.".to_string(),
                "[Audio DSP] Target: 44.1 kHz, 16-bit Stereo (PCM5102A).".to_string(),
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
                match self.current_tab {
                    ActiveTab::PerformanceKeyboard => self.render_keyboard_tab(ui),
                    ActiveTab::SynthesisEnvelopes => self.render_synthesis_tab(ui),
                    ActiveTab::VhlVhrToolchain => self.render_toolchain_tab(ui),
                    ActiveTab::EducationalVisualizer => self.render_educational_tab(ui),
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
                if ui.button(egui::RichText::new("Audition Tone").strong()).clicked() {
                    self.test_audition_count += 1;
                    let count = self.test_audition_count;
                    self.log(format!("Audition triggered! Test count = {}", count));
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
                        self.log(format!("Keyboard split point updated to Key #{}", k));
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
        ui.label(egui::RichText::new("Interactive 61-Key Virtual Bed (Click any key to test):").small().color(ModernRagaTheme::TEXT_MUTED));
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
            // Pattern of semitones in an octave: W B W B W W B W B W B W
            // In 5 octaves + 1 high C
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

                // Check click
                if response.clicked() || response.dragged() {
                    if let Some(pos) = response.interact_pointer_pos() {
                        if k_rect.contains(pos) && self.last_clicked_key != Some(key_idx) {
                            self.last_clicked_key = Some(key_idx);
                            let note_midi = key_idx + 36;
                            self.log(format!("Key Pressed: #{}, MIDI {}", key_idx, note_midi));
                        }
                    }
                }
            }

            // Draw Black Keys on top
            let black_key_width = white_key_width * 0.65;
            let black_key_height = white_key_height * 0.62;

            for &key_idx in &black_key_indices {
                // Find white key preceding this black key
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
                            let note_midi = key_idx + 36;
                            self.log(format!("Black Key Pressed: #{}, MIDI {}", key_idx, note_midi));
                        }
                    }
                }
            }
        }

        ui.add_space(16.0);

        // System Log Activity Box
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
            self.log(format!("Applied ADSR: A={:.1}ms, D={:.1}ms, S={:.1}%, R={:.1}ms", self.attack_ms, self.decay_ms, self.sustain_percent, self.release_ms));
        }
    }

    /// Render Tab 3: Toolchain (VHL / VHR / MIDI / Wi-Fi)
    fn render_toolchain_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(egui::RichText::new("VHL & VHR Toolchain Pipeline").color(ModernRagaTheme::TEXT_PRIMARY));
        ui.add_space(12.0);

        ui.horizontal(|ui| {
            if ui.button(egui::RichText::new("🔨 Compile VHL Package").strong()).clicked() {
                self.log("Triggered VHL compilation pre-baking pass...");
            }
            if ui.button(egui::RichText::new("🔍 Decompile .vhl File").strong()).clicked() {
                self.log("Opened VHL decompiler inspector...");
            }
            if ui.button(egui::RichText::new("🎼 Convert MIDI to VHR").strong()).clicked() {
                self.log("MIDI to VHR translator initialized...");
            }
            if ui.button(egui::RichText::new("📡 Sync via Wi-Fi to ESP32").strong()).clicked() {
                self.log(format!("Sending payload over Wi-Fi to {}", self.esp32_ip));
            }
        });
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
}
