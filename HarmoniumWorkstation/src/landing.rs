//! Landing Page for Harmonium Workstation
//!
//! Provides a clean welcome screen with quick actions:
//! 1. Sound Editor (main synthesizer & multi-sample workspace)
//! 2. Open Project (browse and load existing .hwproj)
//! 3. Live Player (placeholder / disabled for future release)
//! 4. Modulation Editor (SoundFont 2 dynamic expression matrix)

use crate::theme::AppTheme;
use crate::widgets;
use egui::{Color32, CursorIcon, FontId, Frame, Margin, Pos2, Rect, Rounding, Sense, Stroke, Ui, Vec2};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LandingAction {
    None,
    OpenSoundEditor,
    OpenProject,
    OpenModulationEditor,
}

pub struct LandingPage;

impl LandingPage {
    pub fn show(
        ctx: &egui::Context,
        theme: &AppTheme,
        project_name: &str,
        sample_count: usize,
        region_count: usize,
        audio_rate: u32,
        is_audio_ok: bool,
    ) -> LandingAction {
        let mut action = LandingAction::None;

        egui::CentralPanel::default()
            .frame(Frame::none().fill(theme.window))
            .show(ctx, |ui| {
                let available_size = ui.available_size();
                // Background subtle grid decoration
                let painter = ui.painter();
                let screen_rect = ui.max_rect();
                for y in (screen_rect.min.y as i32..screen_rect.max.y as i32).step_by(40) {
                    painter.line_segment(
                        [Pos2::new(screen_rect.min.x, y as f32), Pos2::new(screen_rect.max.x, y as f32)],
                        Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 35)),
                    );
                }

                // Top branding header
                ui.horizontal(|ui| {
                    ui.add_space(20.0);
                    ui.vertical(|ui| {
                        ui.add_space(14.0);
                        ui.horizontal(|ui| {
                            ui.painter().circle_filled(ui.cursor().min + Vec2::new(6.0, 10.0), 5.0, theme.teal);
                            ui.add_space(16.0);
                            ui.label(egui::RichText::new("HARMONIUM WORKSTATION").font(FontId::proportional(15.0)).color(theme.text).strong());
                            widgets::status_pill(ui, theme.teal, AppTheme::alpha(theme.teal, 30), theme.teal_text, "V1.4.0 STUDIO");
                        });
                    });

                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.add_space(20.0);
                        if is_audio_ok {
                            widgets::status_pill(ui, theme.teal, AppTheme::alpha(theme.teal, 25), theme.teal_text, &format!("AUDIO: {} Hz", audio_rate));
                        } else {
                            widgets::status_pill(ui, theme.coral, AppTheme::alpha(theme.coral, 25), theme.coral_text, "AUDIO OFFLINE");
                        }
                    });
                });

                ui.add_space(32.0);

                // Central Hero Area
                let content_width = 820.0_f32.min(available_size.x - 40.0).max(400.0);
                ui.vertical_centered(|ui| {
                    ui.set_width(content_width);

                    // Hero Icon & Title
                    ui.label(egui::RichText::new("🎛").font(FontId::proportional(42.0)).color(theme.teal));
                    ui.add_space(6.0);
                    ui.label(
                        egui::RichText::new("Virtual Harmonium Workstation")
                            .font(FontId::proportional(28.0))
                            .color(theme.text)
                            .strong(),
                    );
                    ui.add_space(4.0);
                    ui.label(
                        egui::RichText::new("Professional Indian Harmonium Multi-Sample Studio & SoundFont 2 Sound Engine")
                            .font(FontId::proportional(14.0))
                            .color(theme.text_dim),
                    );

                    ui.add_space(36.0);

                    // 4 Main Action Cards Grid (2x2)
                    let card_w = ((content_width - 20.0) / 2.0).max(280.0);
                    let card_h = 125.0;

                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 20.0;
                        // Card 1: Sound Editor
                        if Self::render_card(
                            ui,
                            theme,
                            card_w,
                            card_h,
                            "🎹",
                            "Sound Editor",
                            "Full synthesizer workspace with multi-sample mapping, DAHDSR envelopes, biquad filter, and virtual keybed.",
                            theme.teal,
                            true,
                            None,
                        ) {
                            action = LandingAction::OpenSoundEditor;
                        }

                        // Card 2: Open Project
                        if Self::render_card(
                            ui,
                            theme,
                            card_w,
                            card_h,
                            "📂",
                            "Open Project",
                            "Browse and load an existing Harmonium Project (.hwproj) with all sample mappings and regions intact.",
                            theme.gold,
                            true,
                            None,
                        ) {
                            action = LandingAction::OpenProject;
                        }
                    });

                    ui.add_space(16.0);

                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 20.0;
                        // Card 3: Live Player (Placeholder / Disabled)
                        Self::render_card(
                            ui,
                            theme,
                            card_w,
                            card_h,
                            "▶",
                            "Live Player",
                            "Dedicated low-latency performance console with live tape deck replayer and ESP32 hardware sync.",
                            theme.text_dim,
                            false,
                            Some("COMING SOON"),
                        );

                        // Card 4: Modulation Editor
                        if Self::render_card(
                            ui,
                            theme,
                            card_w,
                            card_h,
                            "⚡",
                            "Modulation Editor",
                            "SoundFont 2 dynamic expression matrix. Connect physical controllers & 16 transfer curves to sound parameters.",
                            theme.coral,
                            true,
                            Some("NEW"),
                        ) {
                            action = LandingAction::OpenModulationEditor;
                        }
                    });

                    ui.add_space(32.0);

                    // Active Project Quick Info Bar
                    Frame::none()
                        .fill(theme.panel)
                        .stroke(Stroke::new(1.0_f32, theme.hairline))
                        .rounding(Rounding::same(6.0))
                        .inner_margin(Margin::symmetric(20.0, 10.0))
                        .show(ui, |ui| {
                            ui.horizontal(|ui| {
                                ui.label(egui::RichText::new("ACTIVE PROJECT:").font(FontId::monospace(11.0)).color(theme.text_dim));
                                ui.label(egui::RichText::new(project_name).font(FontId::monospace(11.5)).color(theme.teal_text).strong());
                                ui.add_space(12.0);
                                ui.label(egui::RichText::new("•").font(FontId::monospace(11.0)).color(theme.hairline));
                                ui.add_space(12.0);
                                ui.label(egui::RichText::new(format!("{} Samples", sample_count)).font(FontId::monospace(11.0)).color(theme.text));
                                ui.add_space(12.0);
                                ui.label(egui::RichText::new("•").font(FontId::monospace(11.0)).color(theme.hairline));
                                ui.add_space(12.0);
                                ui.label(egui::RichText::new(format!("{} Regions", region_count)).font(FontId::monospace(11.0)).color(theme.text));
                            });
                        });
                });
            });

        action
    }

    #[allow(clippy::too_many_arguments)]
    fn render_card(
        ui: &mut Ui,
        theme: &AppTheme,
        width: f32,
        height: f32,
        icon: &str,
        title: &str,
        desc: &str,
        accent: Color32,
        enabled: bool,
        badge: Option<&str>,
    ) -> bool {
        let (rect, response) = ui.allocate_exact_size(
            Vec2::new(width, height),
            if enabled { Sense::click() } else { Sense::hover() },
        );

        let is_hovered = enabled && response.hovered();
        if is_hovered {
            ui.ctx().set_cursor_icon(CursorIcon::PointingHand);
        }

        let bg_fill = if is_hovered {
            theme.raised
        } else {
            theme.panel
        };

        let stroke_color = if is_hovered {
            accent
        } else {
            theme.hairline
        };

        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, Rounding::same(8.0), bg_fill);
        painter.rect_stroke(rect, Rounding::same(8.0), Stroke::new(if is_hovered { 1.5_f32 } else { 1.0_f32 }, stroke_color));

        // Accent strip on left edge
        if enabled {
            let strip = Rect::from_min_max(rect.min, Pos2::new(rect.min.x + 4.0, rect.max.y));
            painter.rect_filled(strip, Rounding { nw: 8.0, sw: 8.0, ne: 0.0, se: 0.0 }, accent);
        }

        // Draw icon
        painter.text(
            Pos2::new(rect.min.x + 18.0, rect.min.y + 18.0),
            egui::Align2::LEFT_TOP,
            icon,
            FontId::proportional(24.0),
            if enabled { accent } else { theme.text_dim },
        );

        // Draw title
        painter.text(
            Pos2::new(rect.min.x + 56.0, rect.min.y + 16.0),
            egui::Align2::LEFT_TOP,
            title,
            FontId::proportional(15.5),
            if enabled { theme.text } else { theme.text_dim },
        );

        // Draw badge if present
        if let Some(b) = badge {
            let badge_bg = if enabled { accent } else { theme.hairline };
            let badge_text = if enabled { Color32::BLACK } else { theme.text_dim };
            let b_galley = painter.layout_no_wrap(b.to_string(), FontId::monospace(10.0), badge_text);
            let b_width = b_galley.size().x + 10.0;
            let b_height = b_galley.size().y + 4.0;
            let b_rect = Rect::from_min_size(
                Pos2::new(rect.max.x - b_width - 12.0, rect.min.y + 16.0),
                Vec2::new(b_width, b_height),
            );
            painter.rect_filled(b_rect, Rounding::same(4.0), badge_bg);
            painter.galley(Pos2::new(b_rect.min.x + 5.0, b_rect.min.y + 2.0), b_galley, badge_text);
        }

        // Draw description (wrapped)
        let desc_rect = Rect::from_min_max(
            Pos2::new(rect.min.x + 56.0, rect.min.y + 46.0),
            Pos2::new(rect.max.x - 14.0, rect.max.y - 10.0),
        );
        let galley = painter.layout(
            desc.to_string(),
            FontId::proportional(12.0),
            theme.text_dim,
            desc_rect.width(),
        );
        painter.galley(desc_rect.min, galley, theme.text_dim);

        enabled && response.clicked()
    }
}
