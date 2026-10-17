// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// itl_caps.cpp - see itl_caps.h. Plain C++, no OS calls; also built by test/caps_test.cpp on a normal host.
// Element layouts: IEEE 802.11-2020 9.4.2.55 (HT Capabilities), 9.4.2.56 (HT Operation), 9.4.2.156 (VHT Capabilities),
// 9.4.2.158 (VHT Operation).
#include "itl_caps.h"
#include <string.h>

static inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void itl_caps_parse(const uint8_t *ies, size_t len, itl_ap_caps *c)
{
    memset(c, 0, sizeof(*c));
    const uint8_t *ie = ies, *end = ies + len;
    while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
        uint8_t id = ie[0], l = ie[1];
        const uint8_t *d = ie + 2;
        if (id == 45 && l >= 26) {                       // HT Capabilities: info(2) A-MPDU(1) MCS set(16) ext(2) txbf(4) asel(1)
            c->ht = true;
            c->htCap = le16(d);
            c->ampduParm = d[2];
            memcpy(c->mcsRx, d + 3, sizeof(c->mcsRx));
        } else if (id == 61 && l >= 22) {                // HT Operation: primary(1) info(5) basic MCS(16)
            c->htOp = true;
            c->htOpPrimary = d[0];
            c->htOpInfo = d[1];
        } else if (id == 191 && l >= 12) {               // VHT Capabilities: info(4) MCS map(8)
            c->vht = true;
            c->vhtCap = le32(d);
            c->vhtRxMcs = le16(d + 4);                   // rx map, rx highest rate(2), tx map, tx highest rate(2)
            c->vhtTxMcs = le16(d + 8);
        } else if (id == 192 && l >= 5) {                // VHT Operation: width(1) seg0(1) seg1(1) basic MCS map(2)
            c->vhtOp = true;
            c->vhtOpWidth = d[0];
            c->vhtSeg0 = d[1];
            c->vhtSeg1 = d[2];
        } else if (id == 255 && l >= 1 + 6 + 11 + 4 && d[0] == 35) {   // HE Capabilities: mac(6) phy(11) 80 MHz rx/tx maps, [160 MHz, 80+80 MHz]
            const uint8_t *e = d + 1;
            c->he = true;
            memcpy(c->heMac, e, 6);
            memcpy(c->hePhy, e + 6, 11);
            c->heRx80 = le16(e + 17);
            c->heRx160 = 0xffff;
            if ((c->hePhy[0] & 0x08) && l >= 1 + 6 + 11 + 8) c->heRx160 = le16(e + 21);   // 160 MHz in 5 GHz: second pair of maps
        } else if (id == 255 && l >= 1 + 4 + 2 && d[0] == 36) {         // HE Operation: params + BSS color (le32), basic MCS set
            uint32_t p = le32(d + 1);
            c->heOp = true;
            c->heColor = (uint8_t)((p >> 24) & 0x3f);
            c->heColorOn = !(p & 0x80000000u);
        }
        ie += 2 + l;
    }
}

void itl_caps_channel(const itl_ap_caps *c, uint8_t primary, bool allow160, uint8_t *bw, uint8_t *center)
{
    *bw = 0;
    *center = primary;
    // HT: 40 MHz when the secondary channel is above (1) or below (3) and the BSS allows any width
    uint8_t sco = c->htOpInfo & 3;
    bool ht40 = c->ht && c->htOp && (c->htOpInfo & 4) && (sco == 1 || sco == 3) && (c->htCap & 0x2);
    if (ht40) {
        *bw = 1;
        *center = sco == 1 ? (uint8_t)(primary + 2) : (uint8_t)(primary - 2);
    }
    if (primary <= 14 || !c->vht || !c->vhtOp || !c->vhtSeg0) return;       // 2.4 GHz has no VHT
    switch (c->vhtOpWidth) {
    case 1: {                                            // 80 MHz around seg0; with seg1 the center of a 160 MHz channel (or of the other 80+80 segment)
        int d = (int)c->vhtSeg1 - (int)c->vhtSeg0;
        if (allow160 && c->vhtSeg1 && (d == 8 || d == -8)) { *bw = 3; *center = c->vhtSeg1; }
        else { *bw = 2; *center = c->vhtSeg0; }
        break;
    }
    case 2:                                              // legacy 160 MHz encoding: seg0 is the 160 MHz center
        if (allow160) { *bw = 3; *center = c->vhtSeg0; }
        else { *bw = 2; *center = (uint8_t)(primary < c->vhtSeg0 ? c->vhtSeg0 - 8 : c->vhtSeg0 + 8); }   // the 80 MHz half with the primary
        break;
    case 3:                                              // legacy 80+80 encoding: seg0 is the primary segment
        *bw = 2;
        *center = c->vhtSeg0;
        break;
    default:                                             // 0: 20 or 40 MHz, as the HT Operation says
        break;
    }
}

