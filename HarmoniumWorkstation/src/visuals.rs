//! Custom painters for the studio rack.
//!
//! - Sample waveform (min/max peaks cached, loop region, real time axis)
//! - DAHDSR envelope (draggable handles, gradient fill)
//! - Biquad low-pass response (draggable cutoff/resonance handle)
//! - Region lanes (one bar per region over the keyboard), note waterfall and the 61-key piano, which all share one
//!   horizontal geometry (`KeyGeom`) so notes fall exactly onto their keys.

use crate::theme::AppTheme;
use crate::widgets::{self, caps, mono};
use egui::{Align2, Color32, CursorIcon, FontId, Mesh, Pos2, Rect, RichText, Rounding, Sense, Shape, Stroke, Ui, Vec2};

// ───────────────────────────── key geometry ─────────────────────────────

pub const KEY_COUNT: usize = 61; // C2 .. C7
const WHITE_COUNT: f32 = 36.0;

pub const fn is_black(idx: usize) -> bool {
    matches!(idx % 12, 1 | 3 | 6 | 8 | 10)
}

pub fn whites_before(idx: usize) -> usize {
    const IN_OCTAVE: [usize; 12] = [0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6];
    (idx / 12) * 7 + IN_OCTAVE[idx % 12]
}

/// Horizontal layout shared by zone header, waterfall and keyboard.
#[derive(Clone, Copy)]
pub struct KeyGeom {
    x0: f32,
    white_w: f32,
}

impl KeyGeom {
    pub fn new(rect: Rect) -> Self {
        Self { x0: rect.min.x, white_w: rect.width() / WHITE_COUNT }
    }
    /// (left, width) of a key, black keys narrower and centred on the gap.
    pub fn span(&self, idx: usize) -> (f32, f32) {
        let c = whites_before(idx) as f32 * self.white_w;
        if is_black(idx) {
            (self.x0 + c - 0.30 * self.white_w, 0.60 * self.white_w)
        } else {
            (self.x0 + c, self.white_w)
        }
    }
    fn center(&self, idx: usize) -> f32 {
        let (x, w) = self.span(idx);
        x + w / 2.0
    }
    /// x of the border between key `k-1` and key `k` (0 = left edge, KEY_COUNT = right edge).
    pub fn boundary(&self, k: usize) -> f32 {
        if k == 0 {
            self.x0
        } else if k >= KEY_COUNT {
            self.x0 + WHITE_COUNT * self.white_w
        } else {
            (self.center(k - 1) + self.center(k)) / 2.0
        }
    }
    /// Key whose slot contains `x`.
    pub fn key_at(&self, x: f32) -> usize {
        (0..KEY_COUNT).find(|&k| x < self.boundary(k + 1)).unwrap_or(KEY_COUNT - 1)
    }
}

/// Blend two opaque colours (t = 0 -> a, t = 1 -> b).
fn mix(a: Color32, b: Color32, t: f32) -> Color32 {
    let l = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * t).round().clamp(0.0, 255.0) as u8;
    Color32::from_rgb(l(a.r(), b.r()), l(a.g(), b.g()), l(a.b(), b.b()))
}

/// Per-key colour of the region that answers it (None = no region covers the key, so it is silent).
pub type KeyZones = [Option<Color32>; KEY_COUNT];

// ───────────────────────────── shared state types ─────────────────────────────

/// One region as drawn on the region map.
pub struct RegionBar {
    pub lo: u8,
    pub hi: u8,
    pub color: Color32,
    pub text_color: Color32,
    pub label: String,
    pub active: bool,
}

#[derive(Default)]
pub struct LaneEvent {
    pub select: Option<usize>,
    /// (region index, new key_lo, new key_hi) while an edge is being dragged
    pub range: Option<(usize, u8, u8)>,
}

/// Greedy interval packing: regions that don't overlap share a lane.
pub fn pack_lanes(bars: &[RegionBar]) -> (Vec<usize>, usize) {
    let mut ends: Vec<i32> = Vec::new();
    let mut lane_of = vec![0usize; bars.len()];
    let mut order: Vec<usize> = (0..bars.len()).collect();
    order.sort_by_key(|&i| (bars[i].lo, i));
    for i in order {
        let b = &bars[i];
        match ends.iter().position(|&e| e < b.lo as i32) {
            Some(l) => {
                ends[l] = b.hi as i32;
                lane_of[i] = l;
            }
            None => {
                ends.push(b.hi as i32);
                lane_of[i] = ends.len() - 1;
            }
        }
    }
    (lane_of, ends.len())
}

/// One note shown in the waterfall (live play trail).
#[derive(Clone, Copy)]
pub struct WaterNote {
    pub key: u8,
    pub t_on: f64,
    pub t_off: Option<f64>,
}

pub const WATERFALL_TRAIL_SECS: f32 = 2.0;

/// Min/max peaks per pixel column, rebuilt only when the sample, width or generation changes.
#[derive(Default)]
pub struct WaveformCache {
    key: (usize, usize, u64),
    peaks: Vec<(f32, f32)>,
}

pub struct VisualPainters;

