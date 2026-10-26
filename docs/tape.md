# TAPE auxiliary lane

Experimental and opt-in: build with `FELUCCA_TAPE=1`. This branch depends on the
USB routing work in upstream PR #37; the ten-channel build is retained.

TAPE is a separate auxiliary lane; all six instrument tracks remain available.
Open MENU > TAPE, then OCT+ to enable capture. FX > TAPE SEND sets how much of
each selected track enters the buffer, independently of its main LEVEL.
Capture contains track processing, pan and spread, but no shared effects or
TAPE feedback. White keys trigger positions in captured history. Release the
keys to resume capture when the TAPE sequence is off. Held playback freezes
capture; there is no overdub. OCT- clears the recording.

Four main knobs: PITCH / SPEED / LENGTH / MIX. SELECT moves to FILTER and LFO.
PRESETS selects a sequence step; SAVE toggles a trigger. ALGORITHM chooses
16/32/48/64 steps. REC enables/disables the TAPE sequence, using internal clock.
An empty step returns to capture. TAPE shares the existing stutter storage and
respects its ownership; it cannot promise independent concurrent stutter use.

The loop is approximately 2.09 seconds, stereo IMA ADPCM at 22.05 kHz. This is
a creative rolling buffer, not a full-song audio recorder. USB routing offers
TAPE L, TAPE R and TAPE mono after playback filter/LFO and its mix contribution.

See [playback effects](tape-playback-fx.md) and
[project saving](tape-project-saving.md) for controls, persistence and limits.
Run `sh tests/run_tape_tests.sh` after generating headers to check DSP, UI,
metadata and simulated flash. No test script flashes firmware.

User reports: filter/LFO playback sounds stable on the FM-1, and project
settings restore correctly. Target CPU headroom and exhaustive USB/arrangement
stress testing are not established by those reports. Three host CPU budget
checks remain flagged; see project-saving documentation. Golden renders remain
unchanged. Preserve the tested firmware as a rollback option.
