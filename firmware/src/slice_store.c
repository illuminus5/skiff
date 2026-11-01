/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE's MAN slices in flash, kept with the sample they were set on (based on hugelton/Felucca#27 by andreahaku).
 *
 * Where: in the user slot itself, a record at the start of the slot's last sector (slot + SLC_REC_OFF). The slot
 * header has no room (its 32 spare bytes hold 8 slice starts at most, and NOR could write them only once), and every
 * free sector of the map is taken (FM6's bank has 0x9F000 / 0xFE000). An upload writes the sample from the slot's
 * start, so its last sector is free unless the sample fills it: up to 77,312 bytes of ADPCM (about 7.0 s at the
 * 22.05 kHz the editor uploads at; a slot holds 7.4 s). A longer sample keeps its slices until the power goes
 * ("TOO LONG TO SAVE").
 * Record: magic, the material (the slot's sample length and its data CRC: the header's), the starts and the end,
 * a CRC over all of it. At each scan of a valid slot (boot, an upload's end: slc_man_load) a record that matches
 * the slot's material and passes slc_man_restore (sorted, SLC_MIN apart, in range) is put in use; anything else
 * (an erased sector, an older sample's record, the end of a longer sample's data) is ignored. An upload erases each
 * sector it writes into, so a long sample overwrites the record; uploading the same sample again brings its slices
 * back. One copy: a save torn by a power cut loses the slices (the CRC fails), never the sample.
 * Saving: the SLICES page marks the slot (slc_man_save, eng_slice.c) when one leaves it after an edit; slc_store_poll
 * writes when the transport is stopped (no erase while playing, as settings_poll), only if the flash holds something
 * else ("SLICES SAVED").
 * (Skiff, #21) The same sector keeps the slot's sounds' LEVEL, REV and DLY (eng_sample.c usr_mix) in a second record
 * (smx_rec_t, below), with the same rules: the writer below writes both, so a save of either keeps the other.
 * Flash access through storage.c's hooks (st_erase / st_prog); the host tests give their own. */
#define SLC_REC_OFF (SMP_USER_SIZE - 0x1000u)        /* the slot's last sector */
#define SLC_REC_MAGIC 0x314D4C53u                    /* "SLM1" */
typedef struct {
    uint32_t magic;
    uint32_t len, crc;                               /* the material: samples, the slot header's data CRC */
    uint32_t n, end;                                 /* slices (1..SLC_AUTO), where the last ends (0 = the material's end) */
    uint32_t pos[SLC_AUTO];
    uint32_t rcrc;                                   /* st_crc32 of everything above */
} slc_rec_t;
_Static_assert(sizeof(slc_rec_t) == 152u, "SLICE record layout");
/* (Skiff, #21) the sounds' LEVEL, REV and DLY (eng_sample.c usr_mix), right after SLICE's record, with a check that
 * ties them to the slot's header (the same sample under the same keys) and finds a torn or erased record; read at a
 * scan (smx_load), written with SLICE's record (slc_tail_t: one erase, one write) when either changed */
#define SMX_REC_MAGIC 0x31584D53u                    /* "SMX1" */
typedef struct {
    int16_t mix[3][16];                              /* usr_mix[k] */
    uint32_t chk;                                    /* smx_chk */
} smx_rec_t;
#if FELUCCA_VOCAL_TRIM
#define VTR_REC_MAGIC 0x31525456u /* VTR1 */
typedef struct {uint32_t magic,header_crc,edge[16][4],crc;} vtrim_rec_t;
_Static_assert(sizeof(vtrim_rec_t)==268u,"VOCAL trim record layout");
static void vtrim_rec_make(uint32_t k,vtrim_rec_t*r){
 memset(r,0,sizeof *r);r->magic=VTR_REC_MAGIC;r->header_crc=st_crc32(smp_user_xip(k),sizeof(smp_user_hdr_t));
 for(unsigned z=0;z<usr_nz[k];z++){vtrim_data d=vtrim_get(k,z);memcpy(r->edge[z],d.region.edge,sizeof r->edge[z]);}
 r->crc=st_crc32(r,offsetof(vtrim_rec_t,crc));
}
#endif
typedef struct {
    slc_rec_t s;
    smx_rec_t m;
#if FELUCCA_VOCAL_TRIM
    vtrim_rec_t t;
#if FELUCCA_VOCAL_STRETCH
    struct {uint32_t magic,header_crc,flags,crc;} timing;
#if FELUCCA_VOCAL_AUTO
 struct {uint32_t flags,crc;} voicing;
#if FELUCCA_VOCAL_EQ
 struct {uint32_t magic,word[16],crc;} eq;
#endif
#endif
#endif
#endif
} slc_tail_t;
#define SMX_REC_OFF (SLC_REC_OFF + 152u)
_Static_assert(SMX_REC_OFF-SLC_REC_OFF==sizeof(slc_rec_t),"existing mix offset unchanged");
#if FELUCCA_VOCAL_TRIM
_Static_assert(offsetof(slc_tail_t,t)==252u&&sizeof(slc_tail_t)==
#if FELUCCA_VOCAL_STRETCH
536u
#if FELUCCA_VOCAL_AUTO
+8u
#if FELUCCA_VOCAL_EQ
+72u
#endif
#endif
#else
520u
#endif
,"appended trim record preserves old layouts");
#else
_Static_assert(sizeof(slc_tail_t)==252u,"original sample metadata layout");
#endif

/* slot k's sample leaves its last sector free (the record can go there) */
static int slc_rec_room(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    return usr_nz[k] && h->data_len <= SLC_REC_OFF - SMP_USER_DATA;
}

#if FELUCCA_VOCAL_TRIM
static void vtrim_store_load(uint32_t k){
 if(!slc_rec_room(k))return;vtrim_rec_t r;memcpy(&r,smp_user_xip(k)+SLC_REC_OFF+252u,sizeof r);
 if(r.magic!=VTR_REC_MAGIC||r.header_crc!=st_crc32(smp_user_xip(k),sizeof(smp_user_hdr_t))||r.crc!=st_crc32(&r,offsetof(vtrim_rec_t,crc)))return;
 for(unsigned z=0;z<usr_nz[k];z++){vocal_trim_region v=vocal_trim_full(usr_zone[k][z].n);memcpy(v.edge,r.edge[z],sizeof v.edge);if(!vtrim_valid(&v,v.length))return;}
 for(unsigned z=0;z<usr_nz[k];z++){vocal_trim_region v=vocal_trim_full(usr_zone[k][z].n);memcpy(v.edge,r.edge[z],sizeof v.edge);(void)vtrim_put(k,z,&v,0);}
#if FELUCCA_VOCAL_STRETCH
 uint32_t extra[4];memcpy(extra,smp_user_xip(k)+SLC_REC_OFF+520u,sizeof extra);
 if(extra[0]==0x314D5456u&&extra[1]==r.header_crc&&extra[3]==st_crc32(extra,12)&&!(extra[2]&0xffff0000u))for(unsigned z=0;z<usr_nz[k];z++)vtrim_timing_put(k,z,(extra[2]>>z)&1u,0);
#if FELUCCA_VOCAL_AUTO
 memcpy(extra,smp_user_xip(k)+SLC_REC_OFF+536u,8);
 if((extra[0]>>16)==0x4156u&&extra[1]==st_crc32(extra,4))for(unsigned z=0;z<usr_nz[k];z++)vtrim_option_put(k,z,1,(extra[0]>>z)&1u,0);
#if FELUCCA_VOCAL_EQ
 uint32_t eq[18];memcpy(eq,smp_user_xip(k)+SLC_REC_OFF+544u,sizeof eq);
 if(eq[0]==0x31514556u&&eq[17]==st_crc32(eq,68)){
  int valid=1;for(unsigned z=0;z<usr_nz[k];z++)valid&=veq_valid(eq[z+1]);
  if(valid)for(unsigned z=0;z<usr_nz[k];z++)vtrim_mask_put(k,z,VEQ_MASK,eq[z+1],0);
 }
#endif
#endif
#endif
}
#endif
/* what slot k's record should hold now (its MAN slices in use, or none: n = 0) */
static void slc_rec_make(uint32_t k, slc_rec_t *r)
{
    const slc_src_t *s = slc_get(k + 1u);
    const slc_man_t *m = s ? slc_man_of(s) : 0;
    memset(r, 0, sizeof *r);
    r->magic = SLC_REC_MAGIC;
    r->len = s ? s->len : 0u;
    r->crc = ((const smp_user_hdr_t *)smp_user_xip(k))->crc;
    if (m) {
        r->n = m->n;
        r->end = m->end;
        memcpy(r->pos, m->pos, m->n * sizeof m->pos[0]);
    }
    r->rcrc = st_crc32(r, sizeof *r - 4u);
}

/* slc_man_load (eng_slice.c): slot k was read and is valid: its stored slices, if they are its material's */
static void slc_store_load(uint32_t k)
{
    const slc_rec_t *r = (const slc_rec_t *)(smp_user_xip(k) + SLC_REC_OFF);
    const slc_src_t *s = slc_get(k + 1u);
    slc_rec_t c;
    if (!s || !slc_rec_room(k))
        return;
    memcpy(&c, r, sizeof c);                         /* (one read of the flash) */
    if (c.magic != SLC_REC_MAGIC || c.rcrc != st_crc32(&c, sizeof c - 4u) || c.len != s->len ||
        c.crc != ((const smp_user_hdr_t *)smp_user_xip(k))->crc || !c.n)
        return;
    slc_man_restore(k, c.n, c.end, c.pos);
}

/* (#21) the mix record's check: "SMX1" ^ the CRC of its values ^ the CRC of slot k's header (480 bytes) */
static uint32_t smx_chk(uint32_t k, const smx_rec_t *r)
{
    return SMX_REC_MAGIC ^ st_crc32(r->mix, sizeof r->mix) ^ st_crc32(smp_user_xip(k), sizeof(smp_user_hdr_t));
}

/* (#21) what slot k's mix record should hold now */
static void smx_rec_make(uint32_t k, smx_rec_t *r)
{
    memcpy(r->mix, usr_mix[k], sizeof r->mix);
    r->chk = smx_chk(k, r);
}

/* (#21) smx_load (eng_sample.c smp_user_scan): slot k was read and is valid: its stored mix, if it is its header's */
static void smx_store_load(uint32_t k)
{
    smx_rec_t c;
    if (!slc_rec_room(k))                            /* (its last sector is the sample's) */
        return;
    memcpy(&c, smp_user_xip(k) + SMX_REC_OFF, sizeof c);
    if (c.chk == smx_chk(k, &c))                     /* (a value out of 0..127: LEVEL full, the sends capped: dsp.c */
        memcpy(usr_mix[k], c.mix, sizeof c.mix);     /* vmod_level, fx.c smx_render) */
}

/* write slot k's last sector (SLICE's record, (#21) the mix's) if the flash holds something else: 0 nothing to do,
 * 1 no room, 2 flash error, 3 written */
static int slc_store_write(uint32_t k)
{
    slc_tail_t r;
    uint32_t off = SMP_USER_BASE + k * SMP_USER_SIZE + SLC_REC_OFF;
    if (!usr_nz[k])                                  /* emptied or being uploaded meanwhile: nothing to keep */
        return 0;
    if (!slc_rec_room(k))
        return 1;
    slc_rec_make(k, &r.s);
    smx_rec_make(k, &r.m);
#if FELUCCA_VOCAL_TRIM
    vtrim_rec_make(k,&r.t);
#if FELUCCA_VOCAL_STRETCH
    memset(&r.timing,0,sizeof r.timing);r.timing.magic=0x314D5456u;r.timing.header_crc=r.t.header_crc;
    for(unsigned z=0;z<usr_nz[k];z++)r.timing.flags|=((vtrim_flags(k,z)&1u)!=0)<<z;
    r.timing.crc=st_crc32(&r.timing,12);
#if FELUCCA_VOCAL_AUTO
    r.voicing.flags=0x41560000u;
    for(unsigned z=0;z<usr_nz[k];z++)r.voicing.flags|=((vtrim_flags(k,z)&2u)!=0)<<z;
    r.voicing.crc=st_crc32(&r.voicing,4);
#if FELUCCA_VOCAL_EQ
    memset(&r.eq,0,sizeof r.eq);r.eq.magic=0x31514556u;
    for(unsigned z=0;z<usr_nz[k];z++)r.eq.word[z]=vtrim_flags(k,z)&VEQ_MASK;
    r.eq.crc=st_crc32(&r.eq,68);
#endif
#endif
#endif
#endif
    if (!memcmp(&r, smp_user_xip(k) + SLC_REC_OFF, sizeof r))
        return 0;                                    /* unchanged: no erase cycle */
    if (st_erase(off) || st_prog(off, &r, sizeof r))
        return 2;
    return 3;
}

/* main loop: the slices and (#21) the sounds' mix asked for, once the transport stops */
static void slc_store_poll(void)
{
    uint32_t k, m = slc_man_save | smx_save;
    int rc = 0, w = 0;
#if FELUCCA_VOCAL_TRIM
    uint32_t tm=vtrim_save;m|=tm;
    if(vtrim_page_on()
#if FELUCCA_VOCAL_EQ
       ||veq_page_on()
#endif
       ||(tm&&voices_busy()))return;
#endif
    if (!m || transport_busy() || slice_page_on() || smx_page_on())   /* (written once the page is left) */
        return;
    for (k = 0; k < SMP_USER_SLOTS; k++)
        if ((m >> k) & 1u) {
            int r = flash_ok ? slc_store_write(k) : 0;
#if FELUCCA_VOCAL_TRIM
            if(!flash_ok&&tm)r=2;
#endif
            w |= r == 3;
            rc = r != 3 && r > rc ? r : rc;
        }
    if (rc)
        ui_message(rc == 1 ? "TOO LONG TO SAVE" : "SAVE ERROR");
    else if (w)
        ui_message(
#if FELUCCA_VOCAL_TRIM
            tm ? "TRIM SAVED" :
#endif
            slc_man_save ? "SLICES SAVED" : "MIX SAVED");
    slc_man_save = smx_save = 0;
#if FELUCCA_VOCAL_TRIM
    vtrim_save=0;
#endif
}

static void slc_store_boot(void)                     /* persist_boot: before the slot scans */
{
    slc_man_load = slc_store_load;
    smx_load = smx_store_load;
#if FELUCCA_VOCAL_TRIM
    vtrim_load=vtrim_store_load;
#endif
}
