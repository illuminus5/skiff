/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#ifndef FELUCCA_USB_12CH
#define FELUCCA_USB_12CH 0
#endif
#ifndef FELUCCA_USB_6CH
#define FELUCCA_USB_6CH 0
#endif
#if FELUCCA_USB_12CH && FELUCCA_USB_6CH
#error "Choose either six-channel or twelve-channel USB"
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
#elif FELUCCA_USB_12CH
#undef FELUCCA_USB_ROUTING
#define FELUCCA_USB_ROUTING 1
/* Experimental fixed T1 L/R .. T6 L/R capture, not a host audio return. */
#undef FELUCCA_UAC_MAC_COMPAT
#define FELUCCA_UAC_MAC_COMPAT 1
#undef FELUCCA_UAC_48K
#define FELUCCA_UAC_48K 0
#define UAC_CHANNELS 12u
#define UAC_WORDS 6u
#else
#define UAC_CHANNELS 2u
#define UAC_WORDS 1u
#endif
