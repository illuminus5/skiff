/* SPDX-License-Identifier: GPL-3.0-only
 * One-track sample choir: five pitched carriers, up to sixteen virtual singers. */
#define H_GROUPS 5u
#define H_SINGERS 16u
#define H_RING 236u
_Static_assert(FELUCCA_VOCAL_STRETCH&&VS_LANES==2&&VS_GRAINS>=12,"choir requires an unused second grain lane");
#define H_FADE 256
_Static_assert(H_FADE%4==0,"section crossfade counter advances in steps of four");
_Static_assert(24*128-(32768*127>>11)>64&&(24+194)*128+(32768*127>>11)<(H_RING-2)*128,"fractional delay bounds include full HUMAN motion");
_Static_assert(32768u*200u*((1u<<18)/800u)<=INT32_MAX,"choir normalization multiply must fit int32");
static const char*const H_NAMES[]={"LEAD","BASS","TENOR","ALTO","SOPRANO"};
static const int8_t H_DEFAULTS[3][5]={{1,2,3,6,4},{96,85,90,100,90},{0,0,-20,20,0}};
#define H_COUNTS H_DEFAULTS[0]
#define H_LEVELS H_DEFAULTS[1]
#define H_PANS H_DEFAULTS[2]
static const uint8_t H_SPREAD[5]={0,32,70,85,95};
static const uint16_t H_REF[6]={0,21627,11487,9242,7945,7077};
typedef struct {
 vs_state source;vs_note singer[5];voice_t decoder;uint32_t phase[16];uint16_t tap[16];int16_t delay[4][H_RING];int32_t cache[2][CTL];
 vtrim_data trim;int32_t mixed_previous[2],mixed_current[2];
 /* Section current/previous samples live in private voice ph[0:1]/s[0:1].
  * ph[2] packs two bounded Q15 envelope endpoints; bit 31 marks unrendered.
  * This leaves the stereo cache wide without enlarging the shared workspace. */
 uint16_t filter;
 uint32_t generation,slot,zone,signature,ready,half,running,done;int32_t pitch_mod,fine_mod;uint8_t count[5];
} harmony_part_t;
static harmony_part_t*harm(const track_t*t){return (harmony_part_t*)eng_part(t);}
/* Reuse the saved SAMPLE START ADPCM checkpoint; no sample reread/seek. */
static inline __attribute__((always_inline)) void harmony_rewind(harmony_part_t*h){h->decoder.ph[0]=h->trim.region.edge[0];h->decoder.s[0]=(int16_t)h->trim.state[0];h->decoder.s[1]=(h->trim.state[0]>>16)&127u;h->done=0;}
/* Playback is live until completion; current key state gates each boundary. */
static __attribute__((noinline,minsize)) int harmony_live(const track_t*t){
 const harmony_part_t*h=harm(t);if(!(h->running&1u)||(h->done&&(int32_t)(h->source.samples-h->done)>=0))return 0;
 for(unsigned k=0;k<5;k++)if(t->v[k].active&&t->v[k].stage!=4u)return 1;return 0;
}
static __attribute__((noinline,minsize)) int harmony_on(const track_t*t){return t==&trk[0]&&t->engine==ENGI_HARMONY;}
static __attribute__((noinline,minsize)) int harmony_req(const track_t*t){return t==&trk[0]&&t->eng_req==ENGI_HARMONY;}
_Static_assert(ENGI_HARMONY==ENGI_VOCAL+1,"vocal family IDs must remain contiguous");
static int vocal_family(const track_t*t){return harmony_on(t);}
static __attribute__((noinline,minsize)) int vocal_family_req(const track_t*t){return harmony_req(t);}
static __attribute__((noinline,minsize)) unsigned harmony_count(const track_t*t,unsigned g){unsigned sum=0;for(unsigned j=0;j<5;j++){int n=t->p[P_LDECAY0+j];if(n<0||n>(j?15:1))return 0;sum+=(unsigned)n;}return sum<=H_SINGERS?(unsigned)t->p[P_LDECAY0+g]:0;}
static __attribute__((noinline,minsize)) void harmony_defaults(int16_t*p){p[P_SPRD]=127;for(unsigned g=0;g<5;g++){p[P_LDECAY0+g]=H_COUNTS[g];p[P_LN0+g]=H_LEVELS[g];p[P_LPAN0+g]=H_PANS[g];}}
static __attribute__((noinline,minsize)) void harmony_key_on(track_t*,uint32_t,uint32_t);
static __attribute__((noinline,minsize)) void harmony_key_off(track_t*,uint32_t);
static __attribute__((noinline,minsize)) void harmony_voice_on(track_t*t,voice_t*v){
 int32_t l=v->s[2],r=v->s[3];for(unsigned i=0;i<8;i++)v->s[i]=0;v->ph[0]=v->ph[1]=0;
 if(v->env_out){v->s[2]=l;v->s[3]=r;v->s[5]=H_FADE;}
 (void)t;
}
static __attribute__((noinline,minsize)) uint32_t harmony_signature(const track_t*t){uint32_t s=17,source,zone;for(unsigned g=0;g<5;g++)s=s*33+(uint32_t)t->p[P_LDECAY0+g];if(vocal_selection(t,&source,&zone)){vtrim_data d=vtrim_get(source-SMP_NSETS,zone);for(unsigned k=0;k<4;k++)s=s*33+d.region.edge[k];}return s;}
static __attribute__((noinline,minsize)) void harmony_start(track_t*,uint32_t,uint32_t);
static __attribute__((noinline,minsize)) void harmony_block(track_t*t){
 harmony_part_t*h=harm(t);uint32_t source,zone;unsigned last=0;
 for(unsigned k=0;k<5;k++){t->v[k].ph[2]=0x80000000u;if(t->v[k].active)last=k+1;}
 if(!last){h->running=0;h->ready=0;return;}
 if(vocal_selection(t,&source,&zone)&&(!(h->running&1u)||h->slot!=source-SMP_NSETS||h->zone!=zone||h->generation!=smp_user_gen))harmony_start(t,source-SMP_NSETS,zone);
 if(h->decoder.ph[2]!=(unsigned)t->p[P_E1]){h->decoder.ph[2]=(unsigned)t->p[P_E1];for(unsigned k=0;k<5;k++)if(t->v[k].active)t->v[k].s[5]=H_FADE;}
 h->ready=last|0x80000000u;
}
/* Additional controls occupy the three unused section-pan slots; zero keeps
 * old sounds byte-for-byte on the original harmony path. No storage layout change. */
