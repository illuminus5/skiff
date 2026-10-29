#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read EP0 history from a FELUCCA_USB_TRACE=1 diagnostic build over MIDI.

Does not flash, clear the history, change settings or open USB exclusively.
Uses CoreMIDI through mido/python-rtmidi. Stock builds do not answer command 82.
"""
import argparse
import time

PREFIX = [0x7D, 0x46, 0x4C, 82]


def decode(payload):
    p = list(payload)
    if len(p) != 337 or p[0] != 2:
        raise ValueError("Unsupported or incomplete trace header")
    if any(x < 0 or x > 127 for x in p):
        raise ValueError("Trace contains non-MIDI data")
    raw = bytearray()
    for off in range(1, len(p), 8):
        raw.extend(p[off + 1 + j] | ((p[off] >> j) & 1) << 7 for j in range(7))
    total = int.from_bytes(raw[288:292], "little")
    next_slot, count = raw[292:294]
    if count > 32 or next_slot > 31 or (count < 32 and next_slot != count):
        raise ValueError("Invalid trace ring metadata")
    entries = []
    for i in range(count):
        slot = (next_slot - count + i) % 32
        off = slot * 9
        if raw[off + 8] > 1:
            raise ValueError("Invalid trace stall flag")
        entries.append((bool(raw[off + 8]), bytes(raw[off:off + 8])))
    return total, entries


def describe(raw):
    bm, request = raw[:2]
    value, index, length = (int.from_bytes(raw[i:i+2], "little") for i in (2, 4, 6))
    names = {0: "GET_STATUS", 1: "CLEAR_FEATURE", 3: "SET_FEATURE",
             5: "SET_ADDRESS", 6: "GET_DESCRIPTOR", 8: "GET_CONFIGURATION",
             9: "SET_CONFIGURATION", 10: "GET_INTERFACE", 11: "SET_INTERFACE"}
    if bm & 0x60 == 0:
        name = names.get(request, "STANDARD")
    elif bm & 0x60 == 0x20:
        name = {1: "SET_CUR", 0x81: "GET_CUR", 0x82: "GET_MIN",
                0x83: "GET_MAX", 0x84: "GET_RES"}.get(request, "CLASS")
    else:
        name = "VENDOR/OTHER"
    return (f"{name}: bmRequestType=0x{bm:02x} bRequest=0x{request:02x} "
            f"wValue=0x{value:04x} wIndex=0x{index:04x} wLength={length}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list MIDI ports only")
    parser.add_argument("--input", help="exact FM-1 MIDI input port name")
    parser.add_argument("--output", help="exact FM-1 MIDI output port name")
    args = parser.parse_args()
    try:
        import mido
    except ImportError:
        parser.exit(1, "Requires mido and python-rtmidi in your Python environment.\n")
    if args.list:
        print("Inputs:", *mido.get_input_names(), sep="\n  ")
        print("Outputs:", *mido.get_output_names(), sep="\n  ")
        return
    if not args.input or not args.output:
        parser.error("Use --list, then specify --input and --output explicitly")
    with mido.open_input(args.input) as source, mido.open_output(args.output) as dest:
        dest.send(mido.Message("sysex", data=PREFIX))
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            for message in source.iter_pending():
                if message.type != "sysex" or list(message.data[:4]) != PREFIX:
                    continue
                total, entries = decode(message.data[4:])
                print(f"Observed {total} SETUPs (32-bit counter); retained {len(entries)} latest")
                for stalled, raw in entries:
                    print("STALL" if stalled else "SEEN ", raw.hex(" "), describe(raw))
                print("SEEN does not prove successful completion; STALL alone does not prove an error.")
                return
            time.sleep(0.01)
    parser.exit(1, "No trace reply. Requires a diagnostic firmware built with FELUCCA_USB_TRACE=1.\n")


if __name__ == "__main__":
    main()
