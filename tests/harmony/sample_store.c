/* SPDX-License-Identifier: GPL-3.0-only */
static int vtrim_page_on(void){return 0;}
static int veq_page_on(void){return 0;}
#include "slice_store_preview.c"
#include <assert.h>
int main(int argc,char**argv){assert(argc==2);char path[1024];unsigned char*slot=(unsigned char*)host_slots;snprintf(path,sizeof path,"%s.hdr",argv[1]);FILE*f=fopen(path,"rb");assert(f);assert(fread(slot,1,512,f)>=480);fclose(f);snprintf(path,sizeof path,"%s.bin",argv[1]);f=fopen(path,"rb");assert(f);assert(fread(slot+512,1,SMP_USER_SIZE-512,f));fclose(f);slc_store_boot();smp_user_scan(0);assert(usr_nz[0]);
 unsigned char*before=malloc(SLC_REC_OFF);assert(before);memcpy(before,slot,SLC_REC_OFF);
 vtrim_data old=vtrim_get(0,0);vocal_trim_region r=old.region;r.edge[0]=100;r.edge[1]=2000;r.edge[2]=9000;r.edge[3]-=100;assert(vtrim_put(0,0,&r,1));
 unsigned eq=(24u<<veq_shift(0))|(29u<<veq_shift(3));vtrim_mask_put(0,0,VEQ_MASK,eq,1);usr_mix[0][0][0]=100;assert(slc_store_write(0)==3);assert(!memcmp(before,slot,SLC_REC_OFF));
 memset(vtrim_table,0,sizeof vtrim_table);memset(vtrim_active,0,sizeof vtrim_active);memset(usr_mix,0,sizeof usr_mix);smp_user_scan(0);vtrim_data restored=vtrim_get(0,0);assert(!memcmp(&restored.region,&r,sizeof r));assert((restored.timing&VEQ_MASK)==(eq&VEQ_MASK));assert(usr_mix[0][0][0]==100);assert(slc_store_write(0)==0);
 slot[SLC_REC_OFF+252+12]^=1;smp_user_scan(0);restored=vtrim_get(0,0);assert(!memcmp(&restored.region,&old.region,sizeof old.region));assert(!(restored.timing&VEQ_MASK));assert(!memcmp(before,slot,SLC_REC_OFF));free(before);puts("Real sample-tail encoder/loader: trim, EQ and level survive simulated cold rescan; corrupt trim rejected; sample/header bytes unchanged.");return 0;}