#define H_VOICING P_LPAN5
#define H_CHORD P_LPAN6
#define H_TUNE P_LPAN7
static int harmony_source(unsigned source){
 if(source<SMP_NSETS||source>=SMP_NALL)return 0;
 unsigned slot=source-SMP_NSETS,n=usr_nz[slot];if(!n)return 0;
 /* Existing format has no instrument tag. Reject single-key mapped kits,
  * not broad-range vocal phrase banks. Audio content itself is not classified. */
 for(unsigned i=0;i<n;i++)if(usr_zone[slot][i].lo!=usr_zone[slot][i].hi)return 1;
 return 0;
}
static __attribute__((noinline,minsize)) int harmony_offset(const track_t*t,int note,unsigned g){
 if(!g)return 0;
 int root=t->p[P_E2]%12,minor=t->p[P_E2]>=12,n=note;
 if(t->p[H_VOICING]==1){
  int degree=clamp(t->p[H_CHORD],0,6),tones[3],pc=root;
  for(int i=0;i<degree;i++)do{pc++;}while(!allowed(pc,root,minor));
  for(unsigned i=0;i<3;i++){tones[i]=pc%12;for(unsigned j=0;j<2;j++)do{pc++;}while(!allowed(pc,root,minor));}
  static const uint8_t lo[4]={36,48,55,60},hi[4]={55,64,72,79},centre[4]={48,55,64,72};
  vs_note*v=&harm(t)->singer[g];int previous=v->period[1]?(int)v->period[1]-1:centre[g-1],best=previous,error=999;
  for(int p=lo[g-1];p<=hi[g-1];p++)for(unsigned k=0;k<3;k++)if(p%12==tones[k]){
   int d=p-previous;if(d<0)d=-d;if(d<error){error=d;best=p;}
  }
  v->period[1]=(unsigned)(best+1);return best-note;
 }
 if(g==1)return -12;
 if(!allowed(n,root,minor))n+=allowed(n-1,root,minor)?-1:1;
 for(unsigned i=0;i<(g-1)*2;i++){do{n++;}while(!allowed(n,root,minor));}return n-note;
}
static __attribute__((noinline,minsize)) void harmony_start(track_t*t,uint32_t slot,uint32_t zone){
 harmony_part_t*h=harm(t);int32_t held[2]={0,0};
 for(unsigned k=0;k<5;k++){voice_t*v=&t->v[k];if(v->active&&v->env_out){held[0]=h->cache[0][CTL-1];held[1]=h->cache[1][CTL-1];v->s[5]=H_FADE;}v->ph[0]=v->ph[1]=0;v->s[0]=v->s[1]=0;}
 memset(h,0,sizeof *h);for(unsigned ch=0;ch<2;ch++){h->mixed_previous[ch]=h->mixed_current[ch]=held[ch];}
 h->slot=slot;h->zone=zone;h->generation=smp_user_gen;h->trim=vtrim_get(slot,zone);h->signature=harmony_signature(t);for(unsigned g=0;g<5;g++)h->count[g]=(uint8_t)harmony_count(t,g);
 /* The workspace was already cleared: avoid clearing the decoder and all
  * singer states a second time on the audio thread. */
 h->source.held_note=-1;h->source.render_bucket=UINT32_MAX;h->source.root=(unsigned)t->p[P_E2]%12;h->source.minor=t->p[P_E2]>=12;
 h->singer[0].countdown[1]=4096;for(unsigned ch=0;ch<2;ch++)for(unsigned k=0;k<3;k++)h->singer[0].grains[1][ch*3+k].source=held[ch];
 h->decoder.ph[2]=(unsigned)t->p[P_E1];harmony_rewind(h);h->running=1u;
}
/* 440 cached samples occupy only never-rendered lane-1 grain fields.
 * Keep lead source[0..7] (cleanup/detector) and other source[0..2] (filters).
 * No active lane-0 grain or outer voice-metadata fields are reused. Two windows allow release. */
