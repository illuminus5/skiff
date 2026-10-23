/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#ifndef FELUCCA_DISPLAY_MIRROR
#define FELUCCA_DISPLAY_MIRROR 0
#endif
#ifndef FELUCCA_DISPLAY_BULK
#define FELUCCA_DISPLAY_BULK 0
#endif
#if FELUCCA_DISPLAY_BULK && (!FELUCCA_DISPLAY_MIRROR || !FELUCCA_CDC)
#error "Bulk display requires the mirror and CDC"
#endif
#if FELUCCA_DISPLAY_MIRROR
#if !FELUCCA_OTA || BOARD_SMK37
#error "Display mirror prototype requires the FM-1 editor transport"
#endif
#if FELUCCA_UAC
#error "Display mirror currently requires FELUCCA_UAC=0; retain the separate USB audio firmware"
#endif
/* Display-only profile: the removed USB audio ring frees normal BSS RAM.
 * Do not consume the pool reserve, debug RAM, stacks or persistent buffers. */
#if FELUCCA_DISPLAY_BULK
#define DM_CAPACITY 16000u
#else
#define DM_CAPACITY 16320u
#endif
#define DM_QUEUE
static uint32_t dm_now_ms(void);
#define DM_NOW() dm_now_ms()
#include "../../fm1-display-mirror/firmware/skiff/capture.h"
#define DISPLAY_MIRROR_FILL(x,y,w,h,c) dm_fill(x,y,w,h,c)
#define DISPLAY_MIRROR_BLIT(x,y,w,h,p) dm_blit(x,y,w,h,p)
#else
#define DISPLAY_MIRROR_FILL(x,y,w,h,c) ((void)0)
#define DISPLAY_MIRROR_BLIT(x,y,w,h,p) ((void)0)
#endif
