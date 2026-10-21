//! Modulation Editor (SoundFont 2 Dynamic Expression Matrix)
//!
//! Bare skeleton UI for configuring dynamic controller routing:
//! - Source 1 (Controller + 16 transfer curves)
//! - Source 2 (Secondary scaling controller)
//! - Amount (scaling depth)
//! - Transform (linear / absolute value)
//! - Destination (Generator parameter)

use crate::theme::AppTheme;
use crate::widgets;
use crate::vhl::types::*;
use egui::{Color32, FontId, Frame, Margin, Pos2, Rounding, Stroke, Ui, Vec2};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModulatorViewAction {
    None,
    BackToLanding,
    OpenSoundEditor,
}

#[derive(Clone, Debug)]
pub struct UiModulatorEntry {
    pub id: usize,
    pub src1_name: String,
    pub curve_type: u8, // 0=Linear, 1=Concave, 2=Convex, 3=Switch, 4=Custom
    pub is_bipolar: bool,
    pub is_descending: bool,
    pub custom_points: [f32; 5], // Normalized [0..1] at 0%, 25%, 50%, 75%, 100%
    pub dest_name: String,
    pub src2_name: String,
    pub amount: f32,
    pub unit: &'static str,
    pub absolute_value: bool,
}

impl Default for UiModulatorEntry {
    fn default() -> Self {
        Self {
            id: 0,
            src1_name: "Note-On Velocity".to_string(),
            curve_type: 1, // Concave
            is_bipolar: false,
            is_descending: true,
            custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
            dest_name: "Initial Attenuation (dB)".to_string(),
            src2_name: "None (Constant 1.0)".to_string(),
            amount: 96.0,
            unit: " dB",
            absolute_value: false,
        }
    }
}

impl UiModulatorEntry {
    pub fn curve_display_name(&self) -> String {
        if self.curve_type == 4 {
            return "✎ Custom".to_string();
        }
        let shape = match self.curve_type {
            0 => "Linear ∿",
            1 => "Concave ⌢",
            2 => "Convex ⌣",
            _ => "Switch ⎍",
        };
        let evo = match (self.is_descending, self.is_bipolar) {
            (false, false) => "0→1",
            (true, false) => "1→0",
            (false, true) => "-1→1",
            (true, true) => "1→-1",
        };
        format!("{} ({})", shape, evo)
    }
}

impl From<&VHLModulator_v2> for UiModulatorEntry {
    fn from(m: &VHLModulator_v2) -> Self {
        let (_, _, curve, descending, bipolar) = VHLModulator_v2::unpack_source(m.src_oper);
        let src1_name = VHLModulator_v2::source_name(m.src_oper);
        let dest_name = VHLModulator_v2::dest_name(m.dest_oper);
        let src2_name = if m.amt_src_oper == 0 {
            "None".to_string()
        } else {
            VHLModulator_v2::source_name(m.amt_src_oper)
        };
        let (amount, unit) = match m.dest_oper {
            SF_GEN_INITIAL_ATTENUATION | SF_GEN_MOD_LFO_TO_VOLUME => (m.amount as f32 / 10.0, " dB"),
            SF_GEN_INITIAL_FILTER_FC | SF_GEN_MOD_ENV_TO_FILTER_FC | SF_GEN_MOD_LFO_TO_FILTER_FC => (m.amount as f32, " c"),
            SF_GEN_FINE_TUNE | SF_GEN_MOD_LFO_TO_PITCH | SF_GEN_VIB_LFO_TO_PITCH | SF_GEN_MOD_ENV_TO_PITCH => (m.amount as f32, " c"),
            SF_GEN_REVERB_EFFECTS_SEND | SF_GEN_CHORUS_EFFECTS_SEND => (m.amount as f32 / 10.0, " %"),
            SF_GEN_PAN => (m.amount as f32 / 10.0, " %"),
            _ => (m.amount as f32, ""),
        };
        Self {
            id: m.index as usize,
            src1_name,
            curve_type: curve,
            is_bipolar: bipolar,
            is_descending: descending,
            custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
            dest_name,
            src2_name,
            amount,
            unit,
            absolute_value: m.transform == SF_TRANSFORM_ABSOLUTE,
        }
    }
}

