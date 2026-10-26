/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_PORT_ONLY 1
#include "ui_test.c"
#include <assert.h>
#include <math.h>
static int32_t b[CTL],d[CTL],e[CTL];
static void setup(void){ui_power_on();memset(ins,0,sizeof ins);trk[0].p[P_ITYPE]=IT_COMP;trk[0].p[P_IA]=64;trk[0].p[P_IB]=64;trk[0].p[P_IC]=24;trk[0].p[P_IMIX]=127;}
static void constant(int32_t x,int32_t y,int32_t z){for(unsigned i=0;i<CTL;i++){b[i]=x;d[i]=y;e[i]=z;}track_insert_linked(&trk[0],b,d,e,CTL);}
int main(void){
    const double ratios[8]={1,1.5,2,3,4,6,8,12};double max_error=0;
    for(unsigned th=0;th<128;th++)for(unsigned r=0;r<8;r++)for(unsigned pk=1;pk<=262144;pk=pk*11/10+1){
        double threshold=32768*pow(10,(-48+48.0*th/127)/20),g=pk<=threshold?1:pow(threshold/pk,1-1/ratios[r]);
        double actual=cmp_target(pk,th,r*16)/16777216.0,error=fabs(20*log10(actual/g));if(error>max_error)max_error=error;
        assert(actual>=0&&actual<=1&&error<0.16);
    }
    assert(cmp_target(0,0,127)==16777216&&cmp_target(32768,0,0)==16777216);
    setup();for(unsigned k=0;k<3000;k++)constant(16000,8000,-4000);
    assert(b[CTL-1]<6000&&b[CTL-1]>1000);assert(abs(b[CTL-1]-2*d[CTL-1])<=2&&abs(b[CTL-1]+4*e[CTL-1])<=4);
    int32_t low=ins[0].hp;for(unsigned k=0;k<5000;k++)constant(0,0,0);assert(ins[0].hp>low&&ins[0].hp>16700000);
    setup();trk[0].p[P_IB]=0;for(unsigned k=0;k<20;k++)constant(17000,-8500,4000);assert(b[0]==17000&&d[0]==-8500&&e[0]==4000);
    setup();trk[0].p[P_IMIX]=0;constant(20000,-10000,5000);assert(b[0]==20000&&d[0]==-10000&&e[0]==5000&&!trk[0].ins_run);
    setup();trk[0].p[P_ITYPE]=IT_OFF;constant(20000,-10000,5000);assert(b[0]==20000&&d[0]==-10000&&e[0]==5000);
    setup();constant(24000,12000,6000);assert(b[0]>20000);for(unsigned k=0;k<40;k++)constant(24000,12000,6000);assert(b[CTL-1]<6000);
    int32_t before=ins[0].hp;trk[0].p[P_IC]=127;constant(0,0,0);int32_t slow=ins[0].hp-before;
    ins[0].hp=before;trk[0].p[P_IC]=0;constant(0,0,0);assert(ins[0].hp-before>slow*10);
    setup();for(unsigned k=0;k<100;k++)constant(0,24000,-24000);assert(b[0]==0&&d[0]<10000&&abs(d[0]+e[0])<=1);
    assert(!ins[1].hp&&!ins[1].w);trk[0].p[P_ITYPE]=IT_OFF;for(unsigned k=0;k<30;k++)constant(16000,8000,4000);assert(!trk[0].ins_run&&b[0]==16000&&d[0]==8000);
    setup();for(unsigned th=0;th<128;th+=17)for(unsigned r=0;r<128;r+=17)for(unsigned k=0;k<100;k++){
        trk[0].p[P_IA]=th;trk[0].p[P_IB]=r;constant(k&1?-262144:262143,131072,-65536);assert(cmp_abs(b[0])<=262144&&cmp_abs(d[0])<=131072);
    }
    setup();trk[0].p[P_IA]=43;trk[0].p[P_IB]=103;trk[0].p[P_IC]=77;
    project_t p,q;project_capture(&p);project_store_t wire;assert(proj_pack4(&wire,&p));assert(proj_unpack4(&q,wire.raw));assert(!project_restore_runtime(&q));
    assert(trk[0].p[P_ITYPE]==IT_COMP&&trk[0].p[P_IA]==43&&trk[0].p[P_IB]==103&&trk[0].p[P_IC]==77);
    int stored=up_store(0,"COMP TEST");assert(stored==0||stored==3);trk[0].p[P_ITYPE]=IT_OFF;assert(!up_load(0));assert(trk[0].p[P_ITYPE]==IT_COMP&&trk[0].p[P_IA]==43&&trk[0].p[P_IB]==103&&trk[0].p[P_IC]==77);
    const char *labels[]={"THRESH","RATIO","RELEASE"};for(unsigned k=0;k<3;k++)assert(str_eq(track_desc(&trk[0],P_IA+k)->label,labels[k]));
    char val[16];const char *unit;param_format(ins_desc(IT_COMP,1),64,val,&unit);assert(str_eq(val,"4:1"));
    param_format(ins_desc(IT_COMP,0),0,val,&unit);assert(str_eq(val,"-48.0")&&str_eq(unit,"dB"));
    param_format(ins_desc(IT_COMP,2),127,val,&unit);assert(str_eq(val,"1000")&&str_eq(unit,"ms"));
    printf("COMP: analytic curve max error %.3f dB; attack/release, linked stereo, side-only detector, bypass, extremes, independent state, project and labels passed\n",max_error);return 0;
}
