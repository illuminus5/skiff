# Experimental six-track stereo USB capture

Hardware result: macOS enumerated twelve channels, but the user reported local
Skiff output distorting whenever Audacity started recording. This experiment is
not a working release. The exact bottleneck (DSP/USB service/FIFO) is unproven.

Package: build/skiff-1.4-usb12.fwsc. No automatic installation.
Build: FELUCCA_USB_12CH=1 FELUCCA_CDC_DEFAULT=0 ./build.sh --release 1.4-usb12

Fixed channels 1/2=T1 L/R, 3/4=T2 L/R, 5/6=T3 L/R, 7/8=T4 L/R,
9/10=T5 L/R, 11/12=T6 L/R. Twelve 16-bit channels at 22,050 frames/sec.
The two selectable USB route controls are hidden in this build; their stored
preferences are preserved for return to the stereo routing package. Track taps
use existing dry track processing/gain/pan/mute; shared master effects are not
separate track returns. Local synthesis/output stays at its existing rate.

This is a different USB topology, not a runtime mode. AudioControl/AudioStreaming
remain contiguous and MIDI is outside the AudioControl collection. USB SERIAL
should be OFF; the boot default does not override an existing saved setting.
The USB descriptor revision differs from stereo builds to avoid stale caching.

A 63-tap Blackman low-pass (8,820 Hz cutoff, Q15) precedes 2:1 decimation. The
filter is approximately flat through 7 kHz, about -1 dB at 8 kHz; the host test
requires alias-band gain <0.002 above 11.1 kHz. This trades treble extension for
channel count. Buffering: 256 frames x 24 bytes, 576-byte maximum EP4 packet
(24 frames, including clock-drift correction). No host audio return.

Build verification: image 564,668 bytes; RAM data/bss 90,644 / 98,304; pool
346,316 / 356,352; RAM code 24,080 / 24,576. Descriptor, ordered capture, filtering,
whole-frame overflow/underrun, 22,050-frame packet cadence and memory guard tests
pass ASan/UBSan. All 38 mixed-engine scenes match on one/two cores, including
all twelve captured channels. Existing stereo UAC host regression passes.

These do not prove EP4's physical FIFO/DMA can sustain 576-byte packets, sufficient
DSP headroom, or host acceptance. Hardware acceptance must include actual
recordings with all six tracks isolated, changing mute/solo/pan, heavy voices,
CPU1/CPU2, reconnects and long running streams. No claims of hardware success.

## Direct TX-6 use

TE documents TX-6 audio host/device capability, but its documented 44.1/48 kHz
interface modes do not establish generic host support for twelve channels at
22.05 kHz. Test the known-working stereo Skiff firmware directly first. A Mac
recognizing this experimental package does not prove direct TX-6 compatibility.
If the TX-6 refuses 22.05 kHz, investigate six MONO channels at 44.1 kHz next;
twelve mono channels at 44.1 kHz use the same bandwidth as six stereo pairs and
still exceed a single full-speed endpoint. Do not assume proprietary TE device
integrations establish generic USB host compatibility.

Restore build/skiff-1.4-macroute.fwsc to return to hardware-tested stereo capture
at 44.1 kHz. Settings and project layouts are unchanged from that package.