uint8_t itl_caps_nss(const itl_ap_caps *c)
{
    uint8_t n = 1;
    if (c->vht) {
        n = 0;
        for (int i = 0; i < ITL_CAPS_MAX_NSS; i++)
            if (((c->vhtRxMcs >> (2 * i)) & 3) != 3) n = (uint8_t)(i + 1);
    } else if (c->ht) {
        n = 0;
        for (int i = 0; i < ITL_CAPS_MAX_NSS; i++)
            if (c->mcsRx[i]) n = (uint8_t)(i + 1);
    }
    return n ? n : 1;
}

size_t itl_caps_build_ht(uint8_t *o)
{
    memset(o, 0, 28);
    o[0] = 45; o[1] = 26;
    // LDPC | 20/40 MHz | SM power save disabled | SGI 20 | SGI 40 | RX STBC 1 stream
    uint16_t cap = 0x0001 | 0x0002 | 0x000c | 0x0020 | 0x0040 | 0x0100;
    o[2] = (uint8_t)cap; o[3] = (uint8_t)(cap >> 8);
    o[4] = 3 | (5 << 2);                                 // max A-MPDU 65535 bytes, min spacing 4 us
    o[5] = 0xff; o[6] = 0xff;                            // MCS 0-15: two spatial streams
    return 28;
}

size_t itl_caps_build_vht(uint8_t *o, bool allow160)
{
    memset(o, 0, 14);
    o[0] = 191; o[1] = 12;
    // max MPDU 3895 | 160 MHz (or 80 MHz only) | RX LDPC | SGI 80 | SGI 160 | RX STBC 1 stream | max A-MPDU exponent 7
    uint32_t cap = (0u << 0) | ((allow160 ? 1u : 0u) << 2) | (1u << 4) | (1u << 5) | ((allow160 ? 1u : 0u) << 6) | (1u << 8) | (7u << 23);
    for (int i = 0; i < 4; i++) o[2 + i] = (uint8_t)(cap >> (8 * i));
    uint16_t map = 0xfffa;                               // NSS 1, 2: MCS 0-9; NSS 3-8: not supported
    uint16_t hi = allow160 ? 1733 : 867;                 // highest rate, Mb/s (2 streams, MCS 9, SGI)
    o[6] = (uint8_t)map; o[7] = (uint8_t)(map >> 8);
    o[8] = (uint8_t)hi;  o[9] = (uint8_t)(hi >> 8);
    o[10] = (uint8_t)map; o[11] = (uint8_t)(map >> 8);
    o[12] = (uint8_t)hi;  o[13] = (uint8_t)(hi >> 8);
    return 14;
}

