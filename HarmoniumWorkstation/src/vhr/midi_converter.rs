//! Bidirectional Standard MIDI File (.mid) <-> VHR Converter
//!
//! Converts between Standard MIDI Files (Type 0 / 1) and VHR v2.0 performance recordings.
//! Includes zero-dependency MIDI SMF parser and serializer.

use std::io::Cursor;
use byteorder::{BigEndian, ReadBytesExt, WriteBytesExt};
use crate::vhr::types::*;

pub struct MidiConverter;

#[derive(Debug, Clone)]
struct RawMidiEvent {
    abs_ticks: u64,
    status: u8,
    data1: u8,
    data2: u8,
    is_meta: bool,
    meta_type: u8,
    meta_data: Vec<u8>,
}

impl MidiConverter {
    /// Convert Standard MIDI File bytes into a high-level `VhrRecording`
    pub fn midi_to_vhr(midi_bytes: &[u8], title_hint: &str) -> Result<VhrRecording, String> {
        if midi_bytes.len() < 14 {
            return Err("MIDI file too short".to_string());
        }

        let mut cur = Cursor::new(midi_bytes);

        // 1. Read MThd Chunk
        let mut mthd = [0u8; 4];
        std::io::Read::read_exact(&mut cur, &mut mthd).map_err(|e| e.to_string())?;
        if &mthd != b"MThd" {
            return Err("Invalid MIDI header: 'MThd' magic missing".to_string());
        }

        let header_len = cur.read_u32::<BigEndian>().map_err(|e| e.to_string())?;
        if header_len < 6 {
            return Err("Invalid MThd header length".to_string());
        }

        let _format = cur.read_u16::<BigEndian>().map_err(|e| e.to_string())?;
        let num_tracks = cur.read_u16::<BigEndian>().map_err(|e| e.to_string())? as usize;
        let division = cur.read_u16::<BigEndian>().map_err(|e| e.to_string())?;

        // Skip any extra header bytes if header_len > 6
        if header_len > 6 {
            let extra = header_len - 6;
            let mut skip_buf = vec![0u8; extra as usize];
            std::io::Read::read_exact(&mut cur, &mut skip_buf).map_err(|e| e.to_string())?;
        }

        // PPQ (ticks per quarter note)
        let ppq = if (division & 0x8000) == 0 {
            division
        } else {
            480 // fallback if SMPTE
        };

        // 2. Parse all tracks into combined chronological event list
        let mut all_events: Vec<RawMidiEvent> = Vec::new();

        for _ in 0..num_tracks {
            if cur.position() >= midi_bytes.len() as u64 {
                break;
            }

            let mut mtrk = [0u8; 4];
            if std::io::Read::read_exact(&mut cur, &mut mtrk).is_err() {
                break;
            }
            if &mtrk != b"MTrk" {
                // Skip unknown chunk
                let chunk_len = cur.read_u32::<BigEndian>().map_err(|e| e.to_string())? as usize;
                let new_pos = cur.position() + chunk_len as u64;
                cur.set_position(new_pos);
                continue;
            }

            let track_len = cur.read_u32::<BigEndian>().map_err(|e| e.to_string())? as usize;
            let track_end = cur.position() + track_len as u64;

            let mut track_abs_ticks: u64 = 0;
            let mut running_status: u8 = 0;

            while cur.position() < track_end {
                let delta_ticks = read_var_len(&mut cur)?;
                track_abs_ticks += delta_ticks as u64;

                let mut b = cur.read_u8().map_err(|e| e.to_string())?;
                if b & 0x80 == 0 {
                    // Running status
                    if running_status == 0 {
                        return Err("Encountered running status without preceding status".to_string());
                    }
                    cur.set_position(cur.position() - 1);
                    b = running_status;
                } else if b < 0xF0 {
                    running_status = b;
                }

                if b == 0xFF {
                    // Meta event
                    let meta_type = cur.read_u8().map_err(|e| e.to_string())?;
                    let meta_len = read_var_len(&mut cur)? as usize;
                    let mut meta_data = vec![0u8; meta_len];
                    std::io::Read::read_exact(&mut cur, &mut meta_data).map_err(|e| e.to_string())?;

                    all_events.push(RawMidiEvent {
                        abs_ticks: track_abs_ticks,
                        status: 0xFF,
                        data1: 0,
                        data2: 0,
                        is_meta: true,
                        meta_type,
                        meta_data,
                    });
                } else if b == 0xF0 || b == 0xF7 {
                    // Sysex event - skip
                    let sysex_len = read_var_len(&mut cur)? as usize;
                    let mut sysex_buf = vec![0u8; sysex_len];
                    std::io::Read::read_exact(&mut cur, &mut sysex_buf).map_err(|e| e.to_string())?;
                } else {
                    let cmd = b & 0xF0;
                    let d1 = cur.read_u8().map_err(|e| e.to_string())?;
                    let d2 = if cmd == 0xC0 || cmd == 0xD0 {
                        0
                    } else {
                        cur.read_u8().map_err(|e| e.to_string())?
                    };

                    all_events.push(RawMidiEvent {
                        abs_ticks: track_abs_ticks,
                        status: b,
                        data1: d1,
                        data2: d2,
                        is_meta: false,
                        meta_type: 0,
                        meta_data: Vec::new(),
                    });
                }
            }
        }

        // Sort events chronologically (stable sort)
        all_events.sort_by_key(|e| e.abs_ticks);

        // 3. Convert MIDI ticks into Microseconds with tempo tracking
        let mut current_tempo_us_per_qn: u32 = 500_000; // Default 120 BPM
        let mut current_bpm: f32 = 120.0;
        let mut last_tick: u64 = 0;
        let mut last_event_abs_us: u64 = 0;

        let mut vhr_events = Vec::new();

        // Optional title extracted from track name (meta 0x03)
        let mut extracted_title = title_hint.to_string();

        for ev in all_events {
            let tick_delta = ev.abs_ticks.saturating_sub(last_tick);
            let time_delta_us = ((tick_delta as u64) * (current_tempo_us_per_qn as u64)) / (ppq as u64);
            let current_abs_us = last_event_abs_us + time_delta_us;
            let delta_from_prev_event = (current_abs_us - last_event_abs_us) as u32;

            if ev.is_meta {
                if ev.meta_type == 0x51 && ev.meta_data.len() == 3 {
                    // Set Tempo (microseconds per quarter note)
                    let tempo_val = ((ev.meta_data[0] as u32) << 16)
                        | ((ev.meta_data[1] as u32) << 8)
                        | (ev.meta_data[2] as u32);
                    if tempo_val > 0 {
                        current_tempo_us_per_qn = tempo_val;
                        current_bpm = 60_000_000.0 / (tempo_val as f32);

                        let bpm_x10 = (current_bpm * 10.0) as u16;
                        vhr_events.push(VhrEvent {
                            delta_us: delta_from_prev_event,
                            event_type: VHR_EVENT_TEMPO_CHANGE,
                            data1: (bpm_x10 >> 8) as u8,
                            data2: (bpm_x10 & 0xFF) as u8,
                            channel: 0,
                        });
                        last_tick = ev.abs_ticks;
                        last_event_abs_us = current_abs_us;
                    }
                } else if ev.meta_type == 0x03 && extracted_title.is_empty() {
                    // Track name
                    extracted_title = String::from_utf8_lossy(&ev.meta_data).trim().to_string();
                }
                continue;
            }

            let cmd = ev.status & 0xF0;
            let channel = ev.status & 0x0F;

            match cmd {
                0x90 => {
                    // Note On (velocity > 0 is Note On, velocity == 0 is Note Off)
                    let event_type = if ev.data2 > 0 { VHR_EVENT_NOTE_ON } else { VHR_EVENT_NOTE_OFF };
                    vhr_events.push(VhrEvent {
                        delta_us: delta_from_prev_event,
                        event_type,
                        data1: ev.data1,
                        data2: ev.data2,
                        channel,
                    });
                    last_tick = ev.abs_ticks;
                    last_event_abs_us = current_abs_us;
                }
                0x80 => {
                    // Note Off
                    vhr_events.push(VhrEvent {
                        delta_us: delta_from_prev_event,
                        event_type: VHR_EVENT_NOTE_OFF,
                        data1: ev.data1,
                        data2: ev.data2,
                        channel,
                    });
                    last_tick = ev.abs_ticks;
                    last_event_abs_us = current_abs_us;
                }
                0xB0 => {
                    // Control Change
                    vhr_events.push(VhrEvent {
                        delta_us: delta_from_prev_event,
                        event_type: VHR_EVENT_CONTROL_CHANGE,
                        data1: ev.data1,
                        data2: ev.data2,
                        channel,
                    });
                    last_tick = ev.abs_ticks;
                    last_event_abs_us = current_abs_us;
                }
                0xC0 => {
                    // Program Change
                    vhr_events.push(VhrEvent {
                        delta_us: delta_from_prev_event,
                        event_type: VHR_EVENT_PROGRAM_CHANGE,
                        data1: ev.data1,
                        data2: 0,
                        channel,
                    });
                    last_tick = ev.abs_ticks;
                    last_event_abs_us = current_abs_us;
                }
                _ => {}
            }
        }

        if extracted_title.is_empty() {
            extracted_title = "Imported MIDI".to_string();
        }

        Ok(VhrRecording {
            title: extracted_title,
            native_instrument: "harmonium".to_string(),
            native_tempo_bpm: current_bpm,
            native_transpose: 0,
            native_scale: 0,
            native_octave: 0,
            native_volume: 100,
            timebase_us: 1000,
            ticks_per_quarter: ppq,
            events: vhr_events,
        })
    }

