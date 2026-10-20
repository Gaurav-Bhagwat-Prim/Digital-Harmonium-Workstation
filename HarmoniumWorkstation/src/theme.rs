use egui::{Color32, FontFamily, FontId, Rounding, Stroke, Style, TextStyle, Visuals};

/// Theme modes supported by Harmonium Workstation
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThemeMode {
    Dark,
    Light,
}

impl ThemeMode {
    pub fn toggled(self) -> Self {
        match self {
            ThemeMode::Dark => ThemeMode::Light,
            ThemeMode::Light => ThemeMode::Dark,
        }
    }
}

/// Palette for the "Modern Raga Studio" look (matches the Style guide board).
///
/// `teal`, `coral` and `gold` are the *fill* colours (bars, curves, dots).
/// `*_text` are the same hues tuned for small text: identical in dark mode,
/// darker in light mode so they keep enough contrast on pale panels.
#[derive(Debug, Clone, Copy)]
pub struct AppTheme {
    pub mode: ThemeMode,

    // Structural surfaces
    pub window: Color32,
    pub deep_well: Color32,
    pub panel: Color32,
    pub raised: Color32,
    pub hairline: Color32,
    pub outline: Color32,

    // Text
    pub text: Color32,
    pub text_dim: Color32,

    // Accents (fills)
    pub teal: Color32,
    pub coral: Color32,
    pub gold: Color32,
    pub rec_red: Color32,

    // Accents (small text)
    pub teal_text: Color32,
    pub coral_text: Color32,
    pub gold_text: Color32,

    // Text drawn on top of a solid teal / red button
    pub on_accent: Color32,

    // Piano keys
    pub key_white_left: Color32,
    pub key_white_right: Color32,
    pub key_black_left: Color32,
    pub key_black_right: Color32,
    pub key_edge: Color32,
    pub keycap_on_white: Color32,
    pub keycap_on_black: Color32,

    // Tint strength of the loop region in the waveform (alpha 0..255)
    pub loop_tint: u8,
}

impl AppTheme {
    pub const DARK: Self = Self {
        mode: ThemeMode::Dark,
        window: Color32::from_rgb(20, 23, 28),      // #14171C
        deep_well: Color32::from_rgb(14, 17, 21),   // #0E1115
        panel: Color32::from_rgb(28, 32, 40),       // #1C2028
        raised: Color32::from_rgb(36, 41, 52),      // #242934
        hairline: Color32::from_rgb(43, 49, 61),    // #2B313D
        outline: Color32::from_rgb(51, 58, 72),     // #333A48
        text: Color32::from_rgb(230, 233, 239),     // #E6E9EF
        text_dim: Color32::from_rgb(152, 161, 179), // #98A1B3
        teal: Color32::from_rgb(43, 196, 176),      // #2BC4B0
        coral: Color32::from_rgb(240, 115, 106),    // #F0736A
        gold: Color32::from_rgb(242, 184, 75),      // #F2B84B
        rec_red: Color32::from_rgb(255, 77, 90),    // #FF4D5A
        teal_text: Color32::from_rgb(94, 224, 207), // #5EE0CF
        coral_text: Color32::from_rgb(255, 154, 146), // #FF9A92
        gold_text: Color32::from_rgb(242, 184, 75), // #F2B84B
        on_accent: Color32::from_rgb(14, 17, 21),
        key_white_left: Color32::from_rgb(222, 238, 235),
        key_white_right: Color32::from_rgb(245, 226, 223),
        key_black_left: Color32::from_rgb(14, 40, 43),
        key_black_right: Color32::from_rgb(46, 20, 24),
        key_edge: Color32::from_rgb(8, 10, 13),
        keycap_on_white: Color32::from_rgb(42, 51, 64),
        keycap_on_black: Color32::from_rgb(201, 210, 223),
        loop_tint: 18,
    };

    pub const LIGHT: Self = Self {
        mode: ThemeMode::Light,
        window: Color32::from_rgb(238, 241, 245),   // #EEF1F5
        deep_well: Color32::from_rgb(255, 255, 255),// #FFFFFF
        panel: Color32::from_rgb(250, 251, 253),    // #FAFBFD
        raised: Color32::from_rgb(241, 244, 248),   // #F1F4F8
        hairline: Color32::from_rgb(221, 226, 234), // #DDE2EA
        outline: Color32::from_rgb(197, 205, 218),  // #C5CDDA
        text: Color32::from_rgb(27, 34, 48),        // #1B2230
        text_dim: Color32::from_rgb(91, 101, 119),  // #5B6577
        teal: Color32::from_rgb(11, 133, 117),      // #0B8575
        coral: Color32::from_rgb(224, 87, 77),      // #E0574D
        gold: Color32::from_rgb(217, 150, 26),      // #D9961A
        rec_red: Color32::from_rgb(220, 38, 38),    // #DC2626
        teal_text: Color32::from_rgb(10, 111, 97),  // #0A6F61
        coral_text: Color32::from_rgb(182, 58, 49), // #B63A31
        gold_text: Color32::from_rgb(154, 91, 0),   // #9A5B00
        on_accent: Color32::WHITE,
        key_white_left: Color32::from_rgb(236, 247, 245),
        key_white_right: Color32::from_rgb(253, 240, 238),
        key_black_left: Color32::from_rgb(22, 57, 60),
        key_black_right: Color32::from_rgb(58, 24, 28),
        key_edge: Color32::from_rgb(150, 160, 175),
        keycap_on_white: Color32::from_rgb(42, 51, 64),
        keycap_on_black: Color32::from_rgb(201, 210, 223),
        loop_tint: 26,
    };

