#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
vhr_receiver.py -- Capture a VHR binary transmitted over Serial from the ESP32
=============================================================================
IMPORTANT: Close Arduino Serial Monitor BEFORE running this script.
           Only one program can own the COM port at a time.

Usage:
    python vhr_receiver.py                         # auto-detect COM port
    python vhr_receiver.py --port COM5             # specify port
    python vhr_receiver.py --port COM5 --out my.vhr
    python vhr_receiver.py --port COM5 --wait 120  # wait up to 120 seconds

How it works:
    The ESP32 firmware transmits the VHR binary as hex bytes framed by:
        >>>VHR_BEGIN<<<
        00 01 02 ...   (hex bytes, 16 per line)
        >>>VHR_END<<<

    After the 20-second recording finishes, the ESP32 sends this frame
    automatically. If you missed it, press SW1 (GPIO32) AFTER recording
    finishes to retransmit.

After capture, run:
    python vhr_decompiler.py recording_demo.vhr --events
"""

import sys
import os
import argparse
import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial is not installed.")
    print("Run: pip install pyserial")
    sys.exit(1)


BAUD_RATE   = 115200
BEGIN_MARK  = ">>>VHR_BEGIN<<<"
END_MARK    = ">>>VHR_END<<<"


def find_esp32_port():
    """Auto-detect the first likely ESP32 COM port."""
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ["cp210", "ch340", "ftdi", "uart", "usb serial", "esp"]):
            return p.device
    if ports:
        return ports[0].device
    return None


def list_ports():
    """Print all available COM ports."""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("  (no COM ports found)")
        return
    for p in ports:
        print(f"  {p.device:<10} {p.description}")


def receive_vhr(port: str, out_path: str, wait_seconds: int = 90):
    print(f"\n[RECV] ============================================================")
    print(f"[RECV]  VHR Receiver -- ESP32 Digital Harmonium Workstation")
    print(f"[RECV] ============================================================")
    print(f"[RECV]  Port    : {port} at {BAUD_RATE} baud")
    print(f"[RECV]  Output  : {os.path.abspath(out_path)}")
    print(f"[RECV]  Timeout : {wait_seconds} seconds")
    print(f"[RECV] ============================================================\n")

    # Check if port is actually available before blocking
    all_ports = [p.device for p in serial.tools.list_ports.comports()]
    if port not in all_ports:
        print(f"[WARN] Port {port} not found in system. Available ports:")
        list_ports()
        print(f"\n[RECV] Specify the correct port with --port. Exiting.")
        sys.exit(1)

    print(f"[RECV] Opening {port}...")
    print(f"[RECV] NOTE: Make sure Arduino Serial Monitor is CLOSED!")
    print(f"[RECV] If ESP32 already finished transmitting, press SW1 to retransmit.\n")

    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"[ERROR] Cannot open {port}: {e}")
        print(f"        Is Arduino Serial Monitor open? Close it and try again.")
        sys.exit(1)

    print(f"[RECV] Port open. Listening for VHR frame...")
    print(f"[RECV] (will wait up to {wait_seconds}s for '>>>VHR_BEGIN<<<')\n")

    hex_lines  = []
    capturing  = False
    line_count = 0
    found      = False

    import time
    deadline = time.time() + wait_seconds

    try:
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                remaining = int(deadline - time.time())
                if remaining % 10 == 0 and remaining > 0:
                    print(f"[RECV] Still waiting... {remaining}s remaining. "
                          f"(Press SW1 on ESP32 to retransmit if recording already finished)")
                continue

            line = raw.decode("utf-8", errors="replace").strip()

            # Echo all non-empty lines to console
            if line:
                print(f"  ESP32> {line}")

            if BEGIN_MARK in line:
                capturing  = True
                hex_lines  = []
                line_count = 0
                print(f"\n[RECV] *** VHR frame START detected ***")
                continue

            if END_MARK in line:
                if capturing:
                    found = True
                    print(f"[RECV] *** VHR frame END detected ({line_count} hex lines) ***\n")
                    break
                continue

            if capturing:
                stripped = line.strip()
                if stripped.startswith("#"):
                    continue
                if stripped:
                    hex_lines.append(stripped)
                    line_count += 1

        if not found:
            print(f"\n[RECV] TIMEOUT: No VHR frame received in {wait_seconds} seconds.")
            print(f"[RECV] Check:")
            print(f"  1. Arduino Serial Monitor is CLOSED (it blocks the COM port)")
            print(f"  2. Run the receiver BEFORE resetting the ESP32")
            print(f"  3. COM port is correct (detected: {port})")
            print(f"  4. Correct order: python vhr_receiver.py  ->  press EN/RST on ESP32")
            ser.close()
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n[RECV] Interrupted by user.")
        ser.close()
        sys.exit(1)
    finally:
        try:
            ser.close()
        except Exception:
            pass

    if not hex_lines:
        print("[ERROR] VHR frame markers found but no hex data captured.")
        sys.exit(1)

    # Parse hex
    hex_str   = " ".join(hex_lines)
    hex_bytes = hex_str.split()

    print(f"[RECV] Parsing {len(hex_bytes)} hex bytes...")

    try:
        raw_binary = bytes(int(b, 16) for b in hex_bytes)
    except ValueError as e:
        print(f"[ERROR] Hex parse failed: {e}")
        sys.exit(1)

    # Save file (always overwrite so decompiler always gets the freshest recording)
    with open(out_path, "wb") as f:
        f.write(raw_binary)

    print(f"[RECV] ============================================================")
    print(f"[RECV]  SUCCESS: Saved {len(raw_binary)} bytes")
    print(f"[RECV]  File: {os.path.abspath(out_path)}")
    print(f"[RECV] ============================================================")

    # Auto-run decompiler immediately so the user sees results in one step
    decompiler = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vhr_decompiler.py")
    if os.path.exists(decompiler):
        print(f"\n[RECV] Running decompiler...\n")
        import subprocess
        subprocess.run([sys.executable, decompiler, os.path.abspath(out_path), "--events"],
                       check=False)
    else:
        print(f"\n[RECV] To decompile:")
        print(f"       python vhr_decompiler.py \"{os.path.abspath(out_path)}\" --events\n")
    return out_path


def main():
    parser = argparse.ArgumentParser(
        description="VHR Receiver -- capture a VHR binary from ESP32 over Serial USB",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
IMPORTANT: Close Arduino Serial Monitor before running this script!

If the ESP32 already finished transmitting (you missed it), press SW1 on
the ESP32 board AFTER the 20-second recording -- the firmware will retransmit
the last VHR file.
"""
    )
    parser.add_argument("--port", "-p", default=None,
                        help="Serial port (e.g. COM5 or /dev/ttyUSB0). Auto-detected if omitted.")
    parser.add_argument("--list-ports", "-l", action="store_true",
                        help="List all available COM ports and exit.")
    # Default output: always save next to this script (the Builder folder),
    # regardless of which directory the user runs the command from.
    script_dir    = os.path.dirname(os.path.abspath(__file__))
    default_out   = os.path.join(script_dir, "recording_demo.vhr")
    parser.add_argument("--out", "-o", default=default_out,
                        help=f"Output .vhr filename (default: {default_out})")
    parser.add_argument("--wait", "-w", type=int, default=90,
                        help="Seconds to wait for transmission (default: 90)")
    args = parser.parse_args()

    if args.list_ports:
        print("Available COM ports:")
        list_ports()
        return

    port = args.port
    if not port:
        port = find_esp32_port()
        if not port:
            print("[ERROR] No COM port found.")
            print("        Connect ESP32 via USB, or specify: --port COM?")
            print("\nAvailable ports:")
            list_ports()
            sys.exit(1)
        print(f"[RECV] Auto-detected port: {port}")

    receive_vhr(port, args.out, wait_seconds=args.wait)


if __name__ == "__main__":
    main()
