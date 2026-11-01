# FMIR pixel protocol v1

The wire carries actual panel pixels, never UI parameters or layout instructions.
All framebuffer ownership is on the computer. The protocol is independent of
Skiff fonts, engines and UI pages. RGB565 pixel bytes are big-endian (panel order).
Multi-byte header fields and compression lengths are little-endian.

Every record starts with this 28-byte header (`<4sBBBBI6HI`):

| Offset | Field |
|---|---|
| 0 | `FMIR` magic, four bytes |
| 4 | Protocol version, u8 = 1 |
| 5 | Kind, u8: HELLO=1, BEGIN=2, RECT=3, COMMIT=4, INVALID=5 |
| 6 | Flags, u8: bit 0 = full transaction on BEGIN; others reserved |
| 7 | Codec, u8: raw RGB565=0, RLE=1, fill=2, LZ4 block=3 |
| 8 | Sequence, u32; Skiff transport uses modulo 16384 |
| 12,14 | Canvas width and height, u16 each; initial FM-1 implementation 240 × 240 |
| 16,18 | Rectangle x and y, u16 each |
| 20,22 | Rectangle width and height, u16 each |
| 24 | Payload byte length, u32, at most 115200 |

HELLO carries a printable ASCII firmware identifier (1–64 bytes). An identifier
is metadata, not a claim that the bridge supports that firmware's USB adapter.
BEGIN/COMMIT/INVALID have no payload. RECT must belong to an open transaction
with the same sequence. RLE is repeated `[count:u16, color:2 panel-order bytes]`.
Fill contains one two-byte color. LZ4 is a standard independent block, with output
size exactly `w*h*2`; overlapping matches are allowed, out-of-range references
and oversized expansion are rejected. No dictionaries persist between records.

A full transaction must cover every pixel before COMMIT. A delta requires a
previous valid frame and the next consecutive sequence. The decoder keeps one
bounded staging image and one current image, and publishes only at COMMIT.
Gaps, malformed records, overflow or disconnect invalidate the display and
require a full transaction. A new full transaction may establish any sequence.
No truncated or incomplete update may be shown as a current physical screen.

## Experimental Skiff MIDI binding

This binding explicitly extends the existing editor protocol; it does not
change USB speed. The separate video-only profile omits USB audio interfaces.
It uses noncommercial SysEx `F0 7D 46 4C 53 op args… F7` (`53` hex = command 83).
Do not enable competing tools that use a different meaning for this command.
The firmware hook is off by default and is **not hardware-qualified**.

| op | Request | Response after command 83 |
|---|---|---|
| 0 | capability, no arguments | `0, version=2 (MIDI) or 3 (bulk), width14, height14, ASCII Skiff` |
| 1 | start/resynchronize | `1` |
| 2 | `offset14` | `2, state, generation14, total14, offset14, packed-data…` |
| 3 | stop | `3` |
| 4 | `generation14` | `4`; acknowledges and releases that transaction |

A 14-bit value is two 7-bit bytes, least significant first. State 0 is idle,
1 is a retained transaction chunk, 2 requires full recovery. Chunks contain at
most 96 raw bytes for capability version 1, or 154 for version 2, packed as a high-bit mask followed by up to seven low-7-bit
bytes per group. An offset request is nondestructive and may be retried. The
complete transaction is retained until its generation is acknowledged. Its
16,320-byte queue is fixed, never allocated from the heap. Mirroring expires after
three seconds without a request. Malformed commands are ignored.

The bridge waits for each reply. Legacy firmware and idle polls are limited to
250 requests/s; capability version 2 permits up to 1000 active chunk requests/s.
Its largest reply is 190 MIDI bytes / 64 USB-MIDI events, fitting the existing
64-event ring. The maximum is a host request budget, not measured throughput.
USB endpoint scheduling and main-loop work impose additional limits. The firmware accepts
a complete small response only when existing MIDI and SysEx queues are idle;
there are no wait loops. Notes arriving during a short SysEx still wait until
its end, as required for MIDI message integrity. USB audio is disabled in this profile; local synthesis remains enabled. See the feature README for measured development results and hardware limits.

## Local WebSocket binding

HTTP and WebSocket bind only to `127.0.0.1`. A per-run random token is required
for the viewer and WebSocket, with exact Host and Origin validation. There is
no CORS permission and no synth-control route. Only WS ping/pong/close are
accepted from clients; text/binary application messages terminate the session.
Do not share the private URL. This is not an OS security boundary against a
process capable of inspecting this user's terminal or process memory.

Text messages are connection status JSON. Binary messages are self-contained
full BEGIN/RECT(raw)/COMMIT transactions. The bridge keeps one replaceable
snapshot per client, at most four clients, so a slow viewer cannot create an
unbounded queue or miss a required delta. Local full snapshots do not traverse
USB. The viewer compares pixels and updates only a changed bounding region.

## Experimental dedicated CDC binding (capability version 3)

Version 3 advertises a separate bulk profile. Use the bulk bridge rather than
MIDI pixel pulls. The CDC text console is absent in this profile. Host commands
are exactly five bytes: ASCII `FMIR` followed by operation 0 (stop), 1 (start),
or 2 (keepalive). No arbitrary control/console command is interpreted. These
commands are emitted only by the native bridge; the browser remains read-only.

On start the device emits a 41-byte FMIR HELLO identifying `Skiff stream4`,
then streams retained FMIR transactions directly from the completed journal
through bounded ISR copies into separate EP3 DMA storage.
There is no pack7 or per-chunk request. A transaction is released only after its
bytes have been queued in order. Missing later LCD updates require a subsequent
full redraw, so a dropped screen does not create an invalid delta chain. The
host discards a bounded stale prefix until the exact HELLO signature, then uses
the same strict decoder. It sends a keepalive each second; firmware capture
expires after three seconds without one. DTR loss stops capture. No full-speed
USB bus limit is altered; the 10 kHz firmware poll is only a service cadence.

Bulk full recovery may span multiple retained sections of the same generation:
the first contains BEGIN(full), later sections continue RECT records, and only
the last contains COMMIT. Queue release is internal to the CDC producer; the
host decoder keeps the pending image across sections. Held-map recovery
uses two horizontal 120-row sections; normal pages use a single whole capture. A section is not a separately publishable
frame. All memory and full-coverage checks still apply.

The bulk profile transmits 64-byte packets and emits one zero-length packet
when an exact multiple ends, completing a pending host bulk read. This affects
USB packet boundaries only; FMIR framing and pixel reconstruction are unchanged.
Graph deltas may contain only changed four-row strips, selected using bounded
64-bit fingerprints committed when their pixel transaction is queued.

Each retained section has an internal advancing chunk ID independent of the FMIR
sequence shared across a multi-section transaction. Known missed header, footer
and graph rectangles on normal LFO-family pages are restored with fresh renderer
deltas; unknown/layout-changing writes still require a full transaction. The
bridge also recognizes earlier development HELLO identifiers for compatibility.

## Read-only device timing

Command 83 operation 5 has no arguments. It responds with schema byte 1 then
six little-endian 14-bit values: capture/compression ms, pre-transmission queue
ms, first-to-final packet-copy ms, final-copy-to-main-ACK ms, compressed bytes,
and generation. Durations saturate at 16383; readings refer to the last completed
retained section. A non-bulk mirror responds with schema 0 (unsupported).
Timing telemetry uses the existing MIDI diagnostic channel, separate from pixels.
The USB timing ends at the final packet copy into DMA, not the final host ACK
or browser presentation; these values are individual last-section samples.
