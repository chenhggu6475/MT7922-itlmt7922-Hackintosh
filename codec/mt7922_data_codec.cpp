// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "mt7922_data_codec.h"
#include <string.h>

Mt7922DataStatus mt7922_parse_data(const uint8_t *p, uint32_t n,
                                   const Mt7922RxView &rx, Mt7922DataView *out)
{
    if (!p || !out || n < 24) return MT7922_DATA_SHORT;
    if ((p[0] & 15) != 8 || (p[1] & 3) != 2) return MT7922_DATA_FORMAT;
    if (rx.fragmented || (p[1] & 4) || (p[22] & 15)) return MT7922_DATA_FRAGMENT;
    Mt7922DataView d = {};
    d.protected_frame = (p[1] >> 6) & 1;
    if (d.protected_frame &&
        (rx.security_mode != 4 || rx.cipher_mismatch))
        return MT7922_DATA_CIPHER;
    d.qos = (p[0] >> 7) & 1;
    uint32_t off = 24;
    if (d.qos) {
        if (n - off < 2) return MT7922_DATA_SHORT;
        d.qos_control = p[24];
        d.tid = d.qos_control & 15;
        if (d.tid > 7) return MT7922_DATA_FORMAT;
        off += 2;
        if (p[1] & 0x80) {
            if (n - off < 4) return MT7922_DATA_SHORT;
            off += 4;
        }
    }
    d.sequence = (uint16_t)(((uint16_t)p[22] | ((uint16_t)p[23] << 8)) >> 4);
    d.retry = (p[1] >> 3) & 1;
    if (p[0] & 0x40) {
        d.body_offset = off;
        d.body_length = n - off;
        *out = d;
        return MT7922_DATA_NULL;
    }
    if (rx.payload_format) {
        // Connac2 A-MSDU deaggregation inserts two bytes after the MAC header.
        if (!d.qos || !(d.qos_control & 0x80) || n - off < 2) return MT7922_DATA_FORMAT;
        off += 2;
    }
    if (d.qos && (d.qos_control & 0x80)) {
        d.body_offset = off;
        d.body_length = n - off;
        *out = d;
        return MT7922_DATA_AMSDU;
    }
    if (n - off < 8) return MT7922_DATA_SHORT;
    const uint8_t snap[5] = {0xaa, 0xaa, 3, 0, 0};
    if (memcmp(p + off, snap, sizeof(snap)) || (p[off + 5] != 0 && p[off + 5] != 0xf8))
        return MT7922_DATA_FORMAT;
    d.ethertype = (uint16_t)(((uint16_t)p[off + 6] << 8) | p[off + 7]);
    if (d.ethertype < 0x0600) return MT7922_DATA_FORMAT;
    d.body_offset = off + 8;
    d.body_length = n - d.body_offset;
    *out = d;
    return MT7922_DATA_OK;
}

Mt7922AmsduStatus mt7922_validate_amsdu(const uint8_t *p, uint32_t n,
                                       const uint8_t local[6], uint32_t maximum,
                                       uint32_t *subframes)
{
    if (subframes) *subframes = 0;
    if (!p || !local || !subframes || n < 22) return MT7922_AMSDU_MALFORMED;
    uint32_t off = 0, count = 0;
    while (off < n) {
        if (n - off < 22) return MT7922_AMSDU_MALFORMED;
        const uint8_t *f = p + off;
        const uint32_t length = ((uint32_t)f[12] << 8) | f[13];
        if (length < 8 || length > n - off - 14) return MT7922_AMSDU_MALFORMED;
        if (length > 2304 || length - 8 > maximum || count >= 128) return MT7922_AMSDU_LIMIT;
        if (!(f[0] & 1) && memcmp(f, local, 6)) return MT7922_AMSDU_ADDRESS;
        if ((f[6] & 1) || !(f[6] | f[7] | f[8] | f[9] | f[10] | f[11])) return MT7922_AMSDU_ADDRESS;
        const uint8_t *llc = f + 14;
        if (llc[0] != 0xaa || llc[1] != 0xaa || llc[2] != 3 || llc[3] || llc[4] ||
            (llc[5] != 0 && llc[5] != 0xf8) || (((uint32_t)llc[6] << 8) | llc[7]) < 0x0600)
            return MT7922_AMSDU_MALFORMED;
        count++;
        const uint32_t used = 14 + length;
        off += used;
        if (n - off <= 3) {
            // Final alignment padding is allowed, but no partial second header.
            *subframes = count;
            return MT7922_AMSDU_OK;
        }
        const uint32_t padding = (4 - (used & 3)) & 3;
        if (padding > n - off) return MT7922_AMSDU_MALFORMED;
        off += padding;
    }
    return MT7922_AMSDU_MALFORMED;
}