#define H_LOOP_FADE 220u
_Static_assert(sizeof(vs_grain)==16u,"loop cache field layout");
static __attribute__((noinline,minsize)) void*harmony_loop_pcm(harmony_part_t*h,unsigned i){
 unsigned g,k;
 if(i<288u){g=1u+i/72u;return (unsigned char*)&h->singer[g].grains[1][3]+2u*(i%72u);}
 i-=288u;if(i<32u)return (unsigned char*)&h->singer[0].grains[1][8]+2u*i;
 i-=32u;if(i<48u){g=0;k=i/6u;}else{i-=48u;g=1u+i/18u;k=(i%18u)/6u;}
 return (unsigned char*)&h->singer[g].grains[1][k]+4u+2u*(i%6u);
}
/* Interpolated raised-cosine, complementary Q15 weights: exact endpoints,
 * zero endpoint slope, and unity summed gain rather than a gain bump. */
static __attribute__((noinline,minsize)) int harmony_loop_weight(unsigned n,unsigned w){
 unsigned phase=(n<<15)/w,k=phase>>8,f=phase&255u;
 return VS_HANN[k]+(((int)VS_HANN[k+1]-VS_HANN[k])*(int)f>>8)+(int)(phase>>15);
}
/* Primary decoder always follows the outgoing phrase. Cache the loop head
 * during that first pass, including a second window for smooth cancellation.
 * s[5:6] is the exact ADPCM checkpoint after one window; s[7] is blend weight. */
