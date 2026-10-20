//! Small reusable UI pieces so every panel looks the same:
//! cards, caps labels, value chips, pills, icon buttons, transport buttons.

use crate::theme::AppTheme;
use egui::{Align2, Color32, FontId, Frame, Margin, Pos2, Response, RichText, Rounding, Sense, Stroke, Ui, Vec2};

/// Small ALL-CAPS dim label ("ATTACK", "SPLIT POINT" …).
pub fn caps(theme: &AppTheme, s: &str) -> RichText {
    RichText::new(s).size(10.5).strong().color(theme.text_dim)
}

/// Monospace value text.
pub fn mono(s: impl Into<String>, size: f32, color: Color32) -> RichText {
    RichText::new(s.into()).monospace().size(size).color(color)
}

/// Rounded panel card. Content gets a 12 px margin.
pub fn card<R>(ui: &mut Ui, theme: &AppTheme, add: impl FnOnce(&mut Ui) -> R) -> R {
    Frame::none()
        .fill(theme.panel)
        .stroke(Stroke::new(1.0_f32, theme.hairline))
        .rounding(Rounding::same(8.0))
        .inner_margin(Margin::same(12.0))
        .show(ui, |ui| {
            ui.set_min_width(ui.available_width()); // cards always span their column
            add(ui)
        })
        .inner
}

/// Card title row: coloured square, CAPS title, optional right-aligned content.
pub fn card_header(ui: &mut Ui, theme: &AppTheme, dot: Color32, title: &str, right: impl FnOnce(&mut Ui)) {
    ui.horizontal(|ui| {
        let (r, _) = ui.allocate_exact_size(Vec2::splat(8.0), Sense::hover());
        ui.painter().rect_filled(r, Rounding::same(2.0), dot);
        let room = ui.available_width();
        ui.add(egui::Label::new(caps(theme, title)).truncate());
        if room > 300.0 {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), right);
        }
    });
}

/// Small "LIVE" tag.
pub fn live_tag(ui: &mut Ui, theme: &AppTheme) {
    Frame::none()
        .fill(AppTheme::alpha(theme.teal, 36))
        .rounding(Rounding::same(3.0))
        .inner_margin(Margin::symmetric(6.0, 1.0))
        .show(ui, |ui| {
            ui.label(RichText::new("LIVE").monospace().size(10.0).strong().color(theme.teal_text));
        });
}

pub const CHIP_W: f32 = 94.0;
pub const CHIP_H: f32 = 46.0;

/// Reserve a fixed-size chip, paint its background, and return a child Ui for the content.
fn chip_frame(ui: &mut Ui, theme: &AppTheme) -> Ui {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(CHIP_W, CHIP_H), Sense::hover());
    ui.painter().rect(rect, Rounding::same(6.0), theme.deep_well, Stroke::new(1.0_f32, theme.hairline));
    let mut child = ui.new_child(
        egui::UiBuilder::new().max_rect(rect.shrink2(Vec2::new(8.0, 5.0))).layout(egui::Layout::top_down(egui::Align::Min)),
    );
    child.spacing_mut().item_spacing.y = 1.0;
    child.style_mut().wrap_mode = Some(egui::TextWrapMode::Truncate);
    child
}

/// Editable value in a chip: CAPS label above, draggable number below.
pub fn value_chip(
    ui: &mut Ui,
    theme: &AppTheme,
    label: &str,
    val: &mut f32,
    range: std::ops::RangeInclusive<f32>,
    suffix: &str,
    decimals: usize,
) -> bool {
    let span = (*range.end() - *range.start()).max(1.0);
    let mut c = chip_frame(ui, theme);
    c.label(caps(theme, label));
    // Flat number field: no button background, monospace.
    let w = &mut c.visuals_mut().widgets;
    for s in [&mut w.inactive, &mut w.hovered, &mut w.active] {
        s.bg_fill = Color32::TRANSPARENT;
        s.weak_bg_fill = Color32::TRANSPARENT;
        s.bg_stroke = Stroke::NONE;
    }
    c.style_mut().override_font_id = Some(FontId::monospace(12.5));
    c.spacing_mut().button_padding = Vec2::ZERO;
    c.add(egui::DragValue::new(val).range(range).speed(span / 400.0).max_decimals(decimals).suffix(suffix)).changed()
}

/// Read-only chip (CAPS label + value).
pub fn badge_chip(ui: &mut Ui, theme: &AppTheme, label: &str, value: &str) {
    let mut c = chip_frame(ui, theme);
    c.label(caps(theme, label));
    c.label(mono(value, 11.5, theme.text));
}

/// Width `status_pill` will take for `text` (empty text = dot only). Lets layouts budget space exactly.
pub fn status_pill_width(ui: &Ui, text: &str) -> f32 {
    if text.is_empty() {
        28.0
    } else {
        ui.painter().layout_no_wrap(text.to_string(), FontId::proportional(12.0), Color32::WHITE).size().x + 44.0
    }
}

/// Rounded status pill with a glowing dot ("● ESP32 offline"). Empty text = just the dot.
pub fn status_pill(ui: &mut Ui, color: Color32, fill: Color32, text_color: Color32, text: &str) -> Response {
    let w = status_pill_width(ui, text);
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(w, 28.0), Sense::hover());
    let p = ui.painter();
    p.rect(rect, Rounding::same(14.0), fill, Stroke::new(1.0_f32, AppTheme::alpha(color, 120)));
    let c = if text.is_empty() { rect.center() } else { Pos2::new(rect.min.x + 17.0, rect.center().y) };
    p.circle_filled(c, 6.0, AppTheme::alpha(color, 50));
    p.circle_filled(c, 3.5, color);
    if !text.is_empty() {
        let galley = p.layout_no_wrap(text.to_string(), FontId::proportional(12.0), text_color);
        p.galley(Pos2::new(rect.min.x + 30.0, rect.center().y - galley.size().y * 0.5), galley, text_color);
    }
    resp
}

