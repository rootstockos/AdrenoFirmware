/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* A8xx HFI additions: the GPU_PERF table encoding, MX dependency votes and the every-bandwidth perfmode rule. With
 * SOURCE_ORACLE the encodings are compared with the original mainline functions, compiled outside Git
 * (scripts/check-adreno-hfi-a8xx.py); without it, with fixed vectors only. */
#include "AdrenoHfiVotes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#ifdef SOURCE_ORACLE
extern int source_perf_table(const AdrenoHfiStartupConfig *,unsigned *id,unsigned *words,uint32_t payload[159]);
extern int source_dependency_votes(const uint32_t *levels,unsigned count,const uint16_t *mx,unsigned mx_count,
    uint32_t *votes);
#endif
static void bandwidth(AdrenoHfiStartupConfig *c)
{
    static const uint32_t kbps[3]={0,2136718,18597656};
    AdrenoHfiBcm bcms[3]={{.address=0x50000,.unit=1000,.width=4,.buswidth=16,.vcd=0},
        {.address=0x50008,.unit=1000,.width=1,.buswidth=4,.vcd=1},
        {.address=0x50010,.fixed=1,.perfmode=4,.perfmode_kbps=10687500,.vcd=2}};
    CHECK(AdrenoHfiBuildBandwidth(kbps,3,bcms,3,0x50020,c)==AdrenoHfiStartupSuccess);
}
int main(void)
{
    /* An MX rail profile as cmd-db stores it: off, ascending, zero padding. */
    const uint16_t mx_levels[8]={0,48,64,128,192,256,384,0};
    uint8_t mx[16];for(unsigned i=0;i<8;++i){mx[2*i]=(uint8_t)mx_levels[i];mx[2*i+1]=(uint8_t)(mx_levels[i]>>8);}
    const uint32_t levels[5]={0,56,64,192,384};
    uint32_t votes[5];
    CHECK(AdrenoHfiDependencyVotes(levels,5,mx,sizeof(mx),votes,5)==AdrenoHfiStartupSuccess);
    CHECK(votes[0]==UINT32_MAX);
    CHECK(votes[1]==((0x3fffu<<14)|(2u<<8)|0xff) && votes[2]==((0x3fffu<<14)|(2u<<8)|0xff));   /* 56 -> 64 at 2 */
    CHECK(votes[3]==((0x3fffu<<14)|(4u<<8)|0xff) && votes[4]==((0x3fffu<<14)|(6u<<8)|0xff));
#ifdef SOURCE_ORACLE
    uint32_t original[5];
    CHECK(!source_dependency_votes(levels,5,mx_levels,8,original) && !memcmp(original,votes,sizeof(votes)));
#endif
    /* A level above the rail, descending levels and a malformed rail refuse without writing. */
    const uint32_t high[2]={0,500},down[3]={0,192,64};uint32_t untouched[3]={7,7,7};
    CHECK(AdrenoHfiDependencyVotes(high,2,mx,sizeof(mx),untouched,3)==AdrenoHfiStartupInvalid && untouched[0]==7);
    CHECK(AdrenoHfiDependencyVotes(down,3,mx,sizeof(mx),untouched,3)==AdrenoHfiStartupInvalid && untouched[1]==7);
    CHECK(AdrenoHfiDependencyVotes(levels,5,mx,3,untouched,3)==AdrenoHfiStartupInvalid);
    CHECK(AdrenoHfiDependencyVotes(levels+1,4,mx,sizeof(mx),votes,5)==AdrenoHfiStartupInvalid);   /* no off level */

    /* The table format. */
    AdrenoHfiStartupConfig c;memset(&c,0,sizeof(c));
    c.gpu_count=5;c.gmu_count=3;
    for(unsigned i=0;i<5;++i)c.gpu[i]=(AdrenoHfiPerformanceLevel){i?264000000ULL+i*100000000ULL:0,i?0x380000u+i:0};
    c.gmu[1]=(AdrenoHfiPerformanceLevel){350000000ULL,0x400001};c.gmu[2]=(AdrenoHfiPerformanceLevel){650000000ULL,0x800002};
    bandwidth(&c);
    /* A7xx profile: dependency votes must stay zero. */
    CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupSuccess);
    c.gpu_dependency[1]=1;CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupInvalid);c.gpu_dependency[1]=0;
    /* A8xx profile: off = 0xffffffff, every active level voted, unused slots zero. */
    c.table_format=1;
    CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupInvalid);
    memcpy(c.gpu_dependency,votes,sizeof(votes));
    CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupSuccess);
    c.gpu_dependency[5]=1;CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupInvalid);c.gpu_dependency[5]=0;
    c.gpu_dependency[3]=0;CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupInvalid);c.gpu_dependency[3]=votes[3];
    c.table_format=2;CHECK(AdrenoHfiStartupValidate(&c)==AdrenoHfiStartupInvalid);c.table_format=1;
    uint32_t payload[159],id=0;size_t words=0;
    CHECK(AdrenoHfiStartupEncode(&c,AdrenoHfiStagePerformance,&id,payload,159,&words)==AdrenoHfiStartupSuccess);
    CHECK(id==15 && words==6+3*5+2*3);
    CHECK(payload[0]==0 && payload[1]==1 && payload[2]==5 && payload[3]==3);
    CHECK(payload[4]==0 && payload[5]==UINT32_MAX && payload[6]==0);
    CHECK(payload[7]==0x380001 && payload[8]==votes[1] && payload[9]==364000);
    CHECK(payload[19]==3 && payload[20]==2 && payload[21]==0 && payload[22]==0);
    CHECK(payload[23]==0x400001 && payload[24]==350000 && payload[25]==0x800002 && payload[26]==650000);