    /// Convert a `VhrRecording` into a Standard MIDI File (Type 0) byte vector
    pub fn vhr_to_midi(vhr: &VhrRecording) -> Result<Vec<u8>, String> {
        let ppq = if vhr.ticks_per_quarter > 0 { vhr.ticks_per_quarter } else { 480 };
        let mut track_data = Vec::new();

        // 1. Write Track Name Meta Event (0xFF 0x03)
        let title_bytes = vhr.title.as_bytes();
        write_var_len(&mut track_data, 0)?; // delta 0
        track_data.write_u8(0xFF).unwrap();
        track_data.write_u8(0x03).unwrap();
        write_var_len(&mut track_data, title_bytes.len() as u32)?;
        track_data.extend_from_slice(title_bytes);

        // 2. Write Initial Tempo Meta Event (0xFF 0x51 0x03)
        let bpm = vhr.native_tempo_bpm.max(10.0);
        let mut current_tempo_us_per_qn = (60_000_000.0 / bpm) as u32;

        write_var_len(&mut track_data, 0)?;
        track_data.write_u8(0xFF).unwrap();
        track_data.write_u8(0x51).unwrap();
        track_data.write_u8(0x03).unwrap();
        track_data.write_u8((current_tempo_us_per_qn >> 16) as u8).unwrap();
        track_data.write_u8((current_tempo_us_per_qn >> 8) as u8).unwrap();
        track_data.write_u8((current_tempo_us_per_qn & 0xFF) as u8).unwrap();

        // 3. Write Events
        // `pending_us` carries the time of events that are not written to the MIDI stream (start/stop markers,
        // instrument changes, ...) into the next written event, and `carry` keeps the sub-tick remainder, so the
        // exported timing neither compresses nor drifts on long performances.
        let mut pending_us: u64 = 0;
        let mut carry: u64 = 0;
        let delta_for = |pending_us: &mut u64, carry: &mut u64, ev_delta: u32, tempo: u32| -> u32 {
            let total = *pending_us + ev_delta as u64;
            *pending_us = 0;
            let num = total * ppq as u64 + *carry;
            let ticks = num / tempo.max(1) as u64;
            *carry = num % tempo.max(1) as u64;
            ticks as u32
        };
        for ev in &vhr.events {
            let ch = ev.channel.min(15);

            match ev.event_type {
                VHR_EVENT_NOTE_ON => {
                    let delta_ticks = delta_for(&mut pending_us, &mut carry, ev.delta_us, current_tempo_us_per_qn);
                    write_var_len(&mut track_data, delta_ticks)?;
                    track_data.write_u8(0x90 | ch).unwrap();
                    track_data.write_u8(ev.data1.min(127)).unwrap();
                    track_data.write_u8(ev.data2.clamp(1, 127)).unwrap();
                }
                VHR_EVENT_NOTE_OFF => {
                    let delta_ticks = delta_for(&mut pending_us, &mut carry, ev.delta_us, current_tempo_us_per_qn);
                    write_var_len(&mut track_data, delta_ticks)?;
                    track_data.write_u8(0x80 | ch).unwrap();
                    track_data.write_u8(ev.data1.min(127)).unwrap();
                    track_data.write_u8(ev.data2.min(127)).unwrap();
                }
                VHR_EVENT_CONTROL_CHANGE => {
                    let delta_ticks = delta_for(&mut pending_us, &mut carry, ev.delta_us, current_tempo_us_per_qn);
                    write_var_len(&mut track_data, delta_ticks)?;
                    track_data.write_u8(0xB0 | ch).unwrap();
                    track_data.write_u8(ev.data1.min(127)).unwrap();
                    track_data.write_u8(ev.data2.min(127)).unwrap();
                }
                VHR_EVENT_PROGRAM_CHANGE => {
                    let delta_ticks = delta_for(&mut pending_us, &mut carry, ev.delta_us, current_tempo_us_per_qn);
                    write_var_len(&mut track_data, delta_ticks)?;
                    track_data.write_u8(0xC0 | ch).unwrap();
                    track_data.write_u8(ev.data1.min(127)).unwrap();
                }
                VHR_EVENT_TEMPO_CHANGE => {
                    let bpm_x10 = ((ev.data1 as u16) << 8) | (ev.data2 as u16);
                    let new_bpm = (bpm_x10 as f32) / 10.0;
                    if new_bpm > 5.0 {
                        // the time up to this event still runs at the old tempo
                        let delta_ticks = delta_for(&mut pending_us, &mut carry, ev.delta_us, current_tempo_us_per_qn);
                        current_tempo_us_per_qn = (60_000_000.0 / new_bpm) as u32;
                        write_var_len(&mut track_data, delta_ticks)?;
                        track_data.write_u8(0xFF).unwrap();
                        track_data.write_u8(0x51).unwrap();
                        track_data.write_u8(0x03).unwrap();
                        track_data.write_u8((current_tempo_us_per_qn >> 16) as u8).unwrap();
                        track_data.write_u8((current_tempo_us_per_qn >> 8) as u8).unwrap();
                        track_data.write_u8((current_tempo_us_per_qn & 0xFF) as u8).unwrap();
                    } else {
                        pending_us += ev.delta_us as u64;
                    }
                }
                _ => {
                    // Informational markers are not part of a standard MIDI stream, but their time still counts.
                    pending_us += ev.delta_us as u64;
                }
            }
        }

        // 4. End of Track Meta Event (0xFF 0x2F 0x00)
        write_var_len(&mut track_data, 0)?;
        track_data.write_u8(0xFF).unwrap();
        track_data.write_u8(0x2F).unwrap();
        track_data.write_u8(0x00).unwrap();

        // 5. Build full SMF File (MThd + MTrk)
        let mut smf = Vec::with_capacity(14 + 8 + track_data.len());
        smf.extend_from_slice(b"MThd");
        smf.write_u32::<BigEndian>(6).unwrap();
        smf.write_u16::<BigEndian>(0).unwrap(); // Format 0 (single track)
        smf.write_u16::<BigEndian>(1).unwrap(); // 1 track
        smf.write_u16::<BigEndian>(ppq).unwrap();

        smf.extend_from_slice(b"MTrk");
        smf.write_u32::<BigEndian>(track_data.len() as u32).unwrap();
        smf.extend_from_slice(&track_data);

        Ok(smf)
    }
}

// Variable-Length Quantity (VLQ) helpers
fn read_var_len(cur: &mut Cursor<&[u8]>) -> Result<u32, String> {
    let mut value: u32 = 0;
    for _ in 0..4 {
        let b = cur.read_u8().map_err(|e| e.to_string())?;
        value = (value << 7) | ((b & 0x7F) as u32);
        if (b & 0x80) == 0 {
            return Ok(value);
        }
    }
    Err("Invalid variable-length quantity in MIDI stream".to_string())
}

fn write_var_len(out: &mut Vec<u8>, mut val: u32) -> Result<(), String> {
    let mut buf = [0u8; 4];
    buf[0] = (val & 0x7F) as u8;
    let mut count = 1;
    while val > 0x7F {
        val >>= 7;
        buf[count] = ((val & 0x7F) | 0x80) as u8;
        count += 1;
    }
    for i in (0..count).rev() {
        out.push(buf[i]);
    }
    Ok(())
}