static __attribute__((noinline,minsize)) int harmony_input(harmony_part_t*h,const smp_zone_t*z,int loop,unsigned w){
 voice_t*d=&h->decoder;unsigned ls=h->trim.region.edge[1],le=h->trim.region.edge[2],end=h->trim.region.edge[3];
 if(loop&&d->ph[0]>=le&&d->s[7]==(int)w&&w){d->ph[0]=ls+w;d->s[0]=d->s[5];d->s[1]=d->s[6];d->s[7]=0;}
 if(loop&&d->ph[0]>=end&&!d->s[7]){harmony_rewind(h);h->running=1u;}
 unsigned pos=d->ph[0];
 int16_t raw=0;
 if(pos<end){raw=(int16_t)sample_next(z,d,0);unsigned fade=pos-h->trim.region.edge[0];if(end-d->ph[0]<fade)fade=end-d->ph[0];if(fade<64u)raw=(int16_t)(raw*(int)fade/64);
  unsigned at=pos-ls;if(at<2u*w){memcpy(harmony_loop_pcm(h,at),&raw,2);if(at+1u==w){d->s[5]=d->s[0];d->s[6]=d->s[1];}}
 }else if(!d->s[7]){if(!h->done)h->done=h->source.samples+VS_DELAY+2048u;return 0;}
 /* Prepare the boundary smoothly even during a brief key-up. Only the
  * current held-key state decides whether the decoder actually wraps. */
 if(w&&pos>=le-w&&pos<le){if(!d->s[7])d->ph[1]=0;d->s[7]++;}
 else if(d->s[7]>0)d->s[7]--;
 int input=raw;
 if(d->s[7]>0){int16_t head;memcpy(&head,harmony_loop_pcm(h,d->ph[1]++),2);int q=harmony_loop_weight((unsigned)d->s[7],w);input=vs_shift0(raw*(32768-q)+head*q,15);}
 return input;
}
static __attribute__((noinline,minsize)) void harmony_output(harmony_part_t*h,unsigned i,int correction){
 vs_note*meter=&h->singer[0];
  /* Fixed two-pole cleanup precedes detection. User cutoff follows gain, so
   * level stabilization never counteracts a deliberate filter/EQ change.
   * An extra detector-only shelf reduces low-frequency weighting. This is a
   * cheap loudness approximation, not a calibrated BS.1770 meter. Raw stereo
   * magnitude still drives the independent safety ceiling. Lane 1 never runs. */
  int absolute=0,weighted=0;
  for(unsigned ch=0;ch<2;ch++){int a=h->mixed_previous[ch],x=a+((h->mixed_current[ch]-a)*(int)((h->half+2u)&3u)>>2);int32_t*p0=&h->singer[0].grains[1][ch*3].source,*p1=&h->singer[0].grains[1][ch*3+1].source;*p0+=(x-*p0)>>1;*p1+=(*p0-*p1)>>1;absolute+=*p1<0?-*p1:*p1;int32_t*low=&meter->grains[1][6+ch].source;*low+=(*p1-*low)>>3;int y=*p1-((*low*3)>>3);weighted+=y<0?-y:y;}
  int envelope[2]={(int)meter->period[1],weighted};for(unsigned k=0;k<2;k++)meter->phase[k]+=((envelope[k]<<8)-(int32_t)meter->phase[k])>>12;
  int g=correction+((((int)meter->countdown[1]-correction)*(int)i)>>CTL_LOG2);if(absolute*g>30000*4096)g=30000*4096/absolute;
  for(unsigned ch=0;ch<2;ch++){int32_t*lp=&h->singer[0].grains[1][ch*3+2].source;*lp+=mulq15((h->singer[0].grains[1][ch*3+1].source*g>>12)-*lp,h->filter);h->cache[ch][i]=*lp;}
}
static __attribute__((noinline,minsize)) void harmony_prepare(track_t*t,uint32_t n){
 harmony_part_t*h=harm(t);uint32_t source,zone;
 if(!vocal_selection(t,&source,&zone)||!harmony_source(source)||usr_zone[source-SMP_NSETS][zone].rate!=32768u||!h->trim.region.edge[3]){h->running=0;h->done=1;for(unsigned k=0;k<5;k++)if(t->v[k].active){t->v[k].gate=0;t->v[k].stage=3;}memset(h->cache,0,sizeof h->cache);h->ready=1;return;}
 const smp_zone_t*z=&usr_zone[source-SMP_NSETS][zone];int loop=t->nmono!=0,human=clamp(t->p[P_E5],0,127);unsigned w=(h->trim.region.edge[2]-h->trim.region.edge[1])/2u;if(w>H_LOOP_FADE)w=H_LOOP_FADE;
 int level_sum=0;unsigned groups=0;for(unsigned g=0;g<5;g++)if(h->count[g]){groups++;level_sum+=clamp(t->p[P_LN0+g],0,127)*100/127;}
 /* Common, bounded register compensation follows the rendered key/glide and
  * ROOT. A one-octave dead band leaves the middle register unchanged; the
  * outer octave adds at most 1.49 dB. The existing gain ramp handles changes.
  * ready is the one-based final section slot until prepare completes. */
 int delta_pitch=t->v[h->ready-1u].pitch_cur+h->pitch_mod-t->p[P_E1]*16;int distance=delta_pitch<0?-delta_pitch:delta_pitch;
 int reference_scale=H_REF[groups];reference_scale+=(reference_scale*clamp(distance-192,0,192))>>10;
 int divisor=level_sum*3;if(divisor<800)divisor=800;int mix_scale=(1<<18)/divisor;
 voice_t*section[5]={0};int pitch_base[5],scale[5];uint16_t target_tap[16],start[16];int16_t spread[16];

 for(unsigned k=0;k<5;k++)if(!(t->v[k].ph[2]&0x80000000u)&&(unsigned)t->v[k].s[4]<5)section[t->v[k].s[4]]=&t->v[k];
 /* Lane-1 phase fields hold smoothed reference/output envelopes (Q8);
  * countdown[1] is the shared level correction (Q12). No lane-1 grains render. */
 vs_note*meter=&h->singer[0];int ref=(int)(meter->phase[0]>>8),actual=(int)(meter->phase[1]>>8),correction=(int)meter->countdown[1],delta=4096-correction;
 /* Quiet expansion cannot boost silence. Unvoiced material returns to unity;
  * voiced feedback is bounded to +6 dB and ramped across the control block. */
 if(ref<64)delta=ref*64-correction;
 else if(h->source.render_analysis.voiced&&actual>32)delta=ref-((actual*correction)>>12);
 if(h->source.samples>VS_DELAY+4410u)meter->countdown[1]=(unsigned)clamp(correction+(delta>>4),0,8192);
 /* Reuse already scheduled source analysis: no additional detector or voices. */
 unsigned root=(unsigned)t->p[P_E2]%12,minor=(unsigned)t->p[P_E2]>=12;
 if(h->source.root!=root||h->source.minor!=minor){h->source.root=root;h->source.minor=minor;h->source.held_note=-1;}
 vs_analysis a=h->source.render_analysis;int correction_q8=0,mode=t->p[H_TUNE];
 if((mode==1||mode==2)&&a.voiced&&a.note>=24&&a.note<=96){
  int32_t target=VS_PERIOD[a.note-24],delta=(int32_t)a.period-target;
  /* Detector periods are bounded; discard seven low bits before the multiply
   * to keep this ratio signed-32-bit without pulling in a 64-bit divider. */
  correction_q8=(delta/128)*4432/(target/128);
  if(mode==1)correction_q8=correction_q8*3/4;
  correction_q8=clamp(correction_q8,-512,512);
 }
 int previous=(int16_t)meter->live[1];
 previous=mode==1?previous+(correction_q8-previous)/8:correction_q8;
 meter->live[1]=(uint16_t)previous;
 unsigned j=0;
 for(unsigned g=0;g<5;g++){
  voice_t*v=section[g];unsigned count=h->count[g];scale[g]=count?8388608/(127*count):0;
  int p=v?v->pitch_cur+h->pitch_mod:0,base=(int)h->source.render_analysis.note+(p>>4)-t->p[P_E1];if(base<24)base=v?v->note:60;
  pitch_base[g]=(p-t->p[P_E1]*16)*16+h->fine_mod+harmony_offset(t,base,g)*256+previous;
  for(unsigned id=0;id<count&&j<H_SINGERS;id++,j++){
   h->phase[j]+=(180000u+j*9337u)*(n/4u);
   int wave=sine_i(h->phase[j]+j*1640531527u);
   target_tap[j]=(uint16_t)((int)(24u+(j*43u+g*19u)%195u)*128+(wave*human>>11));
   if(h->source.samples<4)h->tap[j]=target_tap[j];
   unsigned rank=(id+count/2u)%count;
   spread[j]=(int16_t)(count>1?((int)(2*rank)-(int)count+1)*H_SPREAD[g]*t->p[P_SPRD]/(127*((int)count-1)):0);
   start[j]=(uint16_t)(VS_DELAY+(id?(j*251u%2205u)*(unsigned)human/64u:0));
  }
 }
 for(unsigned i=0;i<n;i++){
  if(!(h->half&1u)){
   int input=harmony_input(h,z,loop,w);
   int filtered=(input+3*(h->decoder.s[2]+h->decoder.s[3])+h->decoder.s[4])>>3;
   h->decoder.s[4]=h->decoder.s[3];h->decoder.s[3]=h->decoder.s[2];h->decoder.s[2]=input;vs_feed_harmony(&h->source,(int16_t)filtered);
   if(h->half&2u){
    unsigned clock=h->source.samples>>1,head=(clock>>1)%H_RING;j=0;int mixed[2]={0,0},reference=0;
    for(unsigned g=0;g<5;g++){
     int32_t l=0,r=0;unsigned count=h->count[g];voice_t*v=section[g];int carrier=0,gain=0,pan=0;
     if(v&&count){
      int16_t lanes[VS_LANES];vs_note_render(&h->source,&h->singer[g],pitch_base[g],7,lanes);carrier=lanes[0];
      if(g){
       /* Lane 1 is never rendered in mode 7. Its first three source
        * fields hold the carrier's decimation filter history. */
       vs_grain*q=h->singer[g].grains[1];int a=q[0].source,b=q[1].source,c=q[2].source;
       int16_t low=(int16_t)((carrier+3*(a+b)+c)>>3);
       q[2].source=b;q[1].source=a;q[0].source=carrier;
       if(!(clock&1u))h->delay[g-1][head]=low;
      }
      int target=clamp(t->p[P_LN0+g],0,127)<<8,d=target-v->s[6];v->s[6]+=d>63||d< -63?d/64:d;gain=v->s[6]*scale[g]>>16;
      /* Parameter bounds are enforced on edit/load; final singer pan is clamped. */
      target=t->p[P_LPAN0+g]*256;d=target-v->s[7];v->s[7]+=d>63||d< -63?d/64:d;pan=v->s[7]*100/(64*256);
     }
     for(unsigned id=0;id<count&&j<H_SINGERS;id++,j++){
      if(!v||h->source.samples<=start[j])continue;
      int x=carrier;
      if(g&&id){
       int d=(int)target_tap[j]-h->tap[j];h->tap[j]=(uint16_t)(h->tap[j]+(d>7||d< -7?d/8:d));
       int tap=(int)h->tap[j]-(int)(clock&1u)*64;
       int at=(int)head-(tap>>7);if(at<0)at+=H_RING;int older=at?at-1:H_RING-1;
       int a=h->delay[g-1][at],b=h->delay[g-1][older];x=a+((b-a)*(tap&127)>>7);

      }
      unsigned age=h->source.samples-start[j];int attack=age<2205?age*32767u/2205u:32767;
      x=mulq15(mulq15(x,gain),attack);
      int p=clamp(pan+spread[j],-100,100);l+=x;r+=x*p;
     }
     int raw[2]={((100*l-r)*mix_scale)>>18,((100*l+r)*mix_scale)>>18};
     int a=v?(int)(v->ph[2]&65535u):0,b=v?(int)(v->ph[2]>>16):0,amp=a+vs_shift0((b-a)*(int)i,CTL_LOG2);
     int mono=mulq15(mulq15(carrier,gain*(int)count),amp);reference+=((mono<0?-mono:mono)*200*mix_scale)>>18;
     if(v){for(unsigned ch=0;ch<2;ch++){
      int x=raw[ch];if(v->s[5]>0)x=(x*(H_FADE-v->s[5])+v->s[2+ch]*v->s[5])>>8;
      v->s[ch]=(int32_t)v->ph[ch];v->ph[ch]=(uint32_t)x;
      mixed[ch]+=mulq15(x,amp);
     }
     /* H_FADE is divisible by four; this counter is only set to zero/H_FADE. */
     if(v->s[5]>0)v->s[5]-=4;}
    }
    meter->period[1]=(unsigned)mulq15(reference,reference_scale);
    for(unsigned ch=0;ch<2;ch++){h->mixed_previous[ch]=h->mixed_current[ch];h->mixed_current[ch]=mixed[ch];}
   }
  }
  harmony_output(h,i,correction);h->half=(h->half+1u)&3u;
 }
 for(unsigned g=0;g<5;g++)if(section[g]){voice_t*v=section[g];for(unsigned ch=0;ch<2;ch++)v->s[2+ch]=v->s[ch]+(((int32_t)v->ph[ch]-v->s[ch])*(int)((h->half+1u)&3u)>>2);if(h->done&&(int32_t)(h->source.samples-h->done)>=0){v->gate=0;v->stage=3;}}
 h->ready=1;
}
static __attribute__((noinline,minsize)) void harmony_render(track_t*t,voice_t*v,int32_t*out,uint32_t n,const vmod_t*m){
 unsigned g=(unsigned)v->s[4];if(g>=5)return;harmony_part_t*h=harm(t);
 if(h->ready&0x80000000u){h->pitch_mod=m->pitch16-v->pitch_cur;h->fine_mod=m->fine*277/256;h->ready&=0x7fffffffu;}
 /* Sampled envelopes and modulation gains are bounded Q15 (0..32767). */
 v->ph[2]=(unsigned)m->amp0|((unsigned)m->amp1<<16);
 if((unsigned)(v-t->v)+1!=h->ready)return;
 int lp=4000+((clamp((t->p[P_E4]<<8)+m->cutoff,0,127<<8)*28767)>>15);h->filter=(uint16_t)lp;
 harmony_prepare(t,n);vocal_stereo_block(t,out,n,h->cache[0],h->cache[1],0);
}

