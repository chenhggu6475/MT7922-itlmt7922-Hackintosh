// SPDX-License-Identifier: BSD-3-Clause-Clear
// RX group order and fields derived from mt76, Copyright (C) 2020 MediaTek Inc.
// Reference: mt7921/mac.c:mt7921_mac_fill_rx, mt76_connac2_mac.h,
// mt792x.h:mt792x_get_status_freq_info at mt76 467ae2e3f190.
#include "mt7922_rx_codec.h"
#include <string.h>

namespace {
uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool advance(uint32_t *offset, uint32_t bytes, uint32_t length)
{
    if (*offset > length || bytes > length - *offset) return false;
    *offset += bytes;
    return true;
}

// mt76_connac2_mac_fill_rx_rate() for non-mt7915 / MT7922: P-RXV word0.
// Invalid PHY metadata does not invalidate an otherwise valid received frame.
void phyVector(uint32_t word, Mt7922RxView *v)
{
    const uint8_t mode = (uint8_t)((word >> 24) & 15);
    uint8_t rate = (uint8_t)(word & 127);
    uint8_t nss = (uint8_t)(((word >> 7) & 7) + 1);
    const uint8_t stbc = (uint8_t)((word >> 22) & 3);
    const uint8_t gi = (uint8_t)((word >> 15) & 3);
    const uint8_t bw = (uint8_t)((word >> 12) & 7);
    if (bw > 3) return;
    if (stbc && nss > 1) nss >>= 1;
    switch (mode) {
    case 0: // CCK, hardware indices0..3 long /4..7 short preamble
        if (rate > 7) return;
        nss = 1;
        break;
    case 1: // OFDM hardware rate indices8..15
        if (rate < 8 || rate > 15) return;
        nss = 1;
        break;
    case 2:
    case 3:
        if (rate > 31) return;
        nss = (uint8_t)(rate / 8 + 1); // HT NSS is encoded in the MCS index.
        break;
    case 4:
        if (rate > 11) return;
        break;
    case 8:
    case 9:
    case 10:
    case 11:
        rate &= 15; // DCM/ER flags share the upper bits of the rate field.
        if (rate > 11) return;
        break;
    default:
        return;
    }
    v->phy_valid = 1;
    v->phy_mode = mode;
    v->phy_mcs = rate;
    v->phy_nss = nss;
    v->phy_bw = bw;
    v->phy_gi = mode >= 8 ? gi : (mode >= 2 && gi ? 1 : 0);
}
}