/// Solid accent button (Compile VHL).
pub fn primary_button(ui: &mut Ui, theme: &AppTheme, text: &str) -> Response {
    ui.add(
        egui::Button::new(RichText::new(text).strong().color(theme.on_accent))
            .fill(theme.teal)
            .stroke(Stroke::NONE)
            .min_size(Vec2::new(0.0, 32.0)),
    )
}

/// Normal secondary button.
pub fn ghost_button(ui: &mut Ui, text: &str) -> Response {
    ui.add(egui::Button::new(RichText::new(text).size(12.5)).min_size(Vec2::new(0.0, 32.0)))
}

/// Square icon-only toggle (panel visibility, theme). Returns true when clicked.
pub fn icon_toggle(ui: &mut Ui, theme: &AppTheme, icon: &str, tip: &str, on: bool) -> bool {
    let (rect, resp) = ui.allocate_exact_size(Vec2::splat(32.0), Sense::click());
    let resp = resp.on_hover_text(tip);
    let (fill, stroke, fg) = if on {
        (AppTheme::alpha(theme.teal, 40), theme.teal, theme.teal_text)
    } else if resp.hovered() {
        (theme.raised, theme.outline, theme.text)
    } else {
        (Color32::TRANSPARENT, theme.hairline, theme.text_dim)
    };
    ui.painter().rect(rect, Rounding::same(6.0), fill, Stroke::new(1.0_f32, stroke));
    ui.painter().text(rect.center(), Align2::CENTER_CENTER, icon, FontId::proportional(17.0), fg);
    resp.clicked()
}

#[derive(Clone, Copy, PartialEq)]
pub enum TransportKind {
    Record,
    Play,
    Neutral,
}

/// Transport button with fixed width (REC / PLAY / PAUSE / STOP).
#[allow(dead_code)]
pub fn transport_button(ui: &mut Ui, theme: &AppTheme, kind: TransportKind, text: &str, enabled: bool, armed: bool) -> Response {
    transport_button_w(ui, theme, kind, text, enabled, armed, 82.0)
}

/// Same, with an explicit width (REC / STOP REC must not change size when toggled).
pub fn transport_button_w(ui: &mut Ui, theme: &AppTheme, kind: TransportKind, text: &str, enabled: bool, armed: bool, width: f32) -> Response {
    let (fill, stroke, fg) = match kind {
        TransportKind::Record => {
            let f = if armed { theme.rec_red } else { AppTheme::alpha(theme.rec_red, 28) };
            (f, AppTheme::alpha(theme.rec_red, 160), if armed { Color32::WHITE } else { theme.rec_red })
        }
        TransportKind::Play => (AppTheme::alpha(theme.teal, 40), theme.teal, theme.teal_text),
        TransportKind::Neutral => (theme.raised, theme.outline, theme.text),
    };
    let (fill, stroke, fg) = if enabled {
        (fill, stroke, fg)
    } else {
        (theme.raised, theme.hairline, AppTheme::alpha(theme.text_dim, 190))
    };
    let btn = egui::Button::new(RichText::new(text).size(12.0).strong().color(fg))
        .fill(fill)
        .stroke(Stroke::new(1.0_f32, stroke))
        .min_size(Vec2::new(width, 36.0));
    ui.add_enabled(enabled, btn)
}

/// Collapsible section: CAPS title, caret, optional right-hand text. Flat (no indent),
/// open/closed state kept in egui memory under `id_salt`.
pub fn section<R>(ui: &mut Ui, theme: &AppTheme, id_salt: &str, title: &str, right: &str, default_open: bool, add: impl FnOnce(&mut Ui) -> R) {
    let id = ui.make_persistent_id(("section", id_salt));
    let mut open = ui.data(|d| d.get_temp::<bool>(id)).unwrap_or(default_open);
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 26.0), Sense::click());
    if resp.clicked() {
        open = !open;
    }
    ui.data_mut(|d| d.insert_temp(id, open));
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        if resp.hovered() {
            p.rect_filled(rect, Rounding::same(4.0), AppTheme::alpha(theme.text, 10));
        }
        let c = Pos2::new(rect.min.x + 8.0, rect.center().y);
        let tri = if open {
            vec![c + Vec2::new(-4.0, -2.5), c + Vec2::new(4.0, -2.5), c + Vec2::new(0.0, 3.0)]
        } else {
            vec![c + Vec2::new(-2.5, -4.0), c + Vec2::new(-2.5, 4.0), c + Vec2::new(3.0, 0.0)]
        };
        p.add(egui::Shape::convex_polygon(tri, theme.text_dim, Stroke::NONE));
        p.text(Pos2::new(rect.min.x + 20.0, rect.center().y), Align2::LEFT_CENTER, title, FontId::proportional(10.5), theme.text_dim);
        if !right.is_empty() {
            p.text(Pos2::new(rect.max.x - 6.0, rect.center().y), Align2::RIGHT_CENTER, right, FontId::monospace(10.5), theme.text_dim);
        }
    }
    if open {
        add(ui);
    }
}