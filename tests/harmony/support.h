/* SPDX-License-Identifier: GPL-3.0-only */
/* Real sample decoder, voice engine, effects and mixer; simulated user flash. */
#include <assert.h>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif
#ifdef VOCAL_C1_TEST
#include "c1_host.h"
#endif
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "../hostsim.c"
#undef main
static uint64_t instructions(void){
#ifdef __APPLE__
 struct rusage_info_v4 ri;if(!proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t*)&ri))return ri.ri_instructions;
#endif
 return 0;
}
static void fixture(const char*prefix){
 char name[1024];snprintf(name,sizeof name,"%s.hdr",prefix);FILE*f=fopen(name,"rb");assert(f);assert(fread(host_samples[0],1,512,f)>=480);fclose(f);
 snprintf(name,sizeof name,"%s.bin",prefix);f=fopen(name,"rb");assert(f);size_t n=fread(host_samples[0]+512,1,0x14000-512,f);assert(n);fclose(f);smp_user_scan(0);assert(usr_nz[0]==1);
}