// HE Capabilities (IEEE 802.11ax 9.4.2.248), a station without beamforming, MU-MIMO, OFDMA-specific or TWT features:
// the firmware does what it does for the rest. Based on the capabilities mt7922 advertises in Linux (mt7922/main.c).
size_t itl_caps_build_he(uint8_t *o, bool band5g, bool allow160)
{
    bool wide = band5g && allow160;
    size_t n = 2 + 1 + 6 + 11 + 4 + (wide ? 4 : 0);
    memset(o, 0, n);
    o[0] = 255; o[1] = (uint8_t)(n - 2); o[2] = 35;
    uint8_t *mac = o + 3, *phy = o + 9;
    mac[0] = 0x01;                                       // +HTC HE
    mac[1] = 0x08;                                       // trigger frame MAC padding duration 16 us
    mac[3] = 0x02 | 0x18;                                // OM control | max A-MPDU length exponent extension 3
    mac[4] = 0x40;                                       // A-MSDU in A-MPDU
    phy[0] = band5g ? (uint8_t)(0x04 | (allow160 ? 0x08 : 0) | 0x40)   // 40/80 MHz (160 MHz) in 5 GHz, RU mapping
                    : (uint8_t)(0x02 | 0x20);                          // 40 MHz in 2.4 GHz, RU mapping
    phy[1] = 0x10 | 0x20 | 0x40;                         // device class A | LDPC | HE-LTF and GI 0.8 us for HE PPDUs
    phy[2] = 0x02 | 0x08;                                // NDP with 4x HE-LTF and 3.2 us GI | STBC RX below 80 MHz
    phy[3] = 0x02 | 0x10;                                // DCM TX / RX up to QPSK
    phy[6] = 0x20;                                       // partial bandwidth extended range
    phy[7] = 0x04;                                       // HE SU/MU PPDU with 4x HE-LTF and 0.8 us GI (as mt7922)
    phy[8] = band5g ? (uint8_t)(0x40 | (allow160 ? 0x04 | 0x08 : 0)) : (uint8_t)(0x02 | 0x40);   // 20/80 MHz in 160 MHz PPDU; DCM RU 484
    phy[9] = (uint8_t)(0x04 | 0x08 | (ITL_HE_PKT_EXT << 6));   // 1024-QAM below 242-tone RU (TX / RX) | nominal packet padding 16 us
    uint8_t *m = o + 3 + 6 + 11;
    for (int i = 0; i < (wide ? 2 : 1); i++) { m[4 * i] = (uint8_t)ITL_HE_MCS_MAP; m[4 * i + 1] = (uint8_t)(ITL_HE_MCS_MAP >> 8); m[4 * i + 2] = (uint8_t)ITL_HE_MCS_MAP; m[4 * i + 3] = (uint8_t)(ITL_HE_MCS_MAP >> 8); }
    return n;
}

uint32_t itl_caps_max_rate(const itl_ap_caps *c, uint8_t bw, bool he)
{
    uint32_t nss = itl_caps_nss(c);
    if (he && c->he) {
        // per stream, 0.8 us GI, Mb/s, by HE-MCS 7 / 9 / 11 and width 20 / 40 / 80 / 160
        static const uint16_t r[3][4] = {{86, 172, 360, 721}, {115, 229, 480, 961}, {143, 287, 600, 1201}};
        uint16_t map = bw >= 3 ? c->heRx160 : c->heRx80;
        uint32_t top = map & 3;                          // first stream: 0: MCS 0-7, 1: 0-9, 2: 0-11
        if (top > 2) top = 0;
        uint32_t hn = 0;
        for (int i = 0; i < ITL_CAPS_MAX_NSS; i++) if (((map >> (2 * i)) & 3) != 3) hn = (uint32_t)(i + 1);
        if (hn && hn < nss) nss = hn;
        return r[top][bw > 3 ? 3 : bw] * nss;
    }
    if (c->vht) {
        // per stream, short GI, Mb/s, by VHT-MCS 7 / 8 / 9
        static const uint16_t r20[3] = {72, 86, 86}, r40[3] = {150, 180, 200}, r80[3] = {325, 390, 433}, r160[3] = {650, 780, 867};
        const uint16_t *t = bw >= 3 ? r160 : bw == 2 ? r80 : bw == 1 ? r40 : r20;
        uint32_t top = (c->vhtRxMcs & 3);                // 0: MCS 0-7, 1: 0-8, 2: 0-9 for the first stream
        if (top > 2) top = 0;
        return t[top] * nss;
    }
    if (c->ht) return (bw >= 1 ? 150u : 72u) * nss;
    return 54;
}
