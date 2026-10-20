//! Icon font support (Phosphor Regular, bundled by the `egui-phosphor` crate).
//!
//! Every glyph that used to render as an empty square (emoji, ▼, ☀️ …) is replaced
//! by a constant from here. Use it inside any text:  `format!("{} Compile", icons::GEAR)`.

pub use egui_phosphor::regular::{
    ARROWS_CLOCKWISE as SYNC, BOOK_OPEN, DOWNLOAD_SIMPLE as EXPORT, FILE, FLOPPY_DISK as SAVE,
    FOLDER, FOLDER_OPEN as OPEN, GEAR_SIX as COMPILE, MAGNIFYING_GLASS as SEARCH, MOON, PAUSE,
    PLAY, PLUS, RECORD, SIDEBAR_SIMPLE as SIDEBAR_LEFT, SLIDERS_HORIZONTAL, STOP, SUN,
    TRASH, UPLOAD_SIMPLE as IMPORT, WAVES, X,
    CARET_DOWN, SPEAKER_HIGH, WARNING, WAVEFORM as WAV,
};

/// Call once at start-up (HarmoniumApp::new). Adds the icon font as a fallback
/// of the Proportional family, so icons can be mixed into normal text.
pub fn install_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();
    egui_phosphor::add_to_fonts(&mut fonts, egui_phosphor::Variant::Regular);
    ctx.set_fonts(fonts);
}