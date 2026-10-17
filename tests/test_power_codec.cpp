// Host checks exercise the same bounded CLC/SKU helpers used by the kext.
#include "../src/itl_power.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));
}
static void skuChecks() {
    int8_t guarded[163]; memset(guarded,42,sizeof(guarded));
    buildSku(guarded+1,30,true);
    assert(SKU_LEN==161 && guarded[0]==42 && guarded[162]==42);
    int8_t *s=guarded+1;
    for (unsigned i=0;i<29;i++) assert(s[i]==30);
    for (unsigned b=0;b<4;b++) {
        for (unsigned i=0;i<10;i++) assert(s[29+b*12+i]==30);
        assert(s[29+b*12+10]==127 && s[29+b*12+11]==127);
    }
    for (unsigned i=77;i<161;i++) assert(s[i]==30);
    buildSku(s,-3,false);
    for (unsigned i=0;i<4;i++) assert(s[i]==127);
    for (unsigned i=4;i<29;i++) assert(s[i]==-3);
    assert(guarded[0]==42 && guarded[162]==42);
}
static void sarChecks() {
    int8_t sar[11]; int dv=0,gv=0;
    parseSar(nullptr,0,nullptr,0,sar,&dv,&gv);
    assert(!dv && !gv && sarFor(sar,2412)==127);
    uint8_t dyn[12]={},geo[21]={};
    dyn[4]=1; dyn[5]=1;
    for (unsigned i=0;i<5;i++) dyn[7+i]=(uint8_t)(30+10*i);
    geo[5]=3; geo[17]=20; geo[18]=2; geo[19]=35; geo[20]=1;
    parseSar(dyn,sizeof(dyn),geo,sizeof(geo),sar,&dv,&gv);
    assert(dv==1 && gv==1 && sarFor(sar,2412)==20 && sarFor(sar,5200)==35);
    assert(sarFor(sar,5600)==35 && sarFor(sar,6000)==127);
    dyn[4]=0;
    parseSar(dyn,sizeof(dyn),geo,sizeof(geo),sar,&dv,&gv);
    assert(!dv && sarFor(sar,5200)==127);
}
static void malformedChecks() {
    uint8_t buffer[64]={}; ClcTables tables={};
    for (uint32_t n=0;n<16;n++) assert(!selectClcTables(buffer,n,0,&tables) || !n);
    put32(buffer,15); assert(!selectClcTables(buffer,16,0,&tables));
    put32(buffer,0xffffffffu); assert(!selectClcTables(buffer,64,0,&tables));
    put32(buffer,16); buffer[4]=3;
    assert(selectClcTables(buffer,16,0,&tables) && tables.skipped==1 && !tables.power);
    put32(buffer,40); buffer[4]=0;
    uint32_t cursor=16; ClcRule rule={}; buffer[20]=255; buffer[21]=255;
    assert(nextClcRule(buffer,40,&cursor,&rule)==-1);
    cursor=41; assert(nextClcRule(buffer,40,&cursor,&rule)==-1);
    uint32_t length=0;
    assert(!findClcRegion(buffer,sizeof(buffer),&length));
}
static void currentFirmware(const char *path) {
    std::ifstream file(path,std::ios::binary);
    assert(file.good());
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    uint32_t length=0;
    const uint8_t *base=findClcRegion(fw.data(),fw.size(),&length);
    assert(base && length>400000);
    for (uint8_t encap=0;encap<2;encap++) {
        ClcTables selected={};
        assert(selectClcTables(base,length,encap,&selected));
        assert(selected.power && (selected.power[7]&1)==encap && selected.skipped==2);
        uint32_t offset=16,world=0,rules=0;
        ClcRule rule={}; int state=0;
        while ((state=nextClcRule(selected.power,clcLe32(selected.power),&offset,&rule))>0) {
            assert(rule.bytes<=2048);
            rules++;
            if (rule.header[0]=='0' && rule.header[1]=='0') world++;
        }
        assert(state==0 && world>0 && rules>0);
        std::cout << "CLC encap=" << (unsigned)encap << " rules=" << rules
                  << " world=" << world << " skipped=" << selected.skipped << "\n";
    }
    // Reject corrupt region lengths without reading beyond the real image.
    uint8_t *tr=fw.data()+fw.size()-36;
    uint8_t *first=tr-(size_t)tr[2]*40;
    put32(first+20,0xffffffffu);
    assert(!findClcRegion(fw.data(),fw.size(),&length));
}
int main(int argc,char **argv) {
    assert(argc==2);
    skuChecks(); sarChecks(); malformedChecks(); currentFirmware(argv[1]);
    std::cout << "MT7922 power/SAR/current-firmware checks passed\n";
}