impl UiModulatorEntry {
    pub fn to_vhl(&self) -> VHLModulator_v2 {
        let (ctrl, is_cc) = match self.src1_name.as_str() {
            "Note-On Velocity" | "Key Velocity" => (SF_MOD_SRC_VELOCITY as u8, false),
            "Note-On Key Number" | "Key Number (Pitch Track)" => (SF_MOD_SRC_KEY as u8, false),
            "Key Pressure" | "Poly Pressure" => (SF_MOD_SRC_POLY_PRESSURE as u8, false),
            "Channel Pressure" | "Channel Pressure / Aftertouch" => (SF_MOD_SRC_CHANNEL_PRESSURE as u8, false),
            "Pitch Wheel" => (SF_MOD_SRC_PITCH_WHEEL as u8, false),
            "Modulation Envelope" => (SF_MOD_SRC_MOD_ENV as u8, false),
            "Mod LFO" => (SF_MOD_SRC_MOD_LFO as u8, false),
            "Vibrato LFO" => (SF_MOD_SRC_VIB_LFO as u8, false),
            "CC 1 (Modulation Wheel)" | "Mod Wheel (CC1)" => (1, true),
            "CC 2 (Breath)" | "Breath (CC2)" => (2, true),
            "CC 7 (Volume)" | "Volume (CC7)" => (7, true),
            "CC 10 (Pan)" | "Pan (CC10)" => (10, true),
            "CC 11 (Expression)" | "Expression (CC11)" => (11, true),
            "CC 64 (Sustain Pedal)" | "Sustain Pedal (CC64)" => (64, true),
            "CC 91 (Reverb Send)" | "Reverb Send (CC91)" => (91, true),
            "CC 93 (Chorus Send)" | "Chorus Send (CC93)" => (93, true),
            _ => (SF_MOD_SRC_VELOCITY as u8, false),
        };
        let src_oper = VHLModulator_v2::pack_source(ctrl, is_cc, self.curve_type, self.is_descending, self.is_bipolar);
        let dest_oper = match self.dest_name.as_str() {
            "Initial Attenuation" | "Initial Attenuation (dB)" | "Initial Attenuation (Volume dB)" => SF_GEN_INITIAL_ATTENUATION,
            "Initial Filter Cutoff" | "Filter Cutoff (Hz / Cents)" => SF_GEN_INITIAL_FILTER_FC,
            "Initial Filter Resonance" | "Filter Resonance (cB)" => SF_GEN_INITIAL_FILTER_Q,
            "Mod Env to Filter" | "Mod Env -> Filter Cutoff" => SF_GEN_MOD_ENV_TO_FILTER_FC,
            "Mod Env to Pitch" | "Mod Env -> Pitch" => SF_GEN_MOD_ENV_TO_PITCH,
            "Vibrato LFO Pitch Depth" | "Vibrato LFO -> Pitch" => SF_GEN_VIB_LFO_TO_PITCH,
            "Mod LFO Volume Depth" | "Mod LFO -> Volume (Tremolo)" => SF_GEN_MOD_LFO_TO_VOLUME,
            "Mod LFO Pitch Depth" | "Mod LFO -> Pitch (Vibrato)" => SF_GEN_MOD_LFO_TO_PITCH,
            "Mod LFO Filter Depth" | "Mod LFO -> Filter (Wah)" => SF_GEN_MOD_LFO_TO_FILTER_FC,
            "Pan Position" | "Stereo Pan" | "Stereo Pan [-100..+100]" => SF_GEN_PAN,
            "Reverb Effects Send" | "Reverb Send (%)" => SF_GEN_REVERB_EFFECTS_SEND,
            "Chorus Effects Send" | "Chorus Send (%)" => SF_GEN_CHORUS_EFFECTS_SEND,
            "Pitch (Fine Tune)" | "Fine Tune (Cents)" => SF_GEN_FINE_TUNE,
            "Vol Env Attack Time" | "Volume Attack Time" => SF_GEN_VOL_ENV_ATTACK,
            "Vol Env Decay Time" | "Volume Decay Time" => SF_GEN_VOL_ENV_DECAY,
            _ => SF_GEN_INITIAL_ATTENUATION,
        };
        let raw_amount = match dest_oper {
            SF_GEN_INITIAL_ATTENUATION | SF_GEN_MOD_LFO_TO_VOLUME | SF_GEN_REVERB_EFFECTS_SEND | SF_GEN_CHORUS_EFFECTS_SEND | SF_GEN_PAN => {
                (self.amount * 10.0) as i16
            }
            _ => self.amount as i16,
        };
        VHLModulator_v2 {
            src_oper,
            dest_oper,
            amt_src_oper: 0,
            amount: raw_amount,
            transform: if self.absolute_value { SF_TRANSFORM_ABSOLUTE } else { SF_TRANSFORM_LINEAR },
            flags: 1,
            index: self.id as u16,
        }
    }
}