impl VisualPainters {
    // ───────────────────────────── waveform ─────────────────────────────
    #[allow(clippy::too_many_arguments)]
    pub fn draw_waveform(
        ui: &mut Ui,
        theme: &AppTheme,
        cache: &mut WaveformCache,
        generation: u64,
        sample_name: &str,
        root_key: &mut u8,
        sample_rate: u32,
        pcm: &[i16],
        loop_start: &mut u32,
        loop_end: &mut u32,
        channels: u8,
    ) -> bool {
        let mut changed = false;
        let n_frames = (pcm.len() / channels.max(1) as usize) as u32;

        ui.horizontal(|ui| {
            let (r, _) = ui.allocate_exact_size(Vec2::splat(8.0), Sense::hover());
            ui.painter().rect_filled(r, Rounding::same(2.0), theme.teal);
            ui.label(caps(theme, "SAMPLE WAVEFORM"));
            ui.add(egui::Label::new(RichText::new(sample_name).strong().color(theme.teal_text)).truncate());
        });
        ui.add_space(2.0);
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing = Vec2::new(8.0, 4.0);
            ui.label(caps(theme, "ROOT MIDI"));
            changed |= ui.add(egui::DragValue::new(root_key).range(0..=127_u8)).changed();
            ui.label(caps(theme, "LOOP START"));
            changed |= ui.add(egui::DragValue::new(loop_start).range(0..=n_frames).speed(10).suffix(" fr")).changed();
            ui.label(caps(theme, "LOOP END"));
            changed |= ui.add(egui::DragValue::new(loop_end).range(0..=n_frames).speed(10).suffix(" fr")).changed();
            ui.label(mono(
                format!("{} · {} Hz · {} fr", if channels == 1 { "Mono" } else { "Stereo" }, sample_rate, n_frames),
                11.0,
                theme.text_dim,
            ));
        });
        ui.add_space(6.0);

        let (rect, _resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 84.0), Sense::click_and_drag());
        if !ui.is_rect_visible(rect) {
            return changed;
        }

        let ch = channels.max(1) as usize;
        let n = pcm.len() / ch; // frames
        let pad = 6.0;
        let x0 = rect.min.x + pad;
        let w = (rect.width() - 2.0 * pad).max(1.0);
        let top = rect.min.y + 16.0;
        let mid = (top + rect.max.y - 4.0) * 0.5;
        let half = (rect.max.y - 4.0 - top) * 0.5;

        // Interactive dragging of loop start & end lines
        if n > 0 && w > 10.0 {
            let cur_lx0 = x0 + (*loop_start as f32 / n as f32) * w;
            let cur_lx1 = x0 + (*loop_end as f32 / n as f32) * w;

            let hit_start = Rect::from_min_max(Pos2::new(cur_lx0 - 9.0, top - 6.0), Pos2::new(cur_lx0 + 9.0, rect.max.y));
            let hit_end = Rect::from_min_max(Pos2::new(cur_lx1 - 9.0, top - 6.0), Pos2::new(cur_lx1 + 9.0, rect.max.y));

            let id_start = ui.id().with("loop_start_drag");
            let id_end = ui.id().with("loop_end_drag");

            let resp_start = ui.interact(hit_start, id_start, Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);
            let resp_end = ui.interact(hit_end, id_end, Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);

            if resp_start.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let d = resp_start.drag_delta();
                let df = (d.x / w * n as f32) as i64;
                let max_st = (*loop_end as i64).saturating_sub(10).max(0);
                let new_val = ((*loop_start as i64 + df).clamp(0, max_st)) as u32;
                if new_val != *loop_start {
                    *loop_start = new_val;
                    changed = true;
                }
            } else if resp_end.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let d = resp_end.drag_delta();
                let df = (d.x / w * n as f32) as i64;
                let min_en = (*loop_start as i64 + 10).min(n as i64);
                let new_val = ((*loop_end as i64 + df).clamp(min_en, n as i64)) as u32;
                if new_val != *loop_end {
                    *loop_end = new_val;
                    changed = true;
                }
            }
        }

        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, Rounding::same(6.0), theme.deep_well);
        painter.rect_stroke(rect, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));

        // Loop region (behind the waveform)
        if n > 0 && *loop_end > *loop_start && *loop_end as usize <= n {
            let lx0 = x0 + *loop_start as f32 / n as f32 * w;
            let lx1 = x0 + *loop_end as f32 / n as f32 * w;
            let lr = Rect::from_min_max(Pos2::new(lx0, rect.min.y + 1.0), Pos2::new(lx1, rect.max.y - 1.0));
            painter.rect_filled(lr, Rounding::ZERO, AppTheme::alpha(theme.gold, theme.loop_tint));
        }

        painter.line_segment([Pos2::new(rect.min.x, mid), Pos2::new(rect.max.x, mid)], Stroke::new(1.0_f32, theme.hairline));

        // Time axis: ticks come from the real sample length
        if n > 0 && sample_rate > 0 {
            let dur = n as f32 / sample_rate as f32;
            let step = nice_step(dur);
            let mut t = 0.0_f32;
            let mut i = 0;
            while t <= dur * 1.001 && i < 12 {
                let x = x0 + t / dur * w;
                let (align, tx) = if i == 0 { (Align2::LEFT_TOP, x) } else { (Align2::CENTER_TOP, x) };
                painter.text(Pos2::new(tx, rect.min.y + 3.0), align, fmt_time(t), FontId::monospace(9.5), theme.text_dim);
                if i > 0 {
                    painter.line_segment([Pos2::new(x, top - 2.0), Pos2::new(x, rect.max.y - 4.0)], Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 120)));
                }
                t += step;
                i += 1;
            }
        }

        if n > 1 {
            let cols = w as usize;
            if n <= cols * 2 {
                // Few samples: draw the actual waveform as a line
                let pts: Vec<Pos2> = (0..n)
                    .map(|i| Pos2::new(x0 + i as f32 / (n - 1) as f32 * w, mid - pcm[i * ch] as f32 / 32768.0 * half))
                    .collect();
                painter.add(Shape::line(pts, Stroke::new(1.6_f32, theme.teal)));
            } else {
                // Many samples: cached min/max peaks drawn as one mesh
                let key = (n, cols, generation);
                if cache.key != key || cache.peaks.is_empty() {
                    cache.peaks.clear();
                    for px in 0..cols {
                        let a = px * n / cols;
                        let b = ((px + 1) * n / cols).clamp(a + 1, n);
                        let (mut lo, mut hi) = (i16::MAX, i16::MIN);
                        for &s in &pcm[a * ch..b * ch] {
                            lo = lo.min(s);
                            hi = hi.max(s);
                        }
                        cache.peaks.push((lo as f32 / 32768.0, hi as f32 / 32768.0));
                    }
                    cache.key = key;
                }
                let mut mesh = Mesh::default();
                let col_a = AppTheme::alpha(theme.teal, 235);
                for (px, &(lo, hi)) in cache.peaks.iter().enumerate() {
                    let x = x0 + px as f32 + 0.5;
                    let y_hi = mid - hi * half;
                    let y_lo = (mid - lo * half).max(y_hi + 1.0);
                    mesh.colored_vertex(Pos2::new(x, y_hi), col_a);
                    mesh.colored_vertex(Pos2::new(x, y_lo), col_a);
                    if px > 0 {
                        let i = (px * 2) as u32;
                        mesh.add_triangle(i - 2, i - 1, i);
                        mesh.add_triangle(i - 1, i + 1, i);
                    }
                }
                painter.add(Shape::mesh(mesh));
            }

            // Loop markers on top with draggable flag badges
            if *loop_end > *loop_start && *loop_end as usize <= n {
                for (frame, is_end) in [(*loop_start, false), (*loop_end, true)] {
                    let x = x0 + frame as f32 / n as f32 * w;
                    let col = if is_end { theme.coral } else { theme.gold };
                    painter.line_segment([Pos2::new(x, top - 4.0), Pos2::new(x, rect.max.y - 2.0)], Stroke::new(2.0_f32, col));
                    let s = if is_end { -1.0 } else { 1.0 };
                    painter.add(Shape::convex_polygon(
                        vec![Pos2::new(x, top - 4.0), Pos2::new(x + 12.0 * s, top - 4.0), Pos2::new(x, top + 6.0)],
                        col,
                        Stroke::NONE,
                    ));
                    painter.text(
                        Pos2::new(x + 6.0 * s, top - 1.0),
                        Align2::CENTER_CENTER,
                        if is_end { "E" } else { "S" },
                        FontId::monospace(8.0),
                        Color32::BLACK,
                    );
                }
            }
        }

        changed
    }

    // ───────────────────────────── envelope ─────────────────────────────
    #[allow(clippy::too_many_arguments)]
    pub fn draw_adsr_curve(
        ui: &mut Ui,
        theme: &AppTheme,
        layer: &mut usize,
        vol_delay_ms: &mut f32,
        vol_attack_ms: &mut f32,
        vol_hold_ms: &mut f32,
        vol_decay_ms: &mut f32,
        vol_sustain_pct: &mut f32,
        vol_release_ms: &mut f32,
        mod_delay_ms: &mut f32,
        mod_attack_ms: &mut f32,
        mod_hold_ms: &mut f32,
        mod_decay_ms: &mut f32,
        mod_sustain_pct: &mut f32,
        mod_release_ms: &mut f32,
    ) -> bool {
        let mut changed = false;
        let active_is_vol = *layer == 0;
        let active_color = if active_is_vol { theme.teal } else { theme.gold };

        widgets::card_header(ui, theme, active_color, "ENVELOPES · DUAL DAHDSR", |ui| {
            ui.horizontal(|ui| {
                ui.spacing_mut().item_spacing.x = 4.0;
                let vol_btn = ui.selectable_label(active_is_vol, egui::RichText::new("● VOL").color(theme.teal).monospace().size(11.0));
                if vol_btn.clicked() {
                    *layer = 0;
                }
                let mod_btn = ui.selectable_label(!active_is_vol, egui::RichText::new("● MOD").color(theme.gold).monospace().size(11.0));
                if mod_btn.clicked() {
                    *layer = 1;
                }
                widgets::live_tag(ui, theme);
            });
        });
        ui.add_space(6.0);

        let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 112.0), Sense::hover());

        let pad = 14.0;
        let usable = (rect.width() - 2.0 * pad).max(10.0);
        let x0 = rect.min.x + pad;
        let top = rect.min.y + 14.0;
        let base = rect.max.y - 22.0;
        let hr = (base - top).max(10.0);

        let sus_vis = 120.0_f32; // visually dedicated sustain bar width
        let vol_total = *vol_delay_ms + *vol_attack_ms + *vol_hold_ms + *vol_decay_ms + sus_vis + *vol_release_ms;
        let mod_total = *mod_delay_ms + *mod_attack_ms + *mod_hold_ms + *mod_decay_ms + sus_vis + *mod_release_ms;
        let total_time = vol_total.max(mod_total).max(300.0);
        let scale = usable / total_time;

        let points = |del: f32, a: f32, h: f32, d: f32, s: f32, r: f32| -> [Pos2; 7] {
            let p0 = Pos2::new(x0, base);
            let p_del = Pos2::new(x0 + del * scale, base);
            let p1 = Pos2::new(p_del.x + a * scale, top);
            let p2 = Pos2::new(p1.x + h * scale, top);
            let p3 = Pos2::new(p2.x + d * scale, base - (s / 100.0) * hr);
            let p4 = Pos2::new(p3.x + sus_vis * scale, p3.y);
            let p5 = Pos2::new(p4.x + r * scale, base);
            [p0, p_del, p1, p2, p3, p4, p5]
        };

        // Interaction for the active layer's nodes
        if ui.is_rect_visible(rect) {
            let id = ui.id().with(if active_is_vol { "adsr_nodes_vol" } else { "adsr_nodes_mod" });
            let (cur_del, cur_a, cur_h, cur_d, cur_s, cur_r) = if active_is_vol {
                (
                    &mut *vol_delay_ms,
                    &mut *vol_attack_ms,
                    &mut *vol_hold_ms,
                    &mut *vol_decay_ms,
                    &mut *vol_sustain_pct,
                    &mut *vol_release_ms,
                )
            } else {
                (
                    &mut *mod_delay_ms,
                    &mut *mod_attack_ms,
                    &mut *mod_hold_ms,
                    &mut *mod_decay_ms,
                    &mut *mod_sustain_pct,
                    &mut *mod_release_ms,
                )
            };

            let p = points(*cur_del, *cur_a, *cur_h, *cur_d, *cur_s, *cur_r);

            // Node 1: Delay end (drag horizontal)
            let resp_del = ui.interact(Rect::from_center_size(p[1], Vec2::splat(22.0)), id.with("del"), Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);
            if resp_del.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let dt = resp_del.drag_delta().x / scale.max(0.01);
                let new_del = (*cur_del + dt).clamp(0.0, 200.0);
                if new_del != *cur_del {
                    *cur_del = new_del;
                    changed = true;
                }
            }

            // Node 2: Attack peak (drag horizontal)
            let resp_a = ui.interact(Rect::from_center_size(p[2], Vec2::splat(22.0)), id.with("a"), Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);
            if resp_a.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let dt = resp_a.drag_delta().x / scale.max(0.01);
                let new_a = (*cur_a + dt).clamp(0.0, 500.0);
                if new_a != *cur_a {
                    *cur_a = new_a;
                    changed = true;
                }
            }

            // Node 3: Hold end (drag horizontal)
            let resp_h = ui.interact(Rect::from_center_size(p[3], Vec2::splat(22.0)), id.with("h"), Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);
            if resp_h.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let dt = resp_h.drag_delta().x / scale.max(0.01);
                let new_h = (*cur_h + dt).clamp(0.0, 200.0);
                if new_h != *cur_h {
                    *cur_h = new_h;
                    changed = true;
                }
            }

            // Node 4: Decay end / Sustain start (drag horizontal for decay, vertical for sustain)
            let resp_d = ui.interact(Rect::from_center_size(p[4], Vec2::splat(22.0)), id.with("d"), Sense::drag()).on_hover_cursor(CursorIcon::Grab);
            if resp_d.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::Grabbing);
                let d = resp_d.drag_delta();
                let dt = d.x / scale.max(0.01);
                let new_d = (*cur_d + dt).clamp(10.0, 1000.0);
                let new_s = (*cur_s - (d.y / hr * 100.0)).clamp(0.0, 100.0);
                if new_d != *cur_d || new_s != *cur_s {
                    *cur_d = new_d;
                    *cur_s = new_s;
                    changed = true;
                }
            }

            // Node 5: Sustain plateau end (drag vertical for sustain)
            let resp_s = ui.interact(Rect::from_center_size(p[5], Vec2::splat(22.0)), id.with("s"), Sense::drag()).on_hover_cursor(CursorIcon::ResizeVertical);
            if resp_s.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeVertical);
                let d = resp_s.drag_delta();
                let new_s = (*cur_s - (d.y / hr * 100.0)).clamp(0.0, 100.0);
                if new_s != *cur_s {
                    *cur_s = new_s;
                    changed = true;
                }
            }

            // Node 6: Release end (drag horizontal)
            let resp_r = ui.interact(Rect::from_center_size(p[6], Vec2::splat(22.0)), id.with("r"), Sense::drag()).on_hover_cursor(CursorIcon::ResizeHorizontal);
            if resp_r.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                let dt = resp_r.drag_delta().x / scale.max(0.01);
                let new_r = (*cur_r + dt).clamp(5.0, 1000.0);
                if new_r != *cur_r {
                    *cur_r = new_r;
                    changed = true;
                }
            }
        }

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect);
            painter.rect_filled(rect, Rounding::same(6.0), theme.deep_well);
            painter.rect_stroke(rect, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));
            for k in 0..=4 {
                let y = top + hr * k as f32 / 4.0;
                painter.line_segment([Pos2::new(rect.min.x + 6.0, y), Pos2::new(rect.max.x - 6.0, y)], Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 150)));
            }

            let p_vol = points(*vol_delay_ms, *vol_attack_ms, *vol_hold_ms, *vol_decay_ms, *vol_sustain_pct, *vol_release_ms);
            let p_mod = points(*mod_delay_ms, *mod_attack_ms, *mod_hold_ms, *mod_decay_ms, *mod_sustain_pct, *mod_release_ms);

            let draw_curve = |painter: &egui::Painter, p: &[Pos2; 7], color: Color32, is_active: bool| {
                if is_active {
                    let mut mesh = Mesh::default();
                    let c_top = AppTheme::alpha(color, 90);
                    let c_bot = AppTheme::alpha(color, 6);
                    for w in p.windows(2) {
                        let i = mesh.vertices.len() as u32;
                        mesh.colored_vertex(w[0], if w[0].y >= base { c_bot } else { c_top });
                        mesh.colored_vertex(w[1], if w[1].y >= base { c_bot } else { c_top });
                        mesh.colored_vertex(Pos2::new(w[1].x, base), c_bot);
                        mesh.colored_vertex(Pos2::new(w[0].x, base), c_bot);
                        mesh.add_triangle(i, i + 1, i + 2);
                        mesh.add_triangle(i, i + 2, i + 3);
                    }
                    painter.add(Shape::mesh(mesh));
                    painter.add(Shape::line(p.to_vec(), Stroke::new(2.4_f32, color)));

                    for (i, pt) in p.iter().enumerate().skip(1) {
                        let (r, fill) = if i == 5 { (3.0, theme.deep_well) } else { (4.5, theme.panel) };
                        painter.circle_filled(*pt, r, fill);
                        painter.circle_stroke(*pt, r, Stroke::new(2.0_f32, color));
                    }
                } else {
                    let mut mesh = Mesh::default();
                    let c_top = AppTheme::alpha(color, 25);
                    let c_bot = AppTheme::alpha(color, 2);
                    for w in p.windows(2) {
                        let i = mesh.vertices.len() as u32;
                        mesh.colored_vertex(w[0], if w[0].y >= base { c_bot } else { c_top });
                        mesh.colored_vertex(w[1], if w[1].y >= base { c_bot } else { c_top });
                        mesh.colored_vertex(Pos2::new(w[1].x, base), c_bot);
                        mesh.colored_vertex(Pos2::new(w[0].x, base), c_bot);
                        mesh.add_triangle(i, i + 1, i + 2);
                        mesh.add_triangle(i, i + 2, i + 3);
                    }
                    painter.add(Shape::mesh(mesh));
                    painter.add(Shape::line(p.to_vec(), Stroke::new(1.4_f32, AppTheme::alpha(color, 160))));

                    for pt in p.iter().skip(1) {
                        painter.circle_filled(*pt, 2.5, AppTheme::alpha(color, 180));
                    }
                }
            };

            // Draw inactive first, then active on top
            if active_is_vol {
                draw_curve(&painter, &p_mod, theme.gold, false);
                draw_curve(&painter, &p_vol, theme.teal, true);
            } else {
                draw_curve(&painter, &p_vol, theme.teal, false);
                draw_curve(&painter, &p_mod, theme.gold, true);
            }

            // Labels for active curve
            let active_p = if active_is_vol { &p_vol } else { &p_mod };
            for (i, (a, b)) in [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (5, 6)].into_iter().enumerate() {
                if active_p[b].x - active_p[a].x < 9.0 {
                    continue; // stage too narrow for a label
                }
                painter.text(
                    Pos2::new((active_p[a].x + active_p[b].x) * 0.5, base + 11.0),
                    Align2::CENTER_CENTER,
                    ["DEL", "A", "H", "D", "S", "R"][i],
                    FontId::monospace(9.5),
                    if active_is_vol { theme.teal_text } else { theme.gold_text },
                );
            }
        }

        ui.add_space(8.0);
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing = Vec2::new(6.0, 6.0);
            if active_is_vol {
                changed |= widgets::value_chip(ui, theme, "VOL DELAY", vol_delay_ms, 0.0..=200.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "VOL ATTACK", vol_attack_ms, 0.0..=500.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "VOL HOLD", vol_hold_ms, 0.0..=200.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "VOL DECAY", vol_decay_ms, 10.0..=1000.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "VOL SUSTAIN", vol_sustain_pct, 0.0..=100.0, " %", 0);
                changed |= widgets::value_chip(ui, theme, "VOL RELEASE", vol_release_ms, 5.0..=1000.0, " ms", 0);
            } else {
                changed |= widgets::value_chip(ui, theme, "MOD DELAY", mod_delay_ms, 0.0..=200.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "MOD ATTACK", mod_attack_ms, 0.0..=500.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "MOD HOLD", mod_hold_ms, 0.0..=200.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "MOD DECAY", mod_decay_ms, 10.0..=1000.0, " ms", 0);
                changed |= widgets::value_chip(ui, theme, "MOD SUSTAIN", mod_sustain_pct, 0.0..=100.0, " %", 0);
                changed |= widgets::value_chip(ui, theme, "MOD RELEASE", mod_release_ms, 5.0..=1000.0, " ms", 0);
            }
        });
        changed
    }

    // ───────────────────────────── filter response ─────────────────────────────
    pub fn draw_filter_response(ui: &mut Ui, theme: &AppTheme, cutoff_hz: &mut f32, resonance_db: &mut f32, sample_rate: f32) -> bool {
        let mut changed = false;
        widgets::card_header(ui, theme, theme.coral, "BIQUAD LOW-PASS · FREQUENCY RESPONSE", |ui| widgets::live_tag(ui, theme));
        ui.add_space(6.0);

        let (rect, resp_bg) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 112.0), Sense::click_and_drag());

        let pad = 10.0;
        let usable = (rect.width() - 2.0 * pad).max(10.0);
        let x0 = rect.min.x + pad;
        let top = rect.min.y + 10.0;
        let base = rect.max.y - 20.0;
        let hr = (base - top).max(10.0);
        const DB_TOP: f32 = 12.0;
        const DB_SPAN: f32 = 48.0; // +12 dB .. -36 dB
        let fx = |f: f32| x0 + (f / 20.0).ln() / 1000.0_f32.ln() * usable;
        let dy = |db: f32| top + (DB_TOP - db) / DB_SPAN * hr;

        if ui.is_rect_visible(rect) {
            // Drag anywhere on the frequency graph
            if resp_bg.dragged() || resp_bg.clicked() {
                if let Some(pos) = resp_bg.interact_pointer_pos() {
                    let frac = ((pos.x - x0) / usable).clamp(0.0, 1.0);
                    *cutoff_hz = (20.0 * 1000.0_f32.powf(frac)).clamp(100.0, 12000.0);
                    let db_frac = ((pos.y - top) / hr).clamp(0.0, 1.0);
                    *resonance_db = (DB_TOP - db_frac * DB_SPAN).clamp(0.0, 18.0);
                    changed = true;
                }
            }

            // Or drag the specific handle
            let handle = Pos2::new(fx(*cutoff_hz), dy(*resonance_db));
            let resp = ui
                .interact(Rect::from_center_size(handle, Vec2::splat(26.0)), ui.id().with("filter_handle"), Sense::drag())
                .on_hover_cursor(CursorIcon::Grab);
            if resp.dragged() {
                ui.ctx().set_cursor_icon(CursorIcon::Grabbing);
                let d = resp.drag_delta();
                *cutoff_hz = (*cutoff_hz * 1000.0_f32.powf(d.x / usable)).clamp(100.0, 12000.0);
                *resonance_db = (*resonance_db - d.y / hr * DB_SPAN).clamp(0.0, 18.0);
                changed = true;
            }
        }

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect);
            painter.rect_filled(rect, Rounding::same(6.0), theme.deep_well);
            painter.rect_stroke(rect, Rounding::same(6.0), Stroke::new(1.0_f32, theme.hairline));

            let grid = Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 170));
            for (db, label) in [(0.0, "0 dB"), (-12.0, "-12"), (-24.0, "-24")] {
                painter.line_segment([Pos2::new(rect.min.x + 6.0, dy(db)), Pos2::new(rect.max.x - 6.0, dy(db))], grid);
                painter.text(Pos2::new(rect.min.x + 8.0, dy(db) - 2.0), Align2::LEFT_BOTTOM, label, FontId::monospace(9.5), theme.text_dim);
            }
            for (f, label) in [(100.0, "100"), (1000.0, "1k"), (10000.0, "10k")] {
                painter.line_segment([Pos2::new(fx(f), top), Pos2::new(fx(f), base)], grid);
                painter.text(Pos2::new(fx(f), rect.max.y - 8.0), Align2::CENTER_CENTER, label, FontId::monospace(9.5), theme.text_dim);
            }
            painter.text(Pos2::new(x0, rect.max.y - 8.0), Align2::LEFT_CENTER, "20 Hz", FontId::monospace(9.5), theme.text_dim);
            painter.text(Pos2::new(x0 + usable, rect.max.y - 8.0), Align2::RIGHT_CENTER, "20k", FontId::monospace(9.5), theme.text_dim);

            // Exact magnitude response of the biquad the audio engine runs (same coefficients, same sample rate)
            let steps = 160;
            let pts: Vec<Pos2> = (0..=steps)
                .map(|i| {
                    let f = 20.0 * 1000.0_f32.powf(i as f32 / steps as f32);
                    let db = crate::audio_engine::lowpass_mag_db(f, *cutoff_hz, *resonance_db, sample_rate).clamp(-36.0, DB_TOP);
                    Pos2::new(x0 + i as f32 / steps as f32 * usable, dy(db))
                })
                .collect();

            let mut mesh = Mesh::default();
            let c_top = AppTheme::alpha(theme.coral, 110);
            let c_bot = AppTheme::alpha(theme.coral, 8);
            for (i, w) in pts.windows(2).enumerate() {
                let v = mesh.vertices.len() as u32;
                mesh.colored_vertex(w[0], c_top);
                mesh.colored_vertex(w[1], c_top);
                mesh.colored_vertex(Pos2::new(w[1].x, base), c_bot);
                mesh.colored_vertex(Pos2::new(w[0].x, base), c_bot);
                mesh.add_triangle(v, v + 1, v + 2);
                mesh.add_triangle(v, v + 2, v + 3);
                let _ = i;
            }
            painter.add(Shape::mesh(mesh));
            painter.add(Shape::line(pts, Stroke::new(2.0_f32, theme.coral)));

            // Cutoff marker
            let hx = fx(*cutoff_hz);
            let hy = dy(*resonance_db);
            painter.extend(Shape::dashed_line(&[Pos2::new(hx, top), Pos2::new(hx, base)], Stroke::new(1.5_f32, theme.gold), 4.0, 3.0));
            painter.circle_filled(Pos2::new(hx, hy), 4.5, theme.panel);
            painter.circle_stroke(Pos2::new(hx, hy), 4.5, Stroke::new(2.0_f32, theme.gold));

            let readout = format!("{:.0} Hz · +{:.1} dB", cutoff_hz, resonance_db);
            let (anchor, px) = if hx - rect.min.x > 130.0 { (Align2::RIGHT_CENTER, hx - 9.0) } else { (Align2::LEFT_CENTER, hx + 9.0) };
            painter.text(Pos2::new(px, (hy - 10.0).max(rect.min.y + 8.0)), anchor, readout, FontId::monospace(10.5), theme.gold_text);
        }

        ui.add_space(8.0);
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing = Vec2::new(6.0, 6.0);
            changed |= widgets::value_chip(ui, theme, "CUTOFF", cutoff_hz, 100.0..=12000.0, " Hz", 0);
            changed |= widgets::value_chip(ui, theme, "RESONANCE", resonance_db, 0.0..=18.0, " dB", 1);
            widgets::badge_chip(ui, theme, "SLOPE", "12 dB/oct");
            widgets::badge_chip(ui, theme, "TOPOLOGY", "RBJ LP");
        });
        changed
    }

    // ───────────────────────────── region lanes ─────────────────────────────
    /// One bar per region, positioned over the keyboard geometry. Regions that overlap are packed
    /// into separate lanes, so "C2-C5" and "C2-C4" are both visible. With `editable`, the bar edges can be
    /// dragged to change the key range; clicking a bar selects the region.
    pub fn draw_region_lanes(ui: &mut Ui, theme: &AppTheme, bars: &[RegionBar], lane_h: f32, max_lanes: usize, editable: bool) -> LaneEvent {
        let (lane_of, n) = pack_lanes(bars);
        let shown = n.min(max_lanes).max(1);
        let gap = 2.0;
        let ruler = if editable { 14.0 } else { 0.0 };
        let h = ruler + shown as f32 * (lane_h + gap) + 2.0;
        let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), h), Sense::hover());
        let mut ev = LaneEvent::default();
        if !ui.is_rect_visible(rect) {
            return ev;
        }
        let geom = KeyGeom::new(rect);
        let painter = ui.painter_at(rect.expand(4.0));
        painter.rect_filled(rect, Rounding::same(4.0), AppTheme::alpha(theme.text, 8));
        if editable {
            for (k, name) in [(0usize, "C2"), (12, "C3"), (24, "C4"), (36, "C5"), (48, "C6")] {
                let x = geom.boundary(k);
                painter.line_segment([Pos2::new(x, rect.min.y + ruler - 3.0), Pos2::new(x, rect.max.y)], Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 160)));
                painter.text(Pos2::new(x + 3.0, rect.min.y + 1.0), Align2::LEFT_TOP, name, FontId::monospace(9.5), theme.text_dim);
            }
        }
        let mut dragged_edge: Option<(usize, u8, u8)> = None;
        for (i, b) in bars.iter().enumerate() {
            let lane = lane_of[i];
            if lane >= shown {
                continue;
            }
            let top = rect.min.y + ruler + lane as f32 * (lane_h + gap) + 1.0;
            let x0 = geom.boundary(b.lo as usize) + 0.5;
            let x1 = (geom.boundary(b.hi as usize + 1) - 0.5).max(x0 + 3.0);
            let r = Rect::from_min_max(Pos2::new(x0, top), Pos2::new(x1, top + lane_h));
            let body = ui.interact(r, ui.id().with(("rlane", i)), Sense::click());
            let fill = AppTheme::alpha(b.color, if b.active { 130 } else if body.hovered() { 85 } else { 55 });
            painter.rect(r, Rounding::same(4.0), fill, Stroke::new(if b.active { 1.6_f32 } else { 1.0_f32 }, if b.active { b.color } else { AppTheme::alpha(b.color, 160) }));
            ui.painter_at(r.shrink2(Vec2::new(3.0, 0.0))).text(Pos2::new(r.min.x + 6.0, r.center().y), Align2::LEFT_CENTER, &b.label, FontId::monospace((lane_h * 0.62).clamp(9.0, 11.5)), if b.active { theme.text } else { b.text_color });
            if body.clicked() {
                ev.select = Some(i);
            }
            if editable {
                for (side, edge_x) in [(0u8, r.min.x), (1u8, r.max.x)] {
                    let hr = Rect::from_center_size(Pos2::new(edge_x, r.center().y), Vec2::new(9.0, lane_h));
                    let resp = ui.interact(hr, ui.id().with(("redge", i, side)), Sense::drag());
                    if resp.hovered() || resp.dragged() {
                        ui.ctx().set_cursor_icon(CursorIcon::ResizeHorizontal);
                        painter.rect_filled(Rect::from_center_size(Pos2::new(edge_x, r.center().y), Vec2::new(3.0, lane_h - 4.0)), Rounding::same(1.5), theme.gold);
                    }
                    if resp.dragged() {
                        if let Some(p) = resp.interact_pointer_pos() {
                            let k = geom.key_at(p.x) as u8;
                            let (lo, hi) = if side == 0 { (k.min(b.hi), b.hi) } else { (b.lo, k.max(b.lo)) };
                            if (lo, hi) != (b.lo, b.hi) {
                                dragged_edge = Some((i, lo, hi));
                            }
                        }
                        ev.select = ev.select.or(Some(i));
                    }
                }
            }
        }
        ev.range = dragged_edge;
        ev
    }

    // ───────────────────────────── waterfall ─────────────────────────────
    /// Live note trail: notes grow upward from the hit line while a key is held,
    /// then drift away and fade over `WATERFALL_TRAIL_SECS`.
    pub fn draw_note_waterfall(ui: &mut Ui, theme: &AppTheme, notes: &[WaterNote], now: f64, zones: &KeyZones, height: f32) {
        ui.horizontal(|ui| {
            ui.spacing_mut().item_spacing.x = 12.0;
            ui.label(caps(theme, "NOTE WATERFALL"));
            for (c, t) in [(theme.gold, "sounding"), (theme.text_dim, "no region")] {
                ui.horizontal(|ui| {
                    ui.spacing_mut().item_spacing.x = 5.0;
                    let (r, _) = ui.allocate_exact_size(Vec2::splat(8.0), Sense::hover());
                    ui.painter().rect_filled(r, Rounding::same(2.0), c);
                    ui.label(RichText::new(t).size(11.5).color(theme.text_dim));
                });
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(mono(format!("Live trail {:.1} s", WATERFALL_TRAIL_SECS), 11.0, theme.text_dim));
            });
        });
        ui.add_space(4.0);

        let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), height), Sense::hover());
        if !ui.is_rect_visible(rect) {
            return;
        }
        let geom = KeyGeom::new(rect);
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, Rounding { nw: 6.0, ne: 6.0, sw: 0.0, se: 0.0 }, theme.deep_well);

        // Lane lines: stronger at every C
        for idx in 0..KEY_COUNT {
            if is_black(idx) {
                continue;
            }
            let (x, _) = geom.span(idx);
            let strong = idx % 12 == 0;
            painter.line_segment(
                [Pos2::new(x, rect.min.y), Pos2::new(x, rect.max.y)],
                Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, if strong { 230 } else { 90 })),
            );
        }
        // Time lines every 0.5 s
        let pps = (rect.height() - 4.0) / WATERFALL_TRAIL_SECS;
        for k in 1..4 {
            let y = rect.max.y - 2.0 - k as f32 * 0.5 * pps;
            painter.line_segment([Pos2::new(rect.min.x, y), Pos2::new(rect.max.x, y)], Stroke::new(1.0_f32, AppTheme::alpha(theme.hairline, 70)));
        }

        for n in notes {
            let top_off = (now - n.t_on) as f32 * pps;
            let bot_off = n.t_off.map(|t| (now - t) as f32 * pps).unwrap_or(0.0);
            if bot_off >= rect.height() || n.key as usize >= KEY_COUNT {
                continue;
            }
            let y_bot = rect.max.y - 2.0 - bot_off;
            let y_top = (rect.max.y - 2.0 - top_off).max(rect.min.y);
            let (x, w) = geom.span(n.key as usize);
            let inset = if is_black(n.key as usize) { 1.0 } else { 3.0 };
            let r = Rect::from_min_max(Pos2::new(x + inset, y_top), Pos2::new(x + w - inset, y_bot.max(y_top + 3.0)));

            let (base, fade) = match n.t_off {
                None => (theme.gold, 1.0),
                Some(t) => (zones[n.key as usize].unwrap_or(theme.text_dim), (1.0 - (now - t) as f32 / WATERFALL_TRAIL_SECS).clamp(0.0, 1.0)),
            };
            let col = AppTheme::alpha(base, (255.0 * fade) as u8);
            painter.rect_filled(r.expand(2.0), Rounding::same(5.0), AppTheme::alpha(base, (45.0 * fade) as u8));
            painter.rect_filled(r, Rounding::same(3.0), col);
            painter.line_segment([Pos2::new(r.min.x + 2.0, r.min.y + 0.5), Pos2::new(r.max.x - 2.0, r.min.y + 0.5)], Stroke::new(1.0_f32, AppTheme::alpha(Color32::WHITE, (90.0 * fade) as u8)));
        }

        // Hit line
        let y = rect.max.y - 1.0;
        painter.line_segment([Pos2::new(rect.min.x, y), Pos2::new(rect.max.x, y)], Stroke::new(5.0_f32, AppTheme::alpha(theme.gold, 40)));
        painter.line_segment([Pos2::new(rect.min.x, y), Pos2::new(rect.max.x, y)], Stroke::new(2.0_f32, theme.gold));
    }

    // ───────────────────────────── keyboard ─────────────────────────────
    /// Draws the 61-key piano and returns the key currently held down by the mouse.
    pub fn draw_keyboard(ui: &mut Ui, theme: &AppTheme, height: f32, zones: &KeyZones, sounding: &[bool; KEY_COUNT]) -> Option<u8> {
        let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), height), Sense::click_and_drag());
        let geom = KeyGeom::new(rect);
        let black_h = height * 0.62;

        let key_rect = |idx: usize| -> Rect {
            let (x, w) = geom.span(idx);
            let h = if is_black(idx) { black_h } else { height - 1.0 };
            Rect::from_min_size(Pos2::new(x, rect.min.y), Vec2::new(w, h))
        };

        // Mouse hit test: black keys sit on top of white ones
        let pressed = if resp.is_pointer_button_down_on() {
            resp.interact_pointer_pos().and_then(|p| {
                let hit = |idx: usize| key_rect(idx).contains(p);
                (0..KEY_COUNT).filter(|&i| is_black(i)).find(|&i| hit(i)).or_else(|| (0..KEY_COUNT).filter(|&i| !is_black(i)).find(|&i| hit(i)))
            })
        } else {
            None
        };

        if ui.is_rect_visible(rect) {
            let painter = ui.painter_at(rect.expand(8.0));
            painter.rect_filled(rect, Rounding { nw: 0.0, ne: 0.0, sw: 6.0, se: 6.0 }, theme.key_edge);

            let white_w = geom.white_w;
            let cap = (white_w * 0.62).clamp(12.0, 20.0);

            for idx in (0..KEY_COUNT).filter(|&i| !is_black(i)) {
                let r = key_rect(idx).shrink2(Vec2::new(0.75, 0.0));
                let on = sounding[idx];
                let fill = if on { theme.gold } else { match zones[idx] { Some(c) => mix(theme.key_white_left, c, 0.28), None => mix(theme.key_white_left, theme.text_dim, 0.55) } };
                let round = Rounding { nw: 0.0, ne: 0.0, sw: 4.0, se: 4.0 };
                if on {
                    painter.rect_filled(r.expand(3.0), Rounding::same(6.0), AppTheme::alpha(theme.gold, 60));
                }
                painter.rect_filled(r, round, fill);
                // bevel: darker lip at the bottom
                painter.rect_filled(Rect::from_min_max(Pos2::new(r.min.x, r.max.y - 5.0), r.max), round, AppTheme::alpha(Color32::BLACK, 28));

                if idx % 12 == 0 {
                    painter.text(Pos2::new(r.center().x, r.max.y - cap - 14.0), Align2::CENTER_CENTER, format!("C{}", idx / 12 + 2), FontId::monospace(9.5), AppTheme::alpha(theme.keycap_on_white, 150));
                }
                Self::keycap(&painter, theme, r.center().x, r.max.y - cap * 0.5 - 8.0, cap, keycap_label(idx), false);
            }

            let bcap = (geom.span(1).1 * 0.8).clamp(11.0, 16.0);
            for idx in (0..KEY_COUNT).filter(|&i| is_black(i)) {
                let r = key_rect(idx);
                let on = sounding[idx];
                let fill = if on { theme.gold } else { match zones[idx] { Some(c) => mix(theme.key_black_left, c, 0.35), None => mix(theme.key_black_left, theme.text_dim, 0.30) } };
                let round = Rounding { nw: 0.0, ne: 0.0, sw: 3.0, se: 3.0 };
                painter.rect_filled(r.translate(Vec2::new(0.0, 2.0)), round, AppTheme::alpha(Color32::BLACK, 90)); // drop shadow
                if on {
                    painter.rect_filled(r.expand(3.0), Rounding::same(5.0), AppTheme::alpha(theme.gold, 60));
                }
                painter.rect_filled(r, round, fill);
                painter.rect_filled(Rect::from_min_max(Pos2::new(r.min.x + 1.5, r.max.y - 6.0), Pos2::new(r.max.x - 1.5, r.max.y - 2.0)), Rounding::same(2.0), AppTheme::alpha(Color32::WHITE, 22));
                Self::keycap(&painter, theme, r.center().x, r.max.y - bcap * 0.5 - 9.0, bcap, keycap_label(idx), true);
            }
        }

        pressed.map(|k| k as u8)
    }

    fn keycap(painter: &egui::Painter, theme: &AppTheme, cx: f32, cy: f32, size: f32, label: &str, on_black: bool) {
        if label.is_empty() {
            return;
        }
        let r = Rect::from_center_size(Pos2::new(cx, cy), Vec2::splat(size));
        let (fill, stroke, fg) = if on_black {
            (AppTheme::alpha(Color32::WHITE, 20), AppTheme::alpha(Color32::WHITE, 46), theme.keycap_on_black)
        } else {
            (AppTheme::alpha(Color32::BLACK, 14), AppTheme::alpha(Color32::BLACK, 56), theme.keycap_on_white)
        };
        painter.rect(r, Rounding::same(3.0), fill, Stroke::new(1.0_f32, stroke));
        painter.text(r.center(), Align2::CENTER_CENTER, label, FontId::monospace(size * 0.56), fg);
    }
}

