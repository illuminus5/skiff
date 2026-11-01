/* SPDX-License-Identifier: GPL-3.0-only */
/* Sample-selection/analysis infrastructure retained by HARMONY.
 * The former standalone VOCAL engine (ID 15) has been removed. */
#include "stream.c"
#if FELUCCA_VOCAL_TRIM
#include "vocal_trim.c"
#endif
static const char *const VOCAL_KEYS[]={"C MAJ","C# MAJ","D MAJ","D# MAJ","E MAJ","F MAJ","F# MAJ","G MAJ","G# MAJ","A MAJ","A# MAJ","B MAJ","C MIN","C# MIN","D MIN","D# MIN","E MIN","F MIN","F# MIN","G MIN","G# MIN","A MIN","A# MIN","B MIN"};
static const char *const VOCAL_PHRASES[]={"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
static __attribute__((noinline,minsize)) int vocal_selection(const track_t*t,uint32_t*source,uint32_t*phrase){
 *source=(uint32_t)t->p[P_E0];if(*source<SMP_NSETS||*source>=SMP_NALL)return 0;
#if FELUCCA_VOCAL_TRIM
 if(!((vtrim_ready>>(*source-SMP_NSETS))&1u))return 0;
#endif
 unsigned count=usr_nz[*source-SMP_NSETS];if(!count)return 0;
 *phrase=(uint32_t)clamp(t->p[P_E3],0,(int32_t)count-1);return 1;
}

static unsigned vocal_options(const track_t*t){uint32_t source,phrase;return vocal_selection(t,&source,&phrase)?vtrim_flags(source-SMP_NSETS,phrase):0;}
static void vocal_stereo_block(track_t*,int32_t*,uint32_t,const int32_t*,const int32_t*,const vmod_t*);
static const param_desc_t*vocal_desc(const track_t*t,uint32_t k){
 static param_desc_t phrases[3]={
 {"PHRASE",VOCAL_PHRASES,0,0,0,F_ENUM,0},{"PHRASE",VOCAL_PHRASES,0,0,0,F_ENUM,0},{"PHRASE",VOCAL_PHRASES,0,0,0,F_ENUM,0}};
 static const param_desc_t empty={"PHRASE",VOCAL_PHRASES,0,0,0,F_ENUM,0};
 if(k!=3)return 0;unsigned source=(unsigned)t->p[P_E0];if(source<SMP_NSETS||source>=SMP_NALL)return &empty;
 unsigned slot=source-SMP_NSETS,count=usr_nz[slot];phrases[slot].max=count?count-1:0;return &phrases[slot];
}
