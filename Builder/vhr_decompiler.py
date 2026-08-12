#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
vhr_decompiler.py - VHR v2.0 Native Performance Recording Decompiler
=====================================================================
Reads a .vhr binary file and prints its contents in human-readable form.

Usage:
    python vhr_decompiler.py recording.vhr
    python vhr_decompiler.py recording.vhr --events     # show full event list
    python vhr_decompiler.py recording.vhr --json       # output as JSON
    python vhr_decompiler.py recording.vhr --summary    # header summary only
"""

import sys
import os
import struct
import argparse
import json
import time
import datetime


# =============================================================================
# VHR Format Constants (mirrors vhr_format.h)
# =============================================================================

VHR_MAGIC_V2    = b'VHR2'
VHR_MAGIC_V1    = b'VHR1'
VHR_HEADER_SIZE = 96   # sizeof(VHRFileHeader) in v2.0
VHR_EVENT_SIZE  = 8    # sizeof(VHREventEntry)

# VHRFileHeader v2.0 layout (packed, little-endian):
#   [0 ..3 ] magic[4]
#   [4     ] version_major
#   [5     ] version_minor
#   [6 ..7 ] format_flags (uint16)
#   [8 ..11] total_events (uint32)
#   [12..15] duration_ms  (uint32)
#   [16..19] start_timestamp (uint32)
#   [20..23] total_file_size (uint32)
#   [24..39] native_instrument[16]
#   [40..41] native_tempo_bpm_x10 (uint16)
#   [42    ] native_transpose (int8)
#   [43    ] native_scale (uint8)
#   [44    ] native_octave (uint8)
#   [45    ] native_volume (uint8)
#   [46..55] _reserved_ctx[10]
#   [56..59] crc32 (uint32)
#   [60..61] timebase_us (uint16)
#   [62..63] ticks_per_quarter (uint16)
#   [64..67] firmware_version (uint32)
#   [68..71] _reserved_timing (uint32)
#   [72..91] title[20]
#   [92..95] _reserved_meta[4]

HEADER_FMT = "<4sBBHIIII16sHbBBB10xIHHII20s4x"
HEADER_FIELDS = [
    "magic", "version_major", "version_minor", "format_flags",
    "total_events", "duration_ms", "start_timestamp", "total_file_size",
    "native_instrument", "native_tempo_bpm_x10", "native_transpose",
    "native_scale", "native_octave", "native_volume",
    "crc32", "timebase_us", "ticks_per_quarter", "firmware_version",
    "_reserved_timing", "title"
]

# VHREventEntry layout (packed, little-endian):
#   [0..3] delta_us  (uint32)
#   [4]    type      (uint8)
#   [5]    data1     (uint8)
#   [6]    data2     (uint8)
#   [7]    channel   (uint8)
EVENT_FMT = "<IBBBB"

# Event type names
EVENT_NAMES = {
    0:  "NONE",
    1:  "NOTE_ON",
    2:  "NOTE_OFF",
    3:  "CONTROL_CHANGE",
    4:  "PROGRAM_CHANGE",
    5:  "TEMPO_CHANGE",
    6:  "TRANSPOSE_CHANGE",
    7:  "INSTRUMENT_CHANGE",
    8:  "PEDAL",
    9:  "VOLUME_CHANGE",
    10: "SCALE_CHANGE",
    11: "TUNING_CHANGE",
    12: "START_RECORD",
    13: "STOP_RECORD",
}

# Format flags
FLAG_NAMES = {
    (1 << 0): "HAS_TEMPO_EVENTS",
    (1 << 1): "HAS_CONTROL_EVENTS",
    (1 << 2): "HAS_INSTRUMENT_EVENTS",
    (1 << 3): "HAS_TRANSPOSE_EVENTS",
    (1 << 4): "HAS_VOLUME_EVENTS",
    (1 << 5): "HAS_SCALE_EVENTS",
    (1 << 6): "HAS_PEDAL_EVENTS",
}

# MIDI note names
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

def midi_note_name(n):
    if 0 <= n <= 127:
        octave = (n // 12) - 1
        return f"{NOTE_NAMES[n % 12]}{octave}"
    return f"note{n}"


# =============================================================================
# CRC-32 (IEEE 802.3, poly 0xEDB88320) - matches firmware implementation
# =============================================================================

def crc32_compute(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return (~crc) & 0xFFFFFFFF


# =============================================================================
# Color output helpers (ANSI, falls back gracefully on Windows CMD)
# =============================================================================

USE_COLOR = sys.stdout.isatty()

def _c(code, text):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text

def green(t):   return _c("32", t)
def yellow(t):  return _c("33", t)
def cyan(t):    return _c("36", t)
def red(t):     return _c("31", t)
def bold(t):    return _c("1",  t)
def dim(t):     return _c("2",  t)


# =============================================================================
# Parse header
# =============================================================================

def parse_header(data: bytes) -> dict:
    if len(data) < VHR_HEADER_SIZE:
        raise ValueError(f"File too small for VHR header: {len(data)} bytes (need {VHR_HEADER_SIZE})")

    raw = struct.unpack_from(HEADER_FMT, data, 0)
    h = dict(zip(HEADER_FIELDS, raw))

    # Decode byte strings
    h["magic"]             = h["magic"]
    h["native_instrument"] = h["native_instrument"].rstrip(b"\x00").decode("ascii", errors="replace")
    h["title"]             = h["title"].rstrip(b"\x00").decode("ascii", errors="replace")

    # Computed fields
    h["tempo_bpm"]  = h["native_tempo_bpm_x10"] / 10.0 if h["native_tempo_bpm_x10"] else 0.0
    h["duration_s"] = h["duration_ms"] / 1000.0

    # Format flags as list
    h["format_flag_names"] = [name for bit, name in FLAG_NAMES.items()
                               if h["format_flags"] & bit]

    return h


# =============================================================================
# Parse event stream
# =============================================================================

def parse_events(data: bytes, total_events: int) -> list:
    events = []
    offset = VHR_HEADER_SIZE
    abs_time_us = 0

    for i in range(total_events):
        if offset + VHR_EVENT_SIZE > len(data):
            print(red(f"  [WARN] Truncated at event {i} (expected {total_events})"))
            break

        delta_us, typ, d1, d2, ch = struct.unpack_from(EVENT_FMT, data, offset)
        abs_time_us += delta_us

        ev = {
            "index":       i,
            "delta_us":    delta_us,
            "abs_time_us": abs_time_us,
            "abs_time_ms": abs_time_us / 1000.0,
            "type_id":     typ,
            "type_name":   EVENT_NAMES.get(typ, f"UNKNOWN_{typ}"),
            "data1":       d1,
            "data2":       d2,
            "channel":     ch,
        }

        # Annotate event meaning
        if typ == 1:  # NOTE_ON
            ev["annotation"] = f"{midi_note_name(d1)} vel={d2}"
        elif typ == 2:  # NOTE_OFF
            ev["annotation"] = f"{midi_note_name(d1)}"
        elif typ == 3:  # CC
            ev["annotation"] = f"cc={d1} val={d2}"
        elif typ == 5:  # TEMPO_CHANGE
            bpm_x10 = (d1 << 8) | d2
            ev["annotation"] = f"{bpm_x10 / 10.0:.1f} BPM"
        elif typ == 6:  # TRANSPOSE
            ev["annotation"] = f"{struct.unpack('b', bytes([d1]))[0]:+d} semitones"
        elif typ == 8:  # PEDAL
            ev["annotation"] = f"pedal={d1} {'ON' if d2 >= 64 else 'OFF'}"
        elif typ == 9:  # VOLUME
            ev["annotation"] = f"vol={d1}"
        elif typ == 10: # SCALE
            ev["annotation"] = f"scale={d1} root={NOTE_NAMES[d2 % 12] if d2 < 12 else d2}"
        elif typ == 11: # TUNING
            coarse = struct.unpack('b', bytes([d1]))[0]
            fine   = struct.unpack('b', bytes([d2]))[0]
            ev["annotation"] = f"coarse={coarse:+d}st fine={fine:+d}c"
        else:
            ev["annotation"] = f"d1={d1} d2={d2}"

        events.append(ev)
        offset += VHR_EVENT_SIZE

    return events


# =============================================================================
# CRC verification
# =============================================================================

def verify_crc(data: bytes, header: dict) -> tuple:
    event_stream = data[VHR_HEADER_SIZE : VHR_HEADER_SIZE + header["total_events"] * VHR_EVENT_SIZE]
    computed = crc32_compute(event_stream)
    stored   = header["crc32"]
    ok       = (computed == stored)
    return ok, computed, stored


# =============================================================================
# Pretty print
# =============================================================================

def print_header(h: dict, crc_ok: bool, computed_crc: int):
    magic_str = h["magic"].decode("ascii", errors="replace")
    magic_ok  = h["magic"] in (VHR_MAGIC_V2, VHR_MAGIC_V1)

    print()
    print(bold("=" * 64))
    print(bold("  VHR File Decompiler - Virtual Harmonium Recording v2.0"))
    print(bold("=" * 64))
    print()

    # ── Format Identity ──
    print(bold("  FORMAT IDENTITY"))
    print(f"    Magic          : {green(magic_str)}{'' if magic_ok else red(' ← INVALID!')}")
    print(f"    Version        : {h['version_major']}.{h['version_minor']}")
    print(f"    Format Flags   : 0x{h['format_flags']:04X}")
    if h["format_flag_names"]:
        for flag in h["format_flag_names"]:
            print(f"                     {dim('•')} {flag}")
    else:
        print(f"                     {dim('(none)')}")
    print()

    # ── Performance Stats ──
    dur_m = int(h["duration_ms"]) // 60000
    dur_s = (int(h["duration_ms"]) % 60000) / 1000.0
    print(bold("  PERFORMANCE STATISTICS"))
    print(f"    Total Events   : {yellow(str(h['total_events']))}")
    print(f"    Duration       : {yellow(f'{dur_m}m {dur_s:.3f}s')}  ({h['duration_ms']} ms)")
    print(f"    File Size      : {h['total_file_size']} bytes  (header={VHR_HEADER_SIZE}, events={h['total_events']}×{VHR_EVENT_SIZE})")
    if h["start_timestamp"]:
        dt = datetime.datetime.utcfromtimestamp(h["start_timestamp"])
        print(f"    Recorded At    : {dt.strftime('%Y-%m-%d %H:%M:%S')} UTC")
    else:
        print(f"    Recorded At    : {dim('(timestamp not available)')}")
    print()

    # ── Native Context ──
    print(bold("  NATIVE RECORDING CONTEXT"))
    print(f"  {dim('  (These are metadata values SET AT RECORD TIME, not measured or derived.)')}")
    print(f"  {dim('  Tempo/transpose/volume here = what was configured when recording started.')}")
    print(f"  {dim('  Live changes during performance appear as events in the EVENT STREAM below.')}")
    print()
    print(f"    Instrument     : {cyan(h['native_instrument'] or '(not specified)')}"
          f"  {dim('(preferred VHL to load for playback)')}")
    if h["native_tempo_bpm_x10"]:
        print(f"    Tempo at start : {h['tempo_bpm']:.1f} BPM  {dim('(stored as BPM x10 = ' + str(h['native_tempo_bpm_x10']) + ')')}")
    else:
        print(f"    Tempo at start : {dim('(not recorded)')}")
    print(f"    Transpose      : {h['native_transpose']:+d} semitones  {dim('(semitone offset at record start)')}")
    print(f"    Scale Index    : {h['native_scale']}  {dim('(0 = chromatic/no scale)')}")
    print(f"    Octave Shift   : {h['native_octave']}  {dim('(0 = default octave)')}")
    print(f"    Volume         : {h['native_volume'] if h['native_volume'] else dim('(not recorded)')}")
    print()

    # ── Timing & Integrity ──
    print(bold("  TIMING & INTEGRITY"))
    print(f"    Timebase       : {h['timebase_us']} µs/tick  ({1000000//max(h['timebase_us'],1)} ticks/sec)")
    print(f"    Ticks/Quarter  : {h['ticks_per_quarter']}")
    print(f"    Firmware Ver   : {h['firmware_version']}")
    crc_str = f"0x{h['crc32']:08X} (computed 0x{computed_crc:08X})"
    if crc_ok:
        print(f"    CRC-32         : {green(crc_str + '  [OK] MATCH')}")
    else:
        print(f"    CRC-32         : {red(crc_str + '  [FAIL] MISMATCH -- file may be corrupt!')}")
    print()

    # ── Metadata ──
    print(bold("  METADATA"))
    print(f"    Title          : {cyan(h['title'] or '(empty)')}")
    print()


def print_events(events: list, max_show: int = None):
    if not events:
        print(dim("  (no events)"))
        return

    total = len(events)
    show  = events if max_show is None else events[:max_show]

    # Column widths
    print(bold("  EVENT STREAM"))
    print(f"  {'#':>5}  {'Time(ms)':>10}  {'d(us)':>9}  {'Type':<22}  {'Ch':>2}  {'Detail'}")
    print(f"  {'-'*5}  {'-'*10}  {'-'*9}  {'-'*22}  {'-'*2}  {'-'*30}")

    for ev in show:
        t_ms = f"{ev['abs_time_ms']:>10.3f}"

        if ev["type_id"] == 1:   # NOTE_ON
            type_str = green(f"{'NOTE_ON':<22}")
        elif ev["type_id"] == 2: # NOTE_OFF
            type_str = yellow(f"{'NOTE_OFF':<22}")
        elif ev["type_id"] in (5, 6, 9, 10, 11): # param changes
            type_str = cyan(f"{ev['type_name']:<22}")
        elif ev["type_id"] in (12, 13): # lifecycle
            type_str = dim(f"{ev['type_name']:<22}")
        else:
            type_str = f"{ev['type_name']:<22}"

        print(f"  {ev['index']:>5}  {t_ms}  {ev['delta_us']:>9}  {type_str}  "
              f"{ev['channel']:>2}  {ev['annotation']}")

    if max_show is not None and total > max_show:
        print(dim(f"  ... {total - max_show} more events (use --events to show all)"))
    print()


def print_statistics(events: list, header: dict):
    if not events:
        return

    note_on  = [e for e in events if e["type_id"] == 1]
    note_off = [e for e in events if e["type_id"] == 2]

    print(bold("  EVENT SUMMARY"))
    by_type = {}
    for ev in events:
        by_type[ev["type_name"]] = by_type.get(ev["type_name"], 0) + 1
    for name, count in sorted(by_type.items(), key=lambda x: -x[1]):
        print(f"    {name:<24} : {count}")
    print()

    if note_on:
        notes_used = sorted(set(e["data1"] for e in note_on))
        note_names = [midi_note_name(n) for n in notes_used]
        velocities = [e["data2"] for e in note_on]
        print(bold("  NOTE ANALYSIS"))
        print(f"    Unique notes played : {len(notes_used)}")
        print(f"    Notes               : {', '.join(note_names)}")
        print(f"    Velocity range      : {min(velocities)}–{max(velocities)}")
        print(f"    Avg velocity        : {sum(velocities)/len(velocities):.1f}")
        print()

    # Timing analysis
    if len(events) > 1:
        deltas = [e["delta_us"] for e in events if e["delta_us"] > 0]
        if deltas:
            print(bold("  TIMING ANALYSIS"))
            print(f"    Shortest gap        : {min(deltas):,} µs  ({min(deltas)/1000:.3f} ms)")
            print(f"    Longest gap         : {max(deltas):,} µs  ({max(deltas)/1000:.3f} ms)")
            print(f"    Avg gap             : {sum(deltas)//len(deltas):,} µs")
            print()

def find_latest_vhr(search_dir: str):
    """Find the most recently modified .vhr file in search_dir."""
    import glob
    pattern = os.path.join(search_dir, "*.vhr")
    files = glob.glob(pattern)
    if not files:
        return None
    return max(files, key=os.path.getmtime)


def main():
    parser = argparse.ArgumentParser(
        description="VHR v2.0 Binary Decompiler - read and inspect a .vhr performance recording",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  python vhr_decompiler.py                           # auto-pick latest .vhr in current dir
  python vhr_decompiler.py recording_demo.vhr        # specific file
  python vhr_decompiler.py recording.vhr --events    # show all events
  python vhr_decompiler.py recording.vhr --summary   # header only
  python vhr_decompiler.py recording.vhr --json      # JSON output"""
    )
    parser.add_argument("file", nargs="?", default=None,
                        help="Path to the .vhr binary file (auto-detected if omitted)")
    parser.add_argument("--events", "-e", action="store_true",
                        help="Print full event list (all events, not just the first 50)")
    parser.add_argument("--summary", "-s", action="store_true",
                        help="Print header summary only (no events)")
    parser.add_argument("--json", "-j", action="store_true",
                        help="Output full parsed data as JSON")
    parser.add_argument("--max-events", "-n", type=int, default=50,
                        help="Max events to display in table (default: 50, use --events for all)")
    args = parser.parse_args()

    # Auto-discover .vhr file if not provided
    path = args.file
    if not path:
        search_dir = os.path.dirname(os.path.abspath(__file__))
        import glob
        vhr_files = sorted(glob.glob(os.path.join(search_dir, "*.vhr")),
                           key=os.path.getmtime, reverse=True)
        if not vhr_files:
            print(red("[ERROR] No .vhr file specified and none found in the current directory."))
            print(f"        Usage: python {os.path.basename(__file__)} <recording.vhr>")
            sys.exit(1)

        if len(vhr_files) == 1:
            path = vhr_files[0]
            print(f"[AUTO] Found 1 .vhr file: {os.path.basename(path)}")
        else:
            print(f"[AUTO] Found {len(vhr_files)} .vhr files (most recent first):")
            for i, f in enumerate(vhr_files):
                size = os.path.getsize(f)
                mtime = os.path.getmtime(f)
                dt = datetime.datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S")
                print(f"  [{i+1}] {os.path.basename(f):<40}  {size:>6} bytes  {dt}")
            print()
            # Default to the most recent
            path = vhr_files[0]
            print(f"[AUTO] Using most recent: {os.path.basename(path)}")
            print(f"       (specify a file explicitly to open a different one)")

    if not os.path.exists(path):
        print(red(f"[ERROR] File not found: {path}"))
        sys.exit(1)

    with open(path, "rb") as f:
        data = f.read()

    file_mtime   = os.path.getmtime(path)
    file_age_sec = time.time() - file_mtime
    file_saved   = datetime.datetime.fromtimestamp(file_mtime).strftime("%Y-%m-%d %H:%M:%S")

    if file_age_sec < 60:
        age_str = f"{int(file_age_sec)}s ago  [FRESH]"
    elif file_age_sec < 3600:
        age_str = f"{int(file_age_sec/60)}m ago"
    else:
        age_str = f"{int(file_age_sec/3600)}h ago"

    file_size = len(data)
    print(f"\n{'='*64}")
    print(f"  READING FILE: {os.path.basename(path)}")
    print(f"  Path  : {os.path.abspath(path)}")
    print(f"  Size  : {file_size} bytes")
    print(f"  Saved : {file_saved}  ({age_str})")
    if file_age_sec > 60:
        print(f"  *** WARNING: This file is {int(file_age_sec)}s old. ***")
        print(f"  *** Make sure vhr_receiver.py already captured the  ***")
        print(f"  *** latest recording before running the decompiler.  ***")
    print(f"{'='*64}")

    # Detect version
    if len(data) < 4:
        print(red("[ERROR] File is too small to be a VHR file."))
        sys.exit(1)

    magic = data[0:4]
    if magic == VHR_MAGIC_V1:
        print(yellow("[WARN] This appears to be a VHR v1.0 file (magic 'VHR1')."))
        print(yellow("       The decompiler supports v2.0 ('VHR2'). Header parsing may be inaccurate."))
    elif magic != VHR_MAGIC_V2:
        print(red(f"[ERROR] Invalid magic bytes: {magic!r} — not a VHR file."))
        sys.exit(1)

    # Parse
    try:
        header = parse_header(data)
    except Exception as e:
        print(red(f"[ERROR] Header parse failed: {e}"))
        sys.exit(1)

    crc_ok, computed_crc, stored_crc = verify_crc(data, header)

    events = []
    if not args.summary:
        events = parse_events(data, header["total_events"])

    # JSON output
    if args.json:
        out = {
            "file": os.path.abspath(path),
            "file_size_bytes": file_size,
            "header": {k: (v.hex() if isinstance(v, bytes) else v)
                       for k, v in header.items()},
            "crc_ok": crc_ok,
            "events": events,
        }
        # Make JSON-serializable
        out["header"].pop("_reserved_timing", None)
        print(json.dumps(out, indent=2))
        return

    # Pretty print
    print_header(header, crc_ok, computed_crc)

    if not args.summary:
        max_ev = None if args.events else args.max_events
        print_events(events, max_show=max_ev)
        print_statistics(events, header)

    # Final status line
    if crc_ok:
        print(green("  [OK] File integrity verified (CRC-32 match)"))
    else:
        print(red("  [FAIL] CRC-32 MISMATCH -- file may be truncated or corrupt"))

    print(bold("=" * 64))
    print()


if __name__ == "__main__":
    main()