static __attribute__((noinline,minsize)) const param_desc_t*harmony_desc(const track_t*t,uint32_t k){return k==3?vocal_desc(t,k):0;}
static const char*const H_TRIG[]={"--","RETRIG","LEGATO"};
static __attribute__((noinline,minsize)) const param_desc_t* harmony_param(uint32_t id){
 static const char*const voicing[]={"FOLLOW","CHORD"},*const chords[]={"I","II","III","IV","V","VI","VII"},*const tune[]={"OFF","NATURAL","HARD"};
 static const param_desc_t extra[]={ {"VOICING",voicing,0,1,0,F_ENUM,0},{"CHORD",chords,0,6,0,F_ENUM,0},{"TUNE",tune,0,2,0,F_ENUM,0} };
 if(id>=H_VOICING&&id<=H_TUNE)return &extra[id-H_VOICING];
 static const param_desc_t width={"WIDTH",0,0,127,127,F_PCT,0};
 if(id==P_SPRD)return &width;
 static const param_desc_t trigger={"TRIGGER",H_TRIG,1,2,2,F_ENUM,0};
 static param_desc_t count={"COUNT",0,0,15,1,F_INT,0};
 if(id==P_VOICE)return &trigger;if(id>=P_LDECAY0&&id<P_LDECAY0+5){unsigned g=id-P_LDECAY0;count.max=g?15:1;count.def=H_COUNTS[g];return &count;}return 0;
}
static const preset_t HARMONY_PRESETS[]={
 {.name="SOUL CHOIR",.e={SMP_NSETS+1,60,12,0,127,64,1,0},.env={30,0,127,45},.mono=1,FX(0,0,0,0),TAG(PT_PAD)},
 {.name="INTIMATE",.e={SMP_NSETS+1,60,12,0,112,25,1,0},.env={12,0,127,36},.mono=1,FX(0,0,0,12),MORE({P_SPRD,45},{P_LDECAY2,0},{P_LDECAY3,2},{P_LDECAY4,0}),TAG(PT_PAD)},
 {.name="WIDE GOSPEL",.e={SMP_NSETS+1,60,0,0,127,90,1,0},.env={24,0,127,55},.mono=1,FX(0,0,0,20),MORE({P_SPRD,127},{P_LPAN5,1},{P_LPAN6,3}),TAG(PT_PAD)},
 {.name="WARM TRIAD",.e={SMP_NSETS+1,60,12,0,105,48,1,0},.env={24,0,127,48},.mono=1,FX(0,0,0,14),MORE({P_LPAN5,1},{P_SPRD,90},{P_LPAN6,0}),TAG(PT_PAD)},
 {.name="TUNED LEAD",.e={SMP_NSETS+1,60,12,0,127,20,1,0},.env={4,0,127,28},.mono=1,FX(0,0,0,8),MORE({P_LPAN7,1},{P_LDECAY1,0},{P_LDECAY2,0},{P_LDECAY3,0}),TAG(PT_LEAD)}
};
static const engine_t ENG_HARMONY={.name="HARMONY",.page_title={"HARMONY","ENSEMBLE"},.edit={
 {"SOURCE",SMP_ALL_NAMES,SMP_NSETS,SMP_PICK_MAX,SMP_NSETS+1,F_ENUM,0},{"ROOT",0,24,84,60,F_INT,0},{"KEY",VOCAL_KEYS,0,23,12,F_ENUM,0},{"PHRASE",VOCAL_PHRASES,0,15,0,F_ENUM,0},
 {"CUTOFF",0,0,127,127,F_PCT,0},{"HUMAN",0,0,127,64,F_PCT,0},{"",0,0,1,1,F_INT,0},{"GROUP",H_NAMES,0,4,0,F_ENUM,0}},
 .presets=HARMONY_PRESETS,.npresets=NELEM(HARMONY_PRESETS),.desc=harmony_desc,.knob={P_E0,P_E1,P_E2,P_E3},.poly=5,.sampled=1,.block=harmony_block,.note_on=harmony_voice_on,.render=harmony_render};
