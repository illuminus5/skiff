/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#ifndef FELUCCA_USB_6CH
#define FELUCCA_USB_6CH 0
#endif
#if FELUCCA_USB_6CH
#undef FELUCCA_UAC_MAC_COMPAT
#define FELUCCA_UAC_MAC_COMPAT 1
#undef FELUCCA_UAC_48K
#define FELUCCA_UAC_48K 0
#undef FELUCCA_USB_ROUTING
#define FELUCCA_USB_ROUTING 1
#ifndef FELUCCA_USB_CHANNELS
#define FELUCCA_USB_CHANNELS 6
#endif
#if FELUCCA_USB_CHANNELS != 6 && FELUCCA_USB_CHANNELS != 8 && FELUCCA_USB_CHANNELS != 10
#error "Mapped USB supports 6, 8 or 10 channels"
#endif
#define UAC_CHANNELS FELUCCA_USB_CHANNELS
#define UAC_WORDS (UAC_CHANNELS/2u)
#else
#define UAC_CHANNELS 2u
#define UAC_WORDS 1u
#endif

/* Ten-channel capture: EP4 can occupy roughly 600 us on the wire.
 * Service it on each 100-us TIMER5 tick; keep MIDI/control polling at 2 kHz.
 * Stereo/six/eight retain their cadence. */
#define UAC_SERVICE_DIV ((FELUCCA_USB_6CH && UAC_CHANNELS == 10) ? 1u : 5u)
#define UAC_STALL_POLLS (200u / UAC_SERVICE_DIV) /* 20 ms, independent of cadence */