#ifdef SOURCE_ORACLE
    uint32_t source[159];unsigned source_id=0,source_words=0;
    CHECK(!source_perf_table(&c,&source_id,&source_words,source));
    CHECK(source_id==id && source_words==words && !memcmp(source,payload,words*4));
#endif
    /* The other stages are unchanged by the table format. */
    CHECK(AdrenoHfiStartupEncode(&c,AdrenoHfiStageBandwidth,&id,payload,159,&words)==AdrenoHfiStartupSuccess &&
        id==3 && words==159);

    /* Fixed BCM perfmode: the A7xx threshold rule, or every nonzero bandwidth. */
    static const uint32_t kbps[3]={0,2136718,18597656};
    AdrenoHfiBcm bcms[3]={{.address=0x50000,.unit=1000,.width=4,.buswidth=16,.vcd=0},
        {.address=0x50008,.unit=1000,.width=1,.buswidth=4,.vcd=1},
        {.address=0x50010,.fixed=1,.perfmode=4,.perfmode_kbps=10687500,.vcd=2}};
    AdrenoHfiStartupConfig b;memset(&b,0,sizeof(b));
    CHECK(AdrenoHfiBuildBandwidth(kbps,3,bcms,3,0x50020,&b)==AdrenoHfiStartupSuccess);
    CHECK(b.ddr_data[0][2]==0x40000000u && b.ddr_data[1][2]==0x60000000u && b.ddr_data[2][2]==0x60000004u);
    bcms[2].perfmode_all=1;
    CHECK(AdrenoHfiBuildBandwidth(kbps,3,bcms,3,0x50020,&b)==AdrenoHfiStartupSuccess);
    CHECK(b.ddr_data[0][2]==0x40000000u && b.ddr_data[1][2]==0x60000004u && b.ddr_data[2][2]==0x60000004u);
    /* Proportional BCMs are the same either way. */
    CHECK(b.ddr_data[2][0]==(0x60000000u|0x3fffu<<14|0x3fffu));   /* 18597656*4/16 kB/s clamps to 0x3fff */
    CHECK(b.ddr_data[1][1]==(0x60000000u|0x3fffu<<14|0x3fffu));   /* 2136718*1/4 = 534179 also clamps */
    bcms[0].perfmode_all=1;CHECK(AdrenoHfiBuildBandwidth(kbps,3,bcms,3,0x50020,&b)==AdrenoHfiStartupInvalid);
    bcms[0].perfmode_all=0;bcms[2].perfmode_all=2;CHECK(AdrenoHfiBuildBandwidth(kbps,3,bcms,3,0x50020,&b)==AdrenoHfiStartupInvalid);
#ifdef SOURCE_ORACLE
    printf("PASS: A8xx GPU_PERF table and MX dependency votes equal the pinned mainline functions; perfmode rules\n");
#else
    printf("PASS: A8xx GPU_PERF table, MX dependency votes and perfmode rules (fixed vectors, no source oracle)\n");
#endif
    return 0;
}
