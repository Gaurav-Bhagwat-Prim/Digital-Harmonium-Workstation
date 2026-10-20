//! VHR (Virtual Harmonium Recording v2.0) Architecture & Toolchain
//!
//! Provides binary types, compilation, decompilation, real-time playback,
//! live recording, and bidirectional MIDI conversion.

pub mod types;
pub mod compiler;
pub mod decompiler;
pub mod midi_converter;
pub mod player;

#[cfg(test)]
mod tests {
    use super::types::*;
    use super::compiler::VhrCompiler;
    use super::decompiler::VhrDecompiler;
    use super::midi_converter::MidiConverter;

    #[test]
    fn test_vhr_roundtrip_compile_decompile() {
        let mut rec = VhrRecording {
            title: "Raga Bhairav Demo".to_string(),
            native_instrument: "harmonium".to_string(),
            native_tempo_bpm: 85.0,
            native_transpose: 2,
            native_scale: 1,
            native_octave: 0,
            native_volume: 110,
            timebase_us: 1000,
            ticks_per_quarter: 480,
            events: Vec::new(),
        };

        // Note C4 on
        rec.events.push(VhrEvent {
            delta_us: 0,
            event_type: VHR_EVENT_NOTE_ON,
            data1: 60,
            data2: 105,
            channel: 0,
        });
        // Note C4 off after 350 ms
        rec.events.push(VhrEvent {
            delta_us: 350_000,
            event_type: VHR_EVENT_NOTE_OFF,
            data1: 60,
            data2: 0,
            channel: 0,
        });

        let binary = VhrCompiler::compile(&rec).expect("Compilation must succeed");
        assert_eq!(binary.len(), VHR_HEADER_SIZE + 2 * VHR_EVENT_SIZE);

        let parsed = VhrDecompiler::decompile(&binary).expect("Decompilation must succeed");
        assert_eq!(parsed.title, "Raga Bhairav Demo");
        assert_eq!(parsed.native_instrument, "harmonium");
        assert!((parsed.native_tempo_bpm - 85.0).abs() < 0.1);
        assert_eq!(parsed.native_transpose, 2);
        assert_eq!(parsed.events.len(), 2);
        assert_eq!(parsed.events[0].data1, 60);
        assert_eq!(parsed.events[0].data2, 105);
        assert_eq!(parsed.events[1].delta_us, 350_000);
    }

    #[test]
    fn test_decompile_real_esp32_recording_demo_vhr() {
        let path = std::path::Path::new("../Builder/recording_demo.vhr");
        if let Ok(bytes) = std::fs::read(path) {
            let parsed = VhrDecompiler::decompile(&bytes).expect("Must parse real demo VHR");
            assert_eq!(parsed.title, "Boot Demo Recording");
            assert_eq!(parsed.native_instrument, "harmonium");
            assert_eq!(parsed.events.len(), 117);
            println!("Parsed real ESP32 VHR demo: {} events, title='{}'", parsed.events.len(), parsed.title);
        }
    }

    #[test]
    fn test_vhr_midi_roundtrip() {
        let mut rec = VhrRecording {
            title: "MIDI Test".to_string(),
            native_instrument: "piano".to_string(),
            native_tempo_bpm: 120.0,
            native_transpose: 0,
            native_scale: 0,
            native_octave: 0,
            native_volume: 100,
            timebase_us: 1000,
            ticks_per_quarter: 480,
            events: Vec::new(),
        };

        rec.events.push(VhrEvent {
            delta_us: 0,
            event_type: VHR_EVENT_NOTE_ON,
            data1: 62, // D4
            data2: 90,
            channel: 0,
        });
        rec.events.push(VhrEvent {
            delta_us: 500_000, // 0.5s = 1 beat at 120 bpm = 480 ticks
            event_type: VHR_EVENT_NOTE_OFF,
            data1: 62,
            data2: 0,
            channel: 0,
        });

        // VHR -> MIDI
        let midi_bytes = MidiConverter::vhr_to_midi(&rec).expect("VHR to MIDI conversion must succeed");
        assert!(midi_bytes.len() > 20);

        // MIDI -> VHR
        let back_to_vhr = MidiConverter::midi_to_vhr(&midi_bytes, "MIDI Test").expect("MIDI to VHR must succeed");
        // Note: Contains 1 initial TEMPO_CHANGE event + 2 Note events = 3 events
        let note_events: Vec<&VhrEvent> = back_to_vhr.events.iter()
            .filter(|e| e.event_type == VHR_EVENT_NOTE_ON || e.event_type == VHR_EVENT_NOTE_OFF)
            .collect();
        assert_eq!(note_events.len(), 2);
        assert_eq!(note_events[0].data1, 62);
        assert_eq!(note_events[1].data1, 62);
    }
}
