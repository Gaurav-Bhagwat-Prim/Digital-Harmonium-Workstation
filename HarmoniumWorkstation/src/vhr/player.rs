//! Real-Time VHR Performance Player & Live Recorder
//!
//! Replays `.vhr` event sequences through `AudioPlayer` with speed control,
//! pause, resume, and real-time event dispatching.
//! Also provides `VhrRecorder` for capturing live performances into VHR format.

use std::time::Instant;
use crate::audio_engine::AudioPlayer;
use crate::vhr::types::*;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum VhrPlayerState {
    Idle,
    Ready,
    Playing,
    Paused,
    Finished,
}

pub struct VhrPlayer {
    pub recording: Option<VhrRecording>,
    pub state: VhrPlayerState,
    pub speed_multiplier: f32,

    pub cursor_idx: usize,
    pub accumulated_us: u64,
    pub cursor_time_us: u64,

    pub events_fired: usize,
    pub total_duration_ms: u32,
    pub current_position_ms: u32,
}

impl Default for VhrPlayer {
    fn default() -> Self {
        Self {
            recording: None,
            state: VhrPlayerState::Idle,
            speed_multiplier: 1.0,
            cursor_idx: 0,
            accumulated_us: 0,
            cursor_time_us: 0,
            events_fired: 0,
            total_duration_ms: 0,
            current_position_ms: 0,
        }
    }
}

impl VhrPlayer {
    pub fn new() -> Self {
        Self::default()
    }

    /// Load a new recording into the player
    pub fn load(&mut self, rec: VhrRecording) {
        let total_us: u64 = rec.events.iter().map(|e| e.delta_us as u64).sum();
        self.total_duration_ms = (total_us / 1000) as u32;
        self.recording = Some(rec);
        self.state = VhrPlayerState::Ready;
        self.reset();
    }

    pub fn play(&mut self) {
        if self.state == VhrPlayerState::Ready || self.state == VhrPlayerState::Paused || self.state == VhrPlayerState::Finished {
            if self.state == VhrPlayerState::Finished {
                self.reset();
            }
            self.state = VhrPlayerState::Playing;
        }
    }

    pub fn pause(&mut self) {
        if self.state == VhrPlayerState::Playing {
            self.state = VhrPlayerState::Paused;
        }
    }

    pub fn stop(&mut self, audio_player: &AudioPlayer) {
        self.reset();
        audio_player.all_notes_off();
        if self.recording.is_some() {
            self.state = VhrPlayerState::Ready;
        } else {
            self.state = VhrPlayerState::Idle;
        }
    }

    pub fn reset(&mut self) {
        self.cursor_idx = 0;
        self.accumulated_us = 0;
        self.cursor_time_us = 0;
        self.events_fired = 0;
        self.current_position_ms = 0;
    }

    pub fn set_speed(&mut self, speed: f32) {
        self.speed_multiplier = speed.clamp(0.25, 4.0);
    }

    /// Seek playback position to a target time in milliseconds
    pub fn seek_to_ms(&mut self, target_ms: u32, audio_player: &AudioPlayer) {
        let rec = match &self.recording {
            Some(r) => r,
            None => return,
        };

        audio_player.all_notes_off();

        let target_us = (target_ms as u64) * 1000;
        let mut cur_time_us = 0u64;
        let mut new_cursor = 0;

        for (idx, ev) in rec.events.iter().enumerate() {
            let ev_time = cur_time_us + (ev.delta_us as u64);
            if ev_time > target_us {
                break;
            }
            cur_time_us = ev_time;
            new_cursor = idx + 1;
        }

        self.cursor_idx = new_cursor;
        self.cursor_time_us = cur_time_us;
        self.accumulated_us = target_us;
        self.current_position_ms = target_ms;

        if self.cursor_idx >= rec.events.len() {
            self.state = VhrPlayerState::Finished;
        } else if self.state == VhrPlayerState::Finished {
            self.state = VhrPlayerState::Ready;
        }
    }

