use egui::{Color32, Stroke, Style, Visuals, Rounding};

/// Theme palette for "Modern Raga Studio"
pub struct ModernRagaTheme;

impl ModernRagaTheme {
    // Primary Vibrant Accents
    pub const PEACOCK_TEAL: Color32 = Color32::from_rgb(0, 168, 150);
    pub const TEAL_HOVER: Color32 = Color32::from_rgb(2, 195, 154);
    pub const CORAL_VERMILLION: Color32 = Color32::from_rgb(240, 93, 94);
    pub const MARIGOLD_GOLD: Color32 = Color32::from_rgb(244, 162, 97);
    pub const EMERALD_ACTIVE: Color32 = Color32::from_rgb(52, 211, 153);

    // DAW Structural Surfaces
    pub const BG_DEEP: Color32 = Color32::from_rgb(20, 23, 28);
    pub const PANEL_SURFACE: Color32 = Color32::from_rgb(28, 32, 40);
    pub const CARD_SURFACE: Color32 = Color32::from_rgb(36, 41, 52);
    pub const BORDER_COLOR: Color32 = Color32::from_rgb(50, 58, 72);

    // Text & Indicators
    pub const TEXT_PRIMARY: Color32 = Color32::from_rgb(240, 244, 248);
    pub const TEXT_MUTED: Color32 = Color32::from_rgb(148, 163, 184);

    /// Apply the Modern Raga Studio styling to egui context
    pub fn apply(ctx: &egui::Context) {
        let mut style: Style = (*ctx.style()).clone();
        let mut visuals = Visuals::dark();

        visuals.override_text_color = Some(Self::TEXT_PRIMARY);
        visuals.panel_fill = Self::PANEL_SURFACE;
        visuals.window_fill = Self::CARD_SURFACE;
        visuals.window_stroke = Stroke::new(1.0_f32, Self::BORDER_COLOR);
        visuals.window_rounding = Rounding::same(6.0);

        // Widget styling (Non-interactive)
        visuals.widgets.noninteractive.bg_fill = Self::CARD_SURFACE;
        visuals.widgets.noninteractive.bg_stroke = Stroke::new(1.0_f32, Self::BORDER_COLOR);
        visuals.widgets.noninteractive.rounding = Rounding::same(4.0);

        // Inactive widgets (Buttons, inputs)
        visuals.widgets.inactive.bg_fill = Color32::from_rgb(33, 38, 48);
        visuals.widgets.inactive.bg_stroke = Stroke::new(1.0_f32, Self::BORDER_COLOR);
        visuals.widgets.inactive.rounding = Rounding::same(4.0);

        // Hovered widgets
        visuals.widgets.hovered.bg_fill = Color32::from_rgb(45, 52, 66);
        visuals.widgets.hovered.bg_stroke = Stroke::new(1.0_f32, Self::PEACOCK_TEAL);
        visuals.widgets.hovered.rounding = Rounding::same(4.0);

        // Active widgets (Pressed)
        visuals.widgets.active.bg_fill = Self::PEACOCK_TEAL;
        visuals.widgets.active.bg_stroke = Stroke::new(1.0_f32, Self::TEAL_HOVER);
        visuals.widgets.active.rounding = Rounding::same(4.0);

        // Selection / Accent color
        visuals.selection.bg_fill = Self::PEACOCK_TEAL;
        visuals.selection.stroke = Stroke::new(1.0_f32, Self::TEAL_HOVER);

        style.visuals = visuals;
        style.spacing.item_spacing = egui::vec2(8.0, 8.0);
        style.spacing.button_padding = egui::vec2(10.0, 6.0);
        ctx.set_style(style);
    }
}
