mod theme;
mod app;
pub mod vhl;
pub mod vhr;
pub mod audio_engine;
pub mod visuals;
mod widgets; 
mod icons;
pub mod project;
pub mod landing;
pub mod modulator_editor;

use app::HarmoniumApp;

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([1180.0, 720.0])
            .with_min_inner_size([960.0, 580.0])
            .with_title("Virtual Harmonium Workstation"),
        ..Default::default()
    };

    eframe::run_native(
        "Virtual Harmonium Workstation",
        options,
        Box::new(|cc| Ok(Box::new(HarmoniumApp::new(cc)))),
    )
}
