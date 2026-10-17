// SPDX-License-Identifier: BSD-3-Clause-Clear
// Derived from mt76_connac_mac.c:mt76_connac2_mac_write_txwi*, MediaTek Inc.
#include "mt7922_tx_codec.h"
#include <string.h>

namespace {
void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
}

bool mt7922_encode_txwi(uint8_t out[32], const uint8_t *f, uint32_t len,
                         const Mt7922TxOptions &o)
{
    if (!out || !f || len < 24 || len > 65503 || o.wlan_index > 1023 ||
        o.queue > 127 || o.tid > 7 || o.fixed_rate > 0x3fff || o.spe_index > 31 ||
        o.sequence < -1 || o.sequence > 4095 || (f[0] & 3)) return false;
    const uint32_t type = (f[0] >> 2) & 3, subtype = f[0] >> 4;
    if (type != 0 && type != 2) return false;
    const bool qos = type == 2 && (f[0] & 0x80);
    const uint32_t hdr = 24 + (qos ? 2 : 0) + (qos && (f[1] & 0x80) ? 4 : 0);
    if (len < hdr) return false;
    uint32_t tid = o.tid, add_ba = 0;
    if (type == 0 && subtype == 13 && len >= 33 && f[24] == 3 && f[25] == 0) {
        tid = ((uint32_t)f[27] >> 2) & 15;
        if (tid > 7) return false;
        add_ba = 1u << 14;
    }
    const bool fixed = !o.rate_adapt;
    const bool multicast = f[4] & 1;
    memset(out, 0, 32);
    put32(out, (len + 32) | ((uint32_t)o.queue << 25)); // CT packet format is 0
    put32(out + 4, (1u << 31) | o.wlan_index | (2u << 16) |
                    ((hdr / 2) << 11) | (tid << 20));
    put32(out + 8, (type << 4) | subtype | (multicast ? (1u << 10) : 0u) |
                    (fixed ? ((1u << 31) | (1u << 13)) : 0u));
    put32(out + 12, (15u << 11) | ((!o.aggregate || fixed) ? (1u << 28) : 0u) |
                     (o.protect ? 2u : 0u) | (multicast ? 1u : 0u) |
                     (o.sequence >= 0 ? ((1u << 31) | ((uint32_t)o.sequence << 16)) : 0u));
    put32(out + 20, add_ba | (fixed ? (3u | (1u << 10)) : 0u));
    put32(out + 24, fixed ? ((1u << 2) | ((uint32_t)o.fixed_rate << 16)) : 0u);
    put32(out + 28, (type << 20) | (subtype << 16) |
                     (fixed ? ((uint32_t)o.spe_index << 11) : 0u));
    return true;
}
