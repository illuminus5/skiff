/* SPDX-License-Identifier: GPL-3.0-only */
/* Peak compressor curve. Block-rate log/exp interpolation; no runtime floating
 * point or heap. 0 dB reference is the engine's 32768 amplitude, before LEVEL.
 * The detector links the mono bus and stereo components; gain never boosts. */
#pragma once
static const uint16_t CMP_THRESH[128]={130,136,142,149,155,162,169,177,185,193,202,211,220,230,240,251,262,273,286,298,311,325,340,355,371,387,404,422,441,461,481,503,525,548,573,598,625,653,682,712,744,777,811,847,885,924,965,1008,1053,1100,1149,1200,1254,1309,1367,1428,1492,1558,1627,1700,1775,1854,1937,2023,2113,2207,2305,2408,2515,2627,2743,2865,2993,3126,3265,3410,3562,3720,3886,4058,4239,4428,4624,4830,5045,5269,5504,5748,6004,6271,6550,6841,7145,7463,7795,8142,8504,8882,9277,9690,10121,10571,11041,11532,12045,12581,13140,13725,14335,14972,15638,16334,17060,17819,18611,19439,20304,21207,22150,23135,24164,25239,26361,27533,28758,30037,31373,32768};
static const uint16_t CMP_LOG[17]={0,358,696,1016,1319,1607,1882,2145,2396,2637,2869,3092,3307,3514,3715,3908,4096};
static const uint32_t CMP_EXP[17]={16777216,16065917,15384775,14732511,14107901,13509772,12937002,12388516,11863283,11360319,10878679,10417458,9975792,9552851,9147842,8760003,8388608};
static uint32_t cmp_abs(int32_t x){return x<0?(uint32_t)(-(int64_t)x):(uint32_t)x;}
static uint32_t cmp_log2(uint32_t x)
{
    if(!x)return 0;
    uint32_t e=31u-(uint32_t)__builtin_clz(x);
    uint32_t q=(uint32_t)(((uint64_t)x<<16)>>e)-65536u,j=q>>12,f=q&4095u;
    return e*4096u+CMP_LOG[j]+((CMP_LOG[j+1]-CMP_LOG[j])*f>>12);
}
static uint32_t cmp_target(uint32_t peak,uint32_t threshold,uint32_t ratio)
{
    static const uint16_t slope[8]={0,10923,16384,21845,24576,27307,28672,30037};
    uint32_t t=CMP_THRESH[threshold>127?127:threshold],r=ratio>127?7:ratio>>4;
    if(peak<=t||!r)return 16777216;
    uint32_t red=(cmp_log2(peak)-cmp_log2(t))*slope[r]>>15;
    uint32_t whole=red>>12,j=(red&4095u)>>8,f=red&255u;
    uint32_t gain=CMP_EXP[j]-((CMP_EXP[j]-CMP_EXP[j+1])*f>>8);
    return whole>=24?0:gain>>whole;
}
static uint32_t cmp_release_ms(uint32_t v){return 20u+((v>127?127:v)*980u+63u)/127u;}
