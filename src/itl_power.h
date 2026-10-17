// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
#ifndef ITL_POWER_H
#define ITL_POWER_H
#include <stdint.h>
#include <string.h>
static const unsigned SKU_LEN = 161;
static const uint16_t sarRange[11][2] = {
    {2402, 2494}, {5150, 5350}, {5350, 5470}, {5470, 5725}, {5725, 5950},
    {5945, 6165}, {6165, 6405}, {6405, 6525}, {6525, 6705}, {6705, 6865}, {6865, 7125},
};

static inline bool asarFits(const uint8_t *t, int len, int pre, int unit, int nrOff, int min, int max)
{
    if (len < pre || (len - pre) % unit) return false;
    int cnt = (len - pre) / unit;
    return cnt >= min && cnt <= max && t[nrOff] == cnt;
}

// First dynamic profile and world geo row, matching mt792x_asar_range_pwr().
static inline void parseSar(const uint8_t *dyn, int dl, const uint8_t *geo, int gl,
                            int8_t sar[11], int *dynVer, int *geoVer)
{
    for (int i = 0; i < 11; i++) sar[i] = 127;
    const uint8_t *frp = nullptr;
    int nfrp = 0;
    if (dl > 0 && asarFits(dyn, dl, 6, 6, 5, 1, 8)) { frp = dyn + 6 + 1; nfrp = 5; }
    else if (dl > 0 && asarFits(dyn, dl, 7, 12, 6, 1, 8)) { frp = dyn + 7 + 1; nfrp = 11; }
    if (!frp || !dyn[4]) {                                   // no table, or dynamic SAR disabled
        *dynVer = *geoVer = 0;
        return;
    }
    const uint8_t *band = nullptr;                           // { pwr, offset } per band of geo row 2 (WW)
    int nband = 0;
    if (gl > 0 && asarFits(geo, gl, 6, 5, 5, 3, 8)) { band = geo + 6 + 2 * 5 + 1; nband = 2; }
    else if (gl > 0 && asarFits(geo, gl, 7, 7, 6, 3, 8)) { band = geo + 7 + 2 * 7 + 1; nband = 3; }
    for (int i = 0; i < 11 && i < nfrp; i++) {
        int p = frp[i] == 0xff ? 127 : frp[i];
        int b = sarRange[i][0] >= 5945 ? 2 : sarRange[i][0] >= 5150 ? 1 : 0;
        if (band && b < nband && band[2 * b] != 0xff) {
            int g = band[2 * b], o = band[2 * b + 1];
            p = p + o < 127 ? p + o : 127;
            if (g < p) p = g;
        }
        sar[i] = (int8_t)(p > 127 ? 127 : p);
    }
    *dynVer = nfrp == 11 ? 2 : 1;
    *geoVer = nband == 3 ? 2 : nband == 2 ? 1 : 0;
}

// Connac2 table: CCK4 + OFDM8 + HT17 + VHT(4*12) + HE(7*12).
static inline void buildSku(int8_t *sku, int8_t value, bool g2)
{
    memset(sku,127,SKU_LEN);
    if (g2) memset(sku,value,4);
    memset(sku+4,value,8);
    memset(sku+12,value,17);
    for (unsigned bw=0;bw<4;bw++) memset(sku+29+bw*12,value,10);
    memset(sku+77,value,84);
}

static inline int8_t sarFor(const int8_t sar[11], uint32_t freq)
{
    for (int i = 0; i < 11; i++)
        if (freq >= sarRange[i][0] && freq < sarRange[i][1]) return sar[i];
    return 127;
}


// Portable bounded Connac2 CLC parsing; runtime and host fixtures share this.
static inline uint32_t clcLe32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
struct ClcTables {
    const uint8_t *power;
    const uint8_t *channels;
    uint32_t skipped;
};
static inline const uint8_t *findClcRegion(const uint8_t *fw, size_t bytes, uint32_t *length)
{
    if (!fw || !length || bytes<36) return nullptr;
    const uint8_t *tr=fw+bytes-36;
    const uint32_t count=tr[2];
    if (!count || count>(bytes-36)/40) return nullptr;
    const size_t dataBytes=bytes-36-(size_t)count*40;
    size_t offset=0;
    for (uint32_t i=0;i<count;i++) {
        const uint8_t *r=tr-(size_t)(count-i)*40;
        uint32_t len=clcLe32(r+20);
        if (offset>dataBytes || len>dataBytes-offset) return nullptr;
        if ((r[24]&64) && r[25]==2) { *length=len; return fw+offset; }
        offset+=len;
    }
    return nullptr;
}
static inline bool selectClcTables(const uint8_t *base, uint32_t length, uint8_t encap, ClcTables *out)
{
    if (!base || !out || encap>1) return false;
    ClcTables selected={nullptr,nullptr,0};
    for (uint32_t offset=0;offset<length;) {
        if (length-offset<16) return false;
        const uint8_t *table=base+offset;
        const uint32_t bytes=clcLe32(table);
        if (bytes<16 || bytes>length-offset) return false;
        uint8_t idx=table[4];
        if (idx==0 && !selected.power && (table[7]&1)==encap) selected.power=table;
        else if (idx==1 && !selected.channels) selected.channels=table;
        else if (idx>1) selected.skipped++;
        offset+=bytes;
    }
    *out=selected;
    return true;
}
struct ClcRule { const uint8_t *header; const uint8_t *data; uint16_t bytes; };
// Returns 1 for a rule, 0 for upstream's <=16-byte tail, -1 for malformed data.
static inline int nextClcRule(const uint8_t *table, uint32_t length, uint32_t *offset, ClcRule *out)
{
    if (!table || !offset || !out || length<16 || *offset<16 || *offset>length) return -1;
    if (length-*offset<=16) return 0;
    const uint8_t *h=table+*offset;
    uint16_t bytes=(uint16_t)((uint16_t)h[4]|((uint16_t)h[5]<<8));
    if (bytes>length-*offset-6) return -1;
    out->header=h; out->data=h+6; out->bytes=bytes;
    *offset+=6+bytes;
    return 1;
}
#endif
