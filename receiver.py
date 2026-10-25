#!/usr/bin/env python3
"""
receiver.py — High-Performance USB Serial Audio Receiver & Laptop Speaker Player
Project: Digital Harmonium Workstation (ESP32 -> Laptop USB Streaming)

Streams 16-bit PCM audio from ESP32 over USB Serial (CP210x / CH340) and plays
it directly through your laptop speakers with low latency using sounddevice.

Packet Wire Format:
  [0..1]: Magic Bytes  (0xA5, 0x5A)
  [2..3]: Sample Rate  (uint16_t LE, e.g. 22050 or 44100 Hz)
  [4..5]: Sample Count (uint16_t LE, e.g. 32 or 64 samples)
  [6.. ]: PCM Data     (int16_t LE, 1 channel mono)
"""

import sys
import time
import struct
import argparse
import threading
import queue
import numpy as np
import serial
import serial.tools.list_ports
import sounddevice as sd

MAGIC = bytes([0xA5, 0x5A])

def find_esp32_port():
    """Auto-detect ESP32 USB-to-UART bridge port."""
    ports = list(serial.tools.list_ports.comports())
    # 1. Look for known ESP32 USB-UART chipsets
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if "cp210" in desc or "ch340" in desc or "usb to uart" in desc or "cp210" in hwid:
            return p.device
    # 2. Return first COM port if available
    if ports:
        return ports[0].device
    return None

def main():
    parser = argparse.ArgumentParser(description="ESP32 USB Audio Receiver & Player")
    parser.add_argument("-p", "--port", type=str, default=None,
                        help="Serial COM port (default: auto-detect CP210x / COM7)")
    parser.add_argument("-b", "--baud", type=int, default=921600,
                        help="Baud rate (default: 921600)")
    parser.add_argument("-d", "--device", type=int, default=None,
                        help="Sounddevice output device ID (default: system default)")
    parser.add_argument("-v", "--volume", type=float, default=1.0,
                        help="Software volume gain multiplier (default: 1.0)")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("[ERROR] No serial COM port found! Ensure your ESP32 is plugged in via USB.")
        sys.exit(1)

    print("=" * 65)
    print("  ESP32 Digital Harmonium — USB Audio Receiver & Player")
    print("=" * 65)
    print(f"  Target Serial Port : {port}")
    print(f"  Baud Rate          : {args.baud}")
    print(f"  Software Gain      : {args.volume:.2f}x")
    print("=" * 65)

    try:
        ser = serial.Serial(port, args.baud, timeout=1.0)
        ser.reset_input_buffer()
    except Exception as e:
        print(f"[ERROR] Could not open serial port {port}: {e}")
        sys.exit(1)

    print(f"[SERIAL] Connected to {port} @ {args.baud} baud.")
    print("[AUDIO] Waiting for ESP32 audio stream synchronization...")

    # Audio stream state
    current_sample_rate = 22050
    audio_queue = queue.Queue(maxsize=120)
    stop_event = threading.Event()
    audio_stream = None

    # Stats
    stats = {
        "packets": 0,
        "peak": 0.0,
        "underflow": 0
    }

    def audio_callback(outdata, frames, time_info, status):
        nonlocal stats
        if status.output_underflow:
            stats["underflow"] += 1

        needed_samples = frames
        collected = []
        samples_left = needed_samples

        while samples_left > 0:
            try:
                chunk = audio_queue.get_nowait()
                if len(chunk) <= samples_left:
                    collected.append(chunk)
                    samples_left -= len(chunk)
                else:
                    # Partial chunk used
                    collected.append(chunk[:samples_left])
                    # Put leftover back in queue
                    audio_queue.queue.appendleft(chunk[samples_left:])
                    samples_left = 0
            except queue.Empty:
                break

        if collected:
            block = np.concatenate(collected)
            if len(block) < needed_samples:
                # Pad remaining with silence
                pad = np.zeros(needed_samples - len(block), dtype=np.float32)
                block = np.concatenate([block, pad])
        else:
            block = np.zeros(needed_samples, dtype=np.float32)

        # Apply user volume gain
        if args.volume != 1.0:
            block = block * args.volume

        # Stereo mirror to both speaker channels
        outdata[:, 0] = block
        outdata[:, 1] = block

        # Measure peak amplitude
        peak = float(np.max(np.abs(block)))
        stats["peak"] = max(peak, stats["peak"] * 0.85)

    def init_stream(sr):
        nonlocal audio_stream
        if audio_stream is not None:
            audio_stream.stop()
            audio_stream.close()
        print(f"\n[AUDIO] Starting playback stream at {sr} Hz stereo...")
        audio_stream = sd.OutputStream(
            samplerate=sr,
            channels=2,
            dtype='float32',
            blocksize=256,
            device=args.device,
            callback=audio_callback
        )
        audio_stream.start()

    # Reader thread
    def serial_reader_loop():
        nonlocal current_sample_rate
        stream_initialized = False

        while not stop_event.is_set():
            try:
                # 1. Search for sync magic (0xA5, 0x5A)
                b = ser.read(1)
                if not b:
                    continue
                if b[0] != 0xA5:
                    continue
                b2 = ser.read(1)
                if not b2 or b2[0] != 0x5A:
                    continue

                # 2. Read 4 bytes: uint16 sample_rate, uint16 num_samples
                header = ser.read(4)
                if len(header) < 4:
                    continue
                sample_rate, num_samples = struct.unpack('<HH', header)

                if sample_rate not in (22050, 44100, 48000, 32000) or num_samples > 512 or num_samples == 0:
                    continue

                # 3. Read PCM data (num_samples * 2 bytes)
                pcm_bytes = ser.read(num_samples * 2)
                if len(pcm_bytes) < num_samples * 2:
                    continue

                # If sample rate changed or not yet initialized
                if not stream_initialized or sample_rate != current_sample_rate:
                    current_sample_rate = sample_rate
                    init_stream(sample_rate)
                    stream_initialized = True

                # Convert int16 LE to float32 normalized [-1.0, 1.0]
                samples_int16 = np.frombuffer(pcm_bytes, dtype=np.int16)
                samples_float = samples_int16.astype(np.float32) / 32768.0

                stats["packets"] += 1

                # Push to audio queue (drop oldest if full)
                try:
                    audio_queue.put_nowait(samples_float)
                except queue.Full:
                    try:
                        audio_queue.get_nowait()
                    except queue.Empty:
                        pass
                    audio_queue.put_nowait(samples_float)

            except Exception as e:
                if not stop_event.is_set():
                    print(f"\n[SERIAL ERROR] {e}")
                    time.sleep(0.5)

    reader_thread = threading.Thread(target=serial_reader_loop, daemon=True)
    reader_thread.start()

    # Main UI loop: Real-time peak meter and stats
    try:
        while True:
            time.sleep(0.08)
            peak = stats["peak"]
            meter_len = int(peak * 25)
            meter_str = "=" * meter_len + " " * (25 - meter_len)
            q_size = audio_queue.qsize()
            sr_khz = current_sample_rate / 1000.0

            sys.stdout.write(
                f"\r[STREAM] {sr_khz:.1f} kHz | Pkts: {stats['packets']:<6} | Q: {q_size:2d}/120 | VU: [{meter_str}] Peak: {peak:.2f} "
            )
            sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n\n[SYSTEM] Stopping audio player...")
    finally:
        stop_event.set()
        if audio_stream is not None:
            audio_stream.stop()
            audio_stream.close()
        ser.close()
        print("[SYSTEM] Audio player stopped cleanly. Goodbye!")

if __name__ == "__main__":
    main()