/// Laptop key that plays each piano key (unshifted layout).
pub fn keycap_label(key_idx: usize) -> &'static str {
    match key_idx {
        0 => "Z", 1 => "S", 2 => "X", 3 => "D", 4 => "C", 5 => "V", 6 => "G", 7 => "B", 8 => "H", 9 => "N", 10 => "J", 11 => "M",
        12 => "Q", 13 => "2", 14 => "W", 15 => "3", 16 => "E", 17 => "R", 18 => "5", 19 => "T", 20 => "6", 21 => "Y", 22 => "7", 23 => "U",
        24 => "I",
        _ => "",
    }
}

// ───────────────────────────── helpers ─────────────────────────────

/// 1-2-5 step giving about five ticks over `dur` seconds.
fn nice_step(dur: f32) -> f32 {
    let raw = (dur / 5.0).max(1e-6);
    let mag = 10.0_f32.powf(raw.log10().floor());
    let f = raw / mag;
    let m = if f <= 1.0 { 1.0 } else if f <= 2.0 { 2.0 } else if f <= 5.0 { 5.0 } else { 10.0 };
    m * mag
}

fn fmt_time(t: f32) -> String {
    if t >= 1.0 {
        format!("{:.2} s", t)
    } else if t >= 0.01 {
        format!("{:.0} ms", t * 1000.0)
    } else if t == 0.0 {
        "0".to_string()
    } else {
        format!("{:.1} ms", t * 1000.0)
    }
}