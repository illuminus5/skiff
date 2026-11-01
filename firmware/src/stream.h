/* SPDX-License-Identifier: GPL-3.0-only */
/* Integer-only, fixed-storage streaming feasibility core; 22050 Hz mono.
 * Experimental: not connected to firmware or an interrupt. */
#ifndef VOCAL_STREAM_H
#define VOCAL_STREAM_H
#include <stdint.h>
#include <stddef.h>
#define VS_RING 3072u
#define VS_DELAY 1024u
#define VS_HOP 256u
#define VS_EPOCHS 64u
#define VS_GRAINS 12u
#define VS_LANES 2u
#define VS_HISTORY 16u
typedef struct {uint32_t at,period;uint16_t note,voiced;} vs_analysis;
typedef struct {uint32_t at;uint16_t period,reserved;} vs_epoch;
typedef struct {int32_t source;uint32_t phase,step;uint16_t age,length;} vs_grain;
typedef struct {vs_grain grains[VS_LANES][VS_GRAINS];uint32_t countdown[VS_LANES],period[VS_LANES],phase[2];uint16_t live[VS_LANES];int32_t wet;} vs_note;
typedef struct {
 int16_t ring[VS_RING],decimated[512];uint16_t cm[201];
 vs_epoch epochs[VS_EPOCHS];vs_analysis history[VS_HISTORY];
 vs_analysis render_analysis;uint32_t render_bucket;uint16_t harmony_note;
 uint32_t samples,ne,nh,next_epoch,ring_pos;
 uint32_t analysis_at,analysis_total,analysis_lag,analysis_valid,max_analysis_products;
 uint64_t products,grain_samples,normalizations;
 int32_t held_note;uint32_t dropped_grains,missing_epochs;
 uint8_t root,minor;
} vs_state;
void vs_init(vs_state*s,unsigned root,unsigned minor);
void vs_feed(vs_state*s,int16_t input);
void vs_note_init(vs_note*n);
void vs_note_render(vs_state*s,vs_note*n,int32_t transpose_q8,unsigned mode,int16_t out[VS_LANES]);
#endif