    /// Advance time and dispatch events whose timestamp has arrived.
    /// Returns any Note-On and Note-Off events fired during this tick.
    pub fn tick(&mut self, elapsed_us: u32, audio_player: &AudioPlayer) -> Vec<VhrTickEvent> {
        let mut fired = Vec::new();
        if self.state != VhrPlayerState::Playing {
            return fired;
        }

        let rec = match &self.recording {
            Some(r) => r,
            None => return fired,
        };

        // If the UI stalled (window dragged, idle without repaints) don't fast-forward through
        // the recording and fire a burst of events: cap a single step at 100 ms.
        let elapsed_us = elapsed_us.min(100_000);
        self.accumulated_us += ((elapsed_us as f64) * (self.speed_multiplier as f64)) as u64;
        self.current_position_ms = (self.accumulated_us / 1000) as u32;

        while self.cursor_idx < rec.events.len() {
            let ev = &rec.events[self.cursor_idx];
            let ev_abs_time = self.cursor_time_us + (ev.delta_us as u64);

            if ev_abs_time > self.accumulated_us {
                break; // Next event is in the future
            }

            // Time has arrived: advance cursor and fire event
            self.cursor_time_us = ev_abs_time;
            self.cursor_idx += 1;
            self.events_fired += 1;

            match ev.event_type {
                VHR_EVENT_NOTE_ON => {
                    let note_midi = ev.data1;
                    let vel = ev.data2;
                    let key_idx = (note_midi as i32 - 36).clamp(0, 60) as u8;
                    audio_player.note_on(key_idx, note_midi, vel);
                    fired.push(VhrTickEvent::NoteOn { key_idx, note_midi, velocity: vel });
                }
                VHR_EVENT_NOTE_OFF => {
                    let note_midi = ev.data1;
                    let key_idx = (note_midi as i32 - 36).clamp(0, 60) as u8;
                    audio_player.note_off(key_idx);
                    fired.push(VhrTickEvent::NoteOff { key_idx, note_midi });
                }
                _ => {}
            }
        }

        if self.cursor_idx >= rec.events.len() {
            self.state = VhrPlayerState::Finished;
            // A recording that ends with a key still held (or was cut off) must not leave notes ringing.
            audio_player.all_notes_off();
        }

        fired
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VhrTickEvent {
    NoteOn { key_idx: u8, note_midi: u8, velocity: u8 },
    NoteOff { key_idx: u8, note_midi: u8 },
}

// ============================================================================
// Live Performance Event Recorder
// ============================================================================

pub struct VhrRecorder {
    pub is_recording: bool,
    pub start_time: Option<Instant>,
    pub last_event_time: Option<Instant>,
    pub recorded_events: Vec<VhrEvent>,
    pub title: String,
    held_notes: Vec<u8>,
    pub native_tempo_bpm: f32,
    pub native_transpose: i8,
    pub native_volume: u8,
}

impl Default for VhrRecorder {
    fn default() -> Self {
        Self {
            is_recording: false,
            start_time: None,
            last_event_time: None,
            recorded_events: Vec::new(),
            title: "Live Harmonium Recording".to_string(),
            held_notes: Vec::new(),
            native_tempo_bpm: 120.0,
            native_transpose: 0,
            native_volume: 100,
        }
    }
}

impl VhrRecorder {
    pub fn new() -> Self {
        Self::default()
    }

    /// Store the session settings (BPM, transpose, volume) that get written into the .vhr header.
    pub fn set_context(&mut self, bpm: f32, transpose: i8, volume: u8) {
        self.native_tempo_bpm = bpm;
        self.native_transpose = transpose;
        self.native_volume = volume;
    }

    pub fn start(&mut self) {
        self.held_notes.clear();
        self.is_recording = true;
        let now = Instant::now();
        self.start_time = Some(now);
        self.last_event_time = Some(now);
        self.recorded_events.clear();

        // Start record stream marker
        self.recorded_events.push(VhrEvent {
            delta_us: 0,
            event_type: VHR_EVENT_START_RECORD,
            data1: 0,
            data2: 0,
            channel: 0,
        });
    }

    pub fn record_note_on(&mut self, note: u8, velocity: u8) {
        if !self.is_recording { return; }
        let now = Instant::now();
        let delta_us = now.duration_since(self.last_event_time.unwrap_or(now)).as_micros() as u32;
        self.last_event_time = Some(now);
        if !self.held_notes.contains(&note) {
            self.held_notes.push(note);
        }

        self.recorded_events.push(VhrEvent {
            delta_us,
            event_type: VHR_EVENT_NOTE_ON,
            data1: note,
            data2: velocity,
            channel: 0,
        });
    }

    pub fn record_note_off(&mut self, note: u8) {
        if !self.is_recording { return; }
        let now = Instant::now();
        let delta_us = now.duration_since(self.last_event_time.unwrap_or(now)).as_micros() as u32;
        self.last_event_time = Some(now);
        self.held_notes.retain(|&n| n != note);

        self.recorded_events.push(VhrEvent {
            delta_us,
            event_type: VHR_EVENT_NOTE_OFF,
            data1: note,
            data2: 0,
            channel: 0,
        });
    }

    pub fn stop(&mut self) -> VhrRecording {
        self.is_recording = false;
        let now = Instant::now();
        let mut delta_us = now.duration_since(self.last_event_time.unwrap_or(now)).as_micros() as u32;

        // Close every note that is still held, otherwise the recording replays with notes that never end.
        for note in std::mem::take(&mut self.held_notes) {
            self.recorded_events.push(VhrEvent {
                delta_us,
                event_type: VHR_EVENT_NOTE_OFF,
                data1: note,
                data2: 0,
                channel: 0,
            });
            delta_us = 0;
        }

        self.recorded_events.push(VhrEvent {
            delta_us,
            event_type: VHR_EVENT_STOP_RECORD,
            data1: 0,
            data2: 0,
            channel: 0,
        });

        VhrRecording {
            title: self.title.clone(),
            native_instrument: "harmonium".to_string(),
            native_tempo_bpm: self.native_tempo_bpm,
            native_transpose: self.native_transpose,
            native_scale: 0,
            native_octave: 0,
            native_volume: self.native_volume,
            timebase_us: 1000,
            ticks_per_quarter: 480,
            events: std::mem::take(&mut self.recorded_events),
        }
    }
}