pub struct ModulatorEditorView {
    pub entries: Vec<UiModulatorEntry>,
    pub selected_index: Option<usize>,
    pub selected_curve_type: u8,
    pub active_region_idx: usize,
    // Test bench controllers
    pub test_mod_wheel: f32, // CC1 (0..127)
    pub test_expression: f32, // CC11 (0..127)
    pub test_velocity: f32, // (0..127)
    pub test_pitch_bend: f32, // (-8192..8191)
}

impl Default for ModulatorEditorView {
    fn default() -> Self {
        let default_entries = vec![
            UiModulatorEntry {
                id: 1,
                src1_name: "Note-On Velocity".to_string(),
                curve_type: 1, // Concave
                is_bipolar: false,
                is_descending: true,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Initial Attenuation".to_string(),
                src2_name: "None".to_string(),
                amount: 96.0,
                unit: " dB",
                absolute_value: false,
            },
            UiModulatorEntry {
                id: 2,
                src1_name: "Note-On Velocity".to_string(),
                curve_type: 0, // Linear
                is_bipolar: false,
                is_descending: true,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Initial Filter Cutoff".to_string(),
                src2_name: "None".to_string(),
                amount: -2400.0,
                unit: " c",
                absolute_value: false,
            },
            UiModulatorEntry {
                id: 3,
                src1_name: "CC 1 (Modulation Wheel)".to_string(),
                curve_type: 0, // Linear
                is_bipolar: false,
                is_descending: false,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Vibrato LFO Pitch Depth".to_string(),
                src2_name: "None".to_string(),
                amount: 50.0,
                unit: " c",
                absolute_value: false,
            },
            UiModulatorEntry {
                id: 4,
                src1_name: "Pitch Wheel".to_string(),
                curve_type: 0, // Linear
                is_bipolar: true,
                is_descending: false,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Pitch (Fine Tune)".to_string(),
                src2_name: "Pitch Wheel Sensitivity".to_string(),
                amount: 200.0,
                unit: " c",
                absolute_value: false,
            },
            UiModulatorEntry {
                id: 5,
                src1_name: "CC 11 (Expression)".to_string(),
                curve_type: 1, // Concave
                is_bipolar: false,
                is_descending: true,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Initial Attenuation".to_string(),
                src2_name: "None".to_string(),
                amount: 96.0,
                unit: " dB",
                absolute_value: false,
            },
            UiModulatorEntry {
                id: 6,
                src1_name: "CC 91 (Reverb Send)".to_string(),
                curve_type: 0, // Linear
                is_bipolar: false,
                is_descending: false,
                custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                dest_name: "Reverb Effects Send".to_string(),
                src2_name: "None".to_string(),
                amount: 20.0,
                unit: " %",
                absolute_value: false,
            },
        ];

        Self {
            entries: default_entries,
            selected_index: Some(0),
            selected_curve_type: 1,
            active_region_idx: 0,
            test_mod_wheel: 0.0,
            test_expression: 127.0,
            test_velocity: 100.0,
            test_pitch_bend: 0.0,
        }
    }
}

impl ModulatorEditorView {
    pub fn sync_from_vhl(&mut self, mods: &[VHLModulator_v2]) {
        if !mods.is_empty() {
            self.entries = mods.iter().map(UiModulatorEntry::from).collect();
        }
    }

    pub fn to_vhl(&self) -> Vec<VHLModulator_v2> {
        self.entries.iter().map(|e| e.to_vhl()).collect()
    }

    pub fn get_custom_curves(&self) -> Vec<[f32; 5]> {
        self.entries.iter().map(|e| e.custom_points).collect()
    }

    pub fn show(&mut self, ui: &mut Ui, theme: &AppTheme, region_names: &[String]) -> ModulatorViewAction {
        let mut action = ModulatorViewAction::None;

        // Top Navigation Header
        ui.horizontal(|ui| {
            ui.add_space(8.0);
            if ui.button(egui::RichText::new("← Home").font(FontId::proportional(13.0))).clicked() {
                action = ModulatorViewAction::BackToLanding;
            }
            ui.add_space(6.0);
            if ui.button(egui::RichText::new("🎹 Sound Editor").font(FontId::proportional(13.0))).clicked() {
                action = ModulatorViewAction::OpenSoundEditor;
            }

            ui.add_space(14.0);
            ui.painter().circle_filled(ui.cursor().min + Vec2::new(4.0, 10.0), 4.5, theme.coral);
            ui.add_space(14.0);
            ui.label(
                egui::RichText::new("MODULATION MATRIX EDITOR · SOUNDFONT 2 DYNAMICS")
                    .font(FontId::proportional(14.0))
                    .color(theme.text)
                    .strong(),
            );
            widgets::live_tag(ui, theme);

            // Region selector on top right
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.add_space(8.0);
                if !region_names.is_empty() {
                    let cur_name = region_names.get(self.active_region_idx).cloned().unwrap_or_else(|| "Region #0".to_string());
                    egui::ComboBox::from_id_salt("mod_region_select")
                        .selected_text(cur_name)
                        .width(180.0)
                        .show_ui(ui, |ui| {
                            for (idx, name) in region_names.iter().enumerate() {
                                ui.selectable_value(&mut self.active_region_idx, idx, name);
                            }
                        });
                    ui.label(egui::RichText::new("ACTIVE REGION:").font(FontId::monospace(11.0)).color(theme.text_dim));
                }
            });
        });

        ui.add_space(10.0);

        // Main Layout: Modulator Table (Left/Center) + Curve Inspector / Test Bench (Right)
        ui.columns(2, |cols| {
            // Left Column: Modulator Matrix Table
            widgets::card(&mut cols[0], theme, |ui| {
                ui.horizontal(|ui| {
                    widgets::card_header(ui, theme, theme.coral, "ROUTING MATRIX (MODULATORS)", |ui| {
                        ui.horizontal(|ui| {
                            if ui.button(egui::RichText::new("+ Add Modulator").font(FontId::proportional(12.0))).clicked() {
                                let new_id = self.entries.len() + 1;
                                self.entries.push(UiModulatorEntry {
                                    id: new_id,
                                    src1_name: "Note-On Velocity".to_string(),
                                    curve_type: 0,
                                    is_bipolar: false,
                                    is_descending: false,
                                    custom_points: [0.0, 0.25, 0.5, 0.75, 1.0],
                                    dest_name: "Initial Filter Cutoff".to_string(),
                                    src2_name: "None".to_string(),
                                    amount: 1200.0,
                                    unit: " c",
                                    absolute_value: false,
                                });
                            }
                            if ui.button(egui::RichText::new("Reset to SF2 Defaults").font(FontId::proportional(12.0))).clicked() {
                                *self = ModulatorEditorView::default();
                            }
                        });
                    });
                });

                ui.add_space(6.0);

                // Modulator list scroll area
                egui::ScrollArea::vertical().max_height(480.0).show(ui, |ui| {
                    let mut delete_idx = None;

                    for (idx, entry) in self.entries.iter_mut().enumerate() {
                        let is_selected = self.selected_index == Some(idx);
                        let bg_color = if is_selected { theme.raised } else { theme.deep_well };

                        Frame::none()
                            .fill(bg_color)
                            .stroke(Stroke::new(1.0_f32, if is_selected { theme.coral } else { theme.hairline }))
                            .rounding(Rounding::same(6.0))
                            .inner_margin(Margin::symmetric(10.0, 8.0))
                            .show(ui, |ui| {
                                ui.horizontal(|ui| {
                                    ui.label(egui::RichText::new(format!("#{:02}", entry.id)).font(FontId::monospace(11.0)).color(theme.coral_text).strong());
                                    ui.add_space(6.0);

                                    // Source 1 Controller
                                    egui::ComboBox::from_id_salt(("src1_combo", idx))
                                        .selected_text(&entry.src1_name)
                                        .width(140.0)
                                        .show_ui(ui, |ui| {
                                            for name in [
                                                "Note-On Velocity",
                                                "Note-On Key Number",
                                                "Modulation Envelope",
                                                "Mod LFO",
                                                "Vibrato LFO",
                                                "Pitch Wheel",
                                                "Channel Pressure",
                                                "Poly Pressure",
                                                "CC 1 (Modulation Wheel)",
                                                "CC 2 (Breath)",
                                                "CC 7 (Volume)",
                                                "CC 10 (Pan)",
                                                "CC 11 (Expression)",
                                                "CC 64 (Sustain Pedal)",
                                                "CC 91 (Reverb Send)",
                                                "CC 93 (Chorus Send)",
                                            ] {
                                                ui.selectable_value(&mut entry.src1_name, name.to_string(), name);
                                            }
                                        });

                                    // Polyphone-style 16-curve + Custom expression picker
                                    let curve_text = entry.curve_display_name();
                                    ui.menu_button(egui::RichText::new(curve_text).size(11.0).color(if is_selected { theme.coral } else { theme.text }), |ui| {
                                        ui.set_min_width(340.0);
                                        ui.label(egui::RichText::new("SF2 SPEC EXPRESSION CURVES (16 TYPES)").strong().size(11.0).color(theme.gold));
                                        ui.add_space(4.0);

                                        egui::Grid::new(("curve_picker_grid", idx)).spacing([8.0, 6.0]).show(ui, |ui| {
                                            ui.label(egui::RichText::new("Shape").strong().size(10.5).color(theme.text_dim));
                                            ui.label(egui::RichText::new("Uni (0→1)").strong().size(10.5).color(theme.text_dim));
                                            ui.label(egui::RichText::new("Uni Inv (1→0)").strong().size(10.5).color(theme.text_dim));
                                            ui.label(egui::RichText::new("Bi (-1→1)").strong().size(10.5).color(theme.text_dim));
                                            ui.label(egui::RichText::new("Bi Inv (1→-1)").strong().size(10.5).color(theme.text_dim));
                                            ui.end_row();

                                            let shapes = [
                                                (0, "Linear", "∿"),
                                                (1, "Concave", "⌢"),
                                                (2, "Convex", "⌣"),
                                                (3, "Switch", "⎍"),
                                            ];

                                            for (stype, sname, sicon) in shapes {
                                                ui.label(egui::RichText::new(format!("{} {}", sicon, sname)).size(11.0).color(theme.text));
                                                for &(desc, bipol, label) in &[
                                                    (false, false, "0 → 1"),
                                                    (true, false, "1 → 0"),
                                                    (false, true, "-1 → 1"),
                                                    (true, true, "1 → -1"),
                                                ] {
                                                    let is_active = entry.curve_type == stype && entry.is_descending == desc && entry.is_bipolar == bipol;
                                                    let col = if is_active { theme.teal_text } else { theme.text };
                                                    let bg = if is_active { AppTheme::alpha(theme.teal, 60) } else { theme.deep_well };
                                                    let btn = egui::Button::new(egui::RichText::new(label).size(10.5).color(col)).fill(bg);
                                                    if ui.add(btn).clicked() {
                                                        entry.curve_type = stype;
                                                        entry.is_descending = desc;
                                                        entry.is_bipolar = bipol;
                                                        ui.close_menu();
                                                    }
                                                }
                                                ui.end_row();
                                            }
                                        });

                                        ui.separator();
                                        let is_cust = entry.curve_type == 4;
                                        let cust_btn = egui::Button::new(egui::RichText::new("✎ Custom Curve (User-Designed Multi-Point / Presets)").size(11.0).strong().color(if is_cust { theme.coral_text } else { theme.text }))
                                            .fill(if is_cust { AppTheme::alpha(theme.coral, 60) } else { theme.deep_well });
                                        if ui.add(cust_btn).clicked() {
                                            entry.curve_type = 4;
                                            self.selected_index = Some(idx);
                                            ui.close_menu();
                                        }
                                    });

                                    ui.label(egui::RichText::new("→").font(FontId::monospace(12.0)).color(theme.text_dim));

                                    // Destination Generator
                                    egui::ComboBox::from_id_salt(("dest_combo", idx))
                                        .selected_text(&entry.dest_name)
                                        .width(140.0)
                                        .show_ui(ui, |ui| {
                                            for name in [
                                                "Initial Filter Cutoff",
                                                "Initial Filter Resonance",
                                                "Initial Attenuation",
                                                "Pan Position",
                                                "Pitch (Fine Tune)",
                                                "Reverb Effects Send",
                                                "Chorus Effects Send",
                                                "Mod LFO Pitch Depth",
                                                "Mod LFO Filter Depth",
                                                "Mod LFO Volume Depth",
                                                "Vibrato LFO Pitch Depth",
                                                "Mod Env to Pitch",
                                                "Mod Env to Filter",
                                                "Vol Env Attack Time",
                                                "Vol Env Release Time",
                                            ] {
                                                if ui.selectable_value(&mut entry.dest_name, name.to_string(), name).clicked() {
                                                    match name {
                                                        "Initial Attenuation" | "Mod LFO Volume Depth" => {
                                                            entry.unit = " dB";
                                                            // CRITICAL BUG FIX: If user previously had 1200 cents, reset to safe dB range so sound doesn't drop to zero!
                                                            if entry.amount.abs() > 48.0 || entry.amount == 0.0 {
                                                                entry.amount = 12.0;
                                                            }
                                                        }
                                                        "Initial Filter Resonance" => {
                                                            entry.unit = " cB";
                                                            if entry.amount.abs() > 240.0 {
                                                                entry.amount = 30.0;
                                                            }
                                                        }
                                                        "Pan Position" | "Reverb Effects Send" | "Chorus Effects Send" => {
                                                            entry.unit = " %";
                                                            if entry.amount.abs() > 100.0 {
                                                                entry.amount = 50.0;
                                                            }
                                                        }
                                                        "Vol Env Attack Time" | "Vol Env Release Time" => {
                                                            entry.unit = " ms";
                                                            if entry.amount.abs() > 5000.0 {
                                                                entry.amount = 50.0;
                                                            }
                                                        }
                                                        _ => {
                                                            entry.unit = " c";
                                                            if entry.amount.abs() < 100.0 {
                                                                entry.amount = 1200.0;
                                                            }
                                                        }
                                                    };
                                                }
                                            }
                                        });

                                    // Amount with unit-aware safe clamping
                                    let (clamp_min, clamp_max, speed) = match entry.unit {
                                        " dB" => (-96.0, 96.0, 0.5),
                                        " %" => (-100.0, 100.0, 1.0),
                                        " cB" => (0.0, 240.0, 1.0),
                                        " ms" => (0.0, 5000.0, 5.0),
                                        _ => (-12000.0, 12000.0, 10.0),
                                    };
                                    ui.add(egui::DragValue::new(&mut entry.amount).range(clamp_min..=clamp_max).speed(speed).suffix(entry.unit));

                                    // Delete button
                                    if ui.button(egui::RichText::new("🗑").color(theme.coral)).clicked() {
                                        delete_idx = Some(idx);
                                    }
                                });
                            });

                        ui.add_space(4.0);
                    }

                    if let Some(i) = delete_idx {
                        self.entries.remove(i);
                    }
                });
            });

            // Right Column: Curve Visualizer & Controller Test Bench
            cols[1].vertical(|ui| {
                let sel_idx = self.selected_index.unwrap_or(0).min(self.entries.len().saturating_sub(1));
                if let Some(entry) = self.entries.get_mut(sel_idx) {
                    if entry.curve_type == 4 {
                        // 1. CUSTOM CURVE DESIGNER
                        widgets::card(ui, theme, |ui| {
                            widgets::card_header(ui, theme, theme.coral, "CUSTOM CURVE DESIGNER (MULTI-NODE)", |ui| {
                                widgets::status_pill(ui, theme.coral, AppTheme::alpha(theme.coral, 30), theme.coral_text, "USER DEFINED");
                            });
                            ui.add_space(4.0);

                            // Canvas plot
                            draw_curve_plot(ui, theme, entry);
                            ui.add_space(6.0);

                            // Node Sliders
                            ui.label(egui::RichText::new("CONTROL NODES (0.0 .. 1.0):").font(FontId::monospace(10.5)).color(theme.text_dim));
                            ui.horizontal_wrapped(|ui| {
                                for (i, p) in entry.custom_points.iter_mut().enumerate() {
                                    ui.horizontal(|ui| {
                                        ui.label(egui::RichText::new(format!("P{}:", i)).font(FontId::monospace(11.0)).color(theme.coral_text));
                                        ui.add(egui::DragValue::new(p).speed(0.01).range(0.0..=1.0));
                                    });
                                }
                            });

                            ui.add_space(6.0);
                            ui.label(egui::RichText::new("QUICK PRESETS:").font(FontId::monospace(10.5)).color(theme.text_dim));
                            ui.horizontal_wrapped(|ui| {
                                if ui.button("S-Curve").clicked() {
                                    entry.custom_points = [0.0, 0.08, 0.5, 0.92, 1.0];
                                }
                                if ui.button("Exponential").clicked() {
                                    entry.custom_points = [0.0, 0.06, 0.25, 0.56, 1.0];
                                }
                                if ui.button("Bell / Peak").clicked() {
                                    entry.custom_points = [0.0, 0.75, 1.0, 0.75, 0.0];
                                }
                                if ui.button("4-Step Gate").clicked() {
                                    entry.custom_points = [0.0, 0.33, 0.66, 0.66, 1.0];
                                }
                                if ui.button("Inverted S").clicked() {
                                    entry.custom_points = [1.0, 0.92, 0.5, 0.08, 0.0];
                                }
                                if ui.button("Reset Linear").clicked() {
                                    entry.custom_points = [0.0, 0.25, 0.5, 0.75, 1.0];
                                }
                            });
                        });
                    } else {
                        // 1. STANDARD SF2 CURVE VISUALIZER
                        widgets::card(ui, theme, |ui| {
                            widgets::card_header(ui, theme, theme.gold, "EXPRESSION TRANSFER CURVES (16 TYPES)", |ui| {
                                widgets::status_pill(ui, theme.gold, AppTheme::alpha(theme.gold, 30), theme.gold_text, "SF2 SPEC");
                            });
                            ui.add_space(4.0);

                            // Canvas plot for the active SF2 curve
                            draw_curve_plot(ui, theme, entry);
                            ui.add_space(6.0);

                            let evo_text = match (entry.is_descending, entry.is_bipolar) {
                                (false, false) => "Unipolar Ascending [0.0 → 1.0]",
                                (true, false) => "Unipolar Descending [1.0 → 0.0]",
                                (false, true) => "Bipolar Ascending [-1.0 → +1.0]",
                                (true, true) => "Bipolar Descending [+1.0 → -1.0]",
                            };
                            ui.label(egui::RichText::new(format!("Active: {} · {}", entry.curve_display_name(), evo_text)).font(FontId::monospace(11.0)).color(theme.teal_text));
                            ui.label(egui::RichText::new("Formula: Concave 96dB Ear Perception, Convex Saturation, Linear Ramp, Switch Gate.").font(FontId::monospace(10.5)).color(theme.text_dim));
                        });
                    }
                }

                ui.add_space(10.0);

                // 2. Real-Time Controller Test Bench
                widgets::card(ui, theme, |ui| {
                    widgets::card_header(ui, theme, theme.teal, "MIDI TEST BENCH (SIMULATE GESTURES)", |ui| {
                        widgets::live_tag(ui, theme);
                    });
                    ui.add_space(6.0);

                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("Key Velocity:").font(FontId::proportional(12.0)));
                        ui.add(egui::Slider::new(&mut self.test_velocity, 0.0..=127.0));
                    });
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("CC 1 Mod Wheel:").font(FontId::proportional(12.0)));
                        ui.add(egui::Slider::new(&mut self.test_mod_wheel, 0.0..=127.0));
                    });
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("CC 11 Expression:").font(FontId::proportional(12.0)));
                        ui.add(egui::Slider::new(&mut self.test_expression, 0.0..=127.0));
                    });
                    ui.horizontal(|ui| {
                        ui.label(egui::RichText::new("Pitch Wheel:").font(FontId::proportional(12.0)));
                        ui.add(egui::Slider::new(&mut self.test_pitch_bend, -8192.0..=8191.0));
                    });
                });
            });
        });

        action
    }
}