    pub fn for_mode(mode: ThemeMode) -> Self {
        match mode {
            ThemeMode::Dark => Self::DARK,
            ThemeMode::Light => Self::LIGHT,
        }
    }

    /// Same colour with a different opacity (0..=255).
    pub fn alpha(c: Color32, a: u8) -> Color32 {
        Color32::from_rgba_unmultiplied(c.r(), c.g(), c.b(), a)
    }

    /// Apply the theme to egui. Cheap, but only call it when the mode changes
    /// (see HarmoniumApp::update), not every frame.
    pub fn apply(&self, ctx: &egui::Context) {
        let mut style: Style = (*ctx.style()).clone();
        let mut v = match self.mode {
            ThemeMode::Dark => Visuals::dark(),
            ThemeMode::Light => Visuals::light(),
        };

        // Text colours are set per widget state instead of `override_text_color`,
        // so disabled widgets still look disabled and RichText colours always win.
        v.override_text_color = None;
        v.panel_fill = self.window;
        v.window_fill = self.panel;
        v.window_stroke = Stroke::new(1.0_f32, self.hairline);
        v.window_rounding = Rounding::same(8.0);
        v.menu_rounding = Rounding::same(6.0);
        v.extreme_bg_color = self.deep_well; // text edits, slider rails
        v.faint_bg_color = Self::alpha(self.text, 6); // striped grid rows
        v.hyperlink_color = self.teal_text;
        v.slider_trailing_fill = true;
        v.striped = true;

        let corner = Rounding::same(6.0);

        v.widgets.noninteractive.bg_fill = self.panel;
        v.widgets.noninteractive.weak_bg_fill = self.panel;
        v.widgets.noninteractive.bg_stroke = Stroke::new(1.0_f32, self.hairline);
        v.widgets.noninteractive.fg_stroke = Stroke::new(1.0_f32, self.text);
        v.widgets.noninteractive.rounding = corner;

        v.widgets.inactive.bg_fill = self.raised;
        v.widgets.inactive.weak_bg_fill = self.raised;
        v.widgets.inactive.bg_stroke = Stroke::new(1.0_f32, self.outline);
        v.widgets.inactive.fg_stroke = Stroke::new(1.0_f32, self.text);
        v.widgets.inactive.rounding = corner;

        let hover_fill = match self.mode {
            ThemeMode::Dark => Color32::from_rgb(45, 52, 66),
            ThemeMode::Light => Color32::from_rgb(228, 234, 242),
        };
        v.widgets.hovered.bg_fill = hover_fill;
        v.widgets.hovered.weak_bg_fill = hover_fill;
        v.widgets.hovered.bg_stroke = Stroke::new(1.0_f32, self.teal);
        v.widgets.hovered.fg_stroke = Stroke::new(1.0_f32, self.text);
        v.widgets.hovered.rounding = corner;

        v.widgets.active.bg_fill = Self::alpha(self.teal, 60);
        v.widgets.active.weak_bg_fill = Self::alpha(self.teal, 60);
        v.widgets.active.bg_stroke = Stroke::new(1.0_f32, self.teal);
        v.widgets.active.fg_stroke = Stroke::new(1.0_f32, self.text);
        v.widgets.active.rounding = corner;

        v.widgets.open.bg_fill = self.raised;
        v.widgets.open.weak_bg_fill = self.raised;
        v.widgets.open.bg_stroke = Stroke::new(1.0_f32, self.outline);
        v.widgets.open.fg_stroke = Stroke::new(1.0_f32, self.text);
        v.widgets.open.rounding = corner;

        v.selection.bg_fill = Self::alpha(self.teal, 90);
        v.selection.stroke = Stroke::new(1.0_f32, self.teal_text);

        style.visuals = v;
        style.spacing.item_spacing = egui::vec2(8.0, 6.0);
        style.spacing.button_padding = egui::vec2(10.0, 5.0);
        style.spacing.interact_size.y = 26.0;
        style.spacing.slider_width = 90.0;
        style.spacing.scroll.bar_width = 6.0;
        style.spacing.scroll.floating = true; // scrollbars float on top: no layout jump

        style.text_styles = [
            (TextStyle::Small, FontId::new(11.0, FontFamily::Proportional)),
            (TextStyle::Body, FontId::new(13.0, FontFamily::Proportional)),
            (TextStyle::Button, FontId::new(13.0, FontFamily::Proportional)),
            (TextStyle::Heading, FontId::new(16.0, FontFamily::Proportional)),
            (TextStyle::Monospace, FontId::new(12.5, FontFamily::Monospace)),
        ]
        .into();

        ctx.set_style(style);
    }
}