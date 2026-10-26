/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "usb_audio_config.h"
#ifndef FELUCCA_USB_ROUTING
#define FELUCCA_USB_ROUTING 1
#endif
/* Stable source IDs: OFF, master L/R/mono, then each track's L/R/mono. */
#ifndef FELUCCA_TAPE
#define FELUCCA_TAPE 0
#endif
/* Source count is also the UI choice count. Persisted IDs 25/26 stay retired. */
#define USB_SOURCE_N (FELUCCA_USB_6CH ? (FELUCCA_TAPE ? 28u : 25u) : 22u)
#define USB_TAPE_L 27u
#define USB_TAPE_R 28u
#define USB_TAPE_MONO 29u
static int usb_source_valid(uint32_t s)
{
    return s < (FELUCCA_USB_6CH ? 25u : 22u)
#if FELUCCA_TAPE && FELUCCA_USB_6CH
        || (s>=USB_TAPE_L && s<=USB_TAPE_MONO)
#endif
        ;
}
static uint32_t usb_source_choice(uint32_t s)
{
#if FELUCCA_TAPE && FELUCCA_USB_6CH
    if(s>=USB_TAPE_L && s<=USB_TAPE_MONO)return s-2u;
#endif
    return usb_source_valid(s)?s:0u;
}
static uint32_t usb_choice_source(uint32_t choice)
{
    if(choice>=USB_SOURCE_N)return 0xFFFFFFFFu;
#if FELUCCA_TAPE && FELUCCA_USB_6CH
    if(choice>=25u)return choice+2u;
#endif
    return choice;
}
#define USB_ROUTES_DEFAULT 0x0201u
static volatile uint32_t usb_routes = USB_ROUTES_DEFAULT;
static uint32_t usb_routes_valid(uint32_t routes)
{
    uint32_t l = routes & 255u, r = routes >> 8 & 255u;
    return (l < 22u ? l : 1u) | (r < 22u ? r : 2u) << 8;
}
static uint32_t usb_route_get(uint32_t slot) { return usb_routes >> (slot * 8u) & 255u; }
/* Main-loop writers only. One aligned word store; audio snapshots once/block. */
static void usb_route_put(uint32_t slot, uint32_t source)
{
    if (slot < 2u && source < 22u)
        usb_routes = (usb_routes & ~(255u << (slot * 8u))) | source << (slot * 8u);
}
static int32_t usb_route_mono(int32_t l, int32_t r)
{
    return (int32_t)(((int64_t)l + r) / 2);
}

#if FELUCCA_USB_6CH
/* Six five-bit source IDs in one atomic snapshot/store. */
#define USB_SIX_DEFAULT (6u | 9u<<5 | 12u<<10 | 15u<<15 | 18u<<20 | 21u<<25)
static volatile uint32_t usb_six_routes = USB_SIX_DEFAULT;
/* Slots 7..10: REV, CHOR, DELAY, OFF. Retain all four when building eight channels. */
#define USB_EXTRA_DEFAULT (22u | 24u<<5 | 23u<<10)
static volatile uint32_t usb_extra_routes=USB_EXTRA_DEFAULT;
static uint32_t usb_extra_valid(uint32_t routes)
{
    uint32_t result=0;
    for(uint32_t k=0;k<4;k++) {
        uint32_t s=(routes>>(5u*k))&31u;
        if(!usb_source_valid(s))s=0;
        result|=s<<(5u*k);
    }
    return result;
}
static uint32_t usb_map_get(uint32_t first,uint32_t extra,uint32_t slot)
{
    return slot<6u?(first>>(slot*5u))&31u:(extra>>((slot-6u)*5u))&31u;
}
static uint32_t usb_six_valid(uint32_t routes)
{
    uint32_t result=0;
    for (uint32_t k=0;k<6;k++) {
        uint32_t source=(routes>>(5u*k))&31u;
        if (!usb_source_valid(source)) source=0;
        result|=source<<(5u*k);
    }
    return result;
}
static uint32_t usb_six_get(uint32_t slot) { return usb_map_get(usb_six_routes,usb_extra_routes,slot); }
static void usb_six_put(uint32_t slot, uint32_t source)
{
    if(slot>=UAC_CHANNELS || !usb_source_valid(source))return;
    if(slot<6u)usb_six_routes=(usb_six_routes & ~(31u<<(5u*slot))) | source<<(5u*slot);
    else {slot-=6u;usb_extra_routes=(usb_extra_routes & ~(31u<<(5u*slot))) | source<<(5u*slot);}
}
#endif

#if FELUCCA_USB_6CH
/* Retired PER7 metadata, retained only for settings/backup compatibility.
 * No compact engine or click routing uses these fields. */
#define USB_COMPACT_DEFAULT 0u
#define USB_CF_TRACK_DEFAULT (125u | 64u<<8 | 64u<<16 | 64u<<24)
static volatile uint32_t usb_compact_config=USB_COMPACT_DEFAULT;
static volatile uint32_t usb_cf_track[6]={USB_CF_TRACK_DEFAULT,USB_CF_TRACK_DEFAULT,USB_CF_TRACK_DEFAULT,USB_CF_TRACK_DEFAULT,USB_CF_TRACK_DEFAULT,USB_CF_TRACK_DEFAULT};
static uint32_t usb_cf_edit;
static uint32_t usb_compact_valid(uint32_t v) { return v&3u; }
static uint32_t usb_cf_track_valid(uint32_t v)
{
    uint32_t result=0;
    for(uint32_t k=0;k<4;k++) {
        uint32_t x=(v>>(8u*k))&255u;
        if(!k) {if(x<10u || x>185u)x=125u;} else if(x>127u)x=64u;
        result|=x<<(8u*k);
    }
    return result;
}
#endif