fn draw_curve_plot(ui: &mut Ui, theme: &AppTheme, entry: &mut UiModulatorEntry) {
    let desired_size = Vec2::new(ui.available_width(), 120.0);
    let (rect, resp) = ui.allocate_exact_size(desired_size, egui::Sense::click_and_drag());
    let painter = ui.painter();

    // Background
    painter.rect_filled(rect, Rounding::same(6.0), theme.deep_well);
    painter.rect_stroke(rect, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));

    // Mid grid lines
    let mid_y = rect.center().y;
    let mid_x = rect.center().x;
    painter.line_segment([Pos2::new(rect.min.x, mid_y), Pos2::new(rect.max.x, mid_y)], Stroke::new(0.5_f32, AppTheme::alpha(theme.text_dim, 40)));
    painter.line_segment([Pos2::new(mid_x, rect.min.y), Pos2::new(mid_x, rect.max.y)], Stroke::new(0.5_f32, AppTheme::alpha(theme.text_dim, 40)));

    // Plot curve line
    let samples = 64;
    let mut points = Vec::with_capacity(samples);
    for i in 0..samples {
        let t = i as f32 / (samples - 1) as f32; // 0..1
        let val = if entry.curve_type == 4 {
            crate::audio_engine::ModCurves::eval_custom(t, &entry.custom_points)
        } else {
            let v = (t * 127.0).round() as u8;
            crate::audio_engine::ModCurves::eval_source(v, entry.curve_type, entry.is_descending, entry.is_bipolar)
        };
        let norm_y = if entry.is_bipolar {
            (1.0 - (val * 0.5 + 0.5)).clamp(0.0, 1.0)
        } else {
            (1.0 - val).clamp(0.0, 1.0)
        };
        let px = rect.min.x + 8.0 + t * (rect.width() - 16.0);
        let py = rect.min.y + 8.0 + norm_y * (rect.height() - 16.0);
        points.push(Pos2::new(px, py));
    }

    let line_col = if entry.curve_type == 4 { theme.coral } else { theme.teal };
    painter.add(egui::Shape::line(points, Stroke::new(2.0_f32, line_col)));

    // If custom, draw 5 control points as glowing nodes and handle dragging!
    if entry.curve_type == 4 {
        for (i, p_val) in entry.custom_points.iter_mut().enumerate() {
            let t = i as f32 / 4.0;
            let norm_y = (1.0 - *p_val).clamp(0.0, 1.0);
            let px = rect.min.x + 8.0 + t * (rect.width() - 16.0);
            let py = rect.min.y + 8.0 + norm_y * (rect.height() - 16.0);
            let node_pos = Pos2::new(px, py);

            painter.circle_filled(node_pos, 5.0, theme.coral);
            painter.circle_stroke(node_pos, 5.0, Stroke::new(1.5_f32, Color32::WHITE));

            if let Some(mouse_pos) = resp.interact_pointer_pos() {
                if (mouse_pos.x - px).abs() < 24.0 {
                    let new_val = 1.0 - ((mouse_pos.y - rect.min.y - 8.0) / (rect.height() - 16.0)).clamp(0.0, 1.0);
                    *p_val = new_val;
                }
            }
        }
    }
}