Mt7922RxStatus mt7922_parse_rx_normal(const uint8_t *p, uint32_t n,
                                     Mt7922RxView *out)
{
    if (!p || !out || n < 24) return MT7922_RX_SHORT;
    const uint32_t d0 = get32(p), d1 = get32(p + 4), d2 = get32(p + 8);
    const uint32_t d3 = get32(p + 12), d4 = get32(p + 16);
    const uint32_t type = d0 >> 27, flag = (d0 >> 16) & 15;
    if (type != 2 && type != 8 && !(type == 7 && flag == 1)) return MT7922_RX_TYPE;
    const uint32_t length = d0 & 0xffff;
    if (length < 24 || length > n) return MT7922_RX_LENGTH;
    if ((d1 & ((1u << 25) | (1u << 26) | (1u << 27) | (1u << 28))) ||
        (d2 & ((1u << 23) | (1u << 24)))) return MT7922_RX_ERROR;
    if (d2 & (1u << 13)) return MT7922_RX_TRANSLATED;
    // Group 5 is only defined in the P-RXV (group 3) path for this generation.
    if ((d1 & (1u << 15)) && !(d1 & (1u << 13))) return MT7922_RX_GROUPS;

    Mt7922RxView v = {};
    v.packet_length = (uint16_t)length;
    v.packet_type = (uint8_t)type;
    v.wlan_index = (uint16_t)(d1 & 0x3ff);
    v.channel_encoded = (uint8_t)(d3 >> 8);
    v.channel = v.channel_encoded;
    v.band = v.channel > 180 ? 6 : (v.channel > 14 ? 5 : 2);
    if (v.band == 6) v.channel = (uint8_t)((v.channel - 181) * 4 + 1);
    v.payload_format = (uint8_t)(d4 & 3);
    v.security_mode = (uint8_t)((d1 >> 16) & 31);
    v.security_done = (uint8_t)(d1 >> 31);
    v.cipher_mismatch = (uint8_t)((d1 >> 23) & 3);
    v.fragmented = (uint8_t)((d2 >> 27) & 1);

    uint32_t off = 24;
    if ((d1 & (1u << 14)) && !advance(&off, 16, length)) return MT7922_RX_GROUPS;
    if ((d1 & (1u << 11)) && !advance(&off, 16, length)) return MT7922_RX_GROUPS;
    if ((d1 & (1u << 12)) && !advance(&off, 8, length)) return MT7922_RX_GROUPS;
    if (d1 & (1u << 13)) {
        const uint32_t group3 = off;
        if (!advance(&off, 8, length)) return MT7922_RX_GROUPS;
        phyVector(get32(p + group3), &v);
        uint32_t rcpi_offset = group3 + 4;
        if (d1 & (1u << 15)) {
            // Linux skips 6 words of group 5, then uses the first of 12 words
            // of the C-RXV as RCPI. Group 5 is 72 bytes in total.
            if (!advance(&off, 24, length)) return MT7922_RX_GROUPS;
            rcpi_offset = off;
            if (!advance(&off, 48, length)) return MT7922_RX_GROUPS;
        }
        const uint32_t rcpi = get32(p + rcpi_offset);
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t r = (uint8_t)(rcpi >> (8 * i));
            if (r == 0 || r == 255) continue; // unused / unavailable chain
            v.chain_valid_mask |= (uint8_t)(1u << i);
            v.chain_rssi_dbm[i] = (int16_t)(((int)r - 220) / 2);
        }
    }
    if (!advance(&off, 2 * ((d2 >> 14) & 3), length)) return MT7922_RX_GROUPS;
    if (off >= length) return MT7922_RX_SHORT;
    v.frame_offset = off;
    v.frame_length = length - off;
    *out = v;
    return MT7922_RX_OK;
}

bool mt7922_parse_beacon(const uint8_t *p, uint32_t n, Mt7922Beacon *out)
{
    if (!p || !out || n < 36) return false;
    const uint16_t fc = get16(p);
    if (fc != 0x0080 && fc != 0x0050) return false;
    if ((p[10] & 1) || memcmp(p + 10, p + 16, 6) != 0) return false;
    if (fc == 0x0080) {
        for (unsigned i = 4; i < 10; ++i) if (p[i] != 255) return false;
    }
    Mt7922Beacon b = {};
    memcpy(b.bssid, p + 16, 6);
    b.beacon_interval = get16(p + 32);
    b.capability = get16(p + 34);
    b.probe_response = fc == 0x0050;
    bool have_ssid = false;
    uint8_t ht_channel = 0;
    uint32_t off = 36;
    while (off < n) {
        if (n - off < 2) return false;
        const uint8_t id = p[off], l = p[off + 1];
        off += 2;
        if (l > n - off) return false;
        if (id == 0) {
            if (have_ssid || l > 32) return false;
            memcpy(b.ssid, p + off, l);
            b.ssid_length = l;
            have_ssid = true;
        } else if (id == 3 && l == 1) {
            b.channel = p[off];
        } else if (id == 61 && l >= 1) {
            ht_channel = p[off];
        }
        off += l;
    }
    if (!have_ssid) return false;
    if (!b.channel) b.channel = ht_channel;
    *out = b;
    return true;
}

const char *mt7922_rx_status_name(Mt7922RxStatus s)
{
    switch (s) {
    case MT7922_RX_OK: return "ok";
    case MT7922_RX_SHORT: return "short";
    case MT7922_RX_TYPE: return "packet-type";
    case MT7922_RX_LENGTH: return "length";
    case MT7922_RX_GROUPS: return "groups";
    case MT7922_RX_ERROR: return "rx-error";
    case MT7922_RX_TRANSLATED: return "header-translated";
    }
    return "unknown";
}
