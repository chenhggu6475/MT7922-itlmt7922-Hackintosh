// SPDX-License-Identifier: BSD-3-Clause-Clear
// Layouts from mt76_connac_mcu.{c,h} and mt7921/mac.c, MediaTek Inc.
#include "mt7922_ba_codec.h"
#include <string.h>
namespace {
void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
uint32_t g32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void staHeader(uint8_t *p) { p[1] = 1; p[2] = 1; p[4] = 1; } // BSS0, WCID1, append one TLV, OMAC0
}
bool mt7922_encode_wtbl_ba(uint8_t out[48], const uint8_t peer[6], uint8_t tid,
                            bool recipient, bool enable, uint16_t ssn, uint16_t win)
{
    if (!out || !peer || tid > 7 || ssn > 4095 || win > 64) return false;
    memset(out, 0, 48); staHeader(out);
    p16(out + 8, 13); p16(out + 10, 40); // STA_REC_WTBL
    out[12] = 1; out[13] = 2; p16(out + 14, 1); // WCID1, WTBL_SET, one nested TLV
    uint8_t *t = out + 20;
    p16(t, 8); p16(t + 2, 28); // WTBL_BA
    t[4] = tid; t[5] = recipient ? 2 : 1;
    p16(t + 12, enable ? win : 0);
    if (recipient) {
        memcpy(t + 14, peer, 6); t[20] = tid; t[21] = 0; t[22] = 1;
    } else { p16(t + 8, enable ? ssn : 0); t[10] = enable; }
    return true;
}
bool mt7922_encode_sta_ba(uint8_t out[20], uint8_t tid, bool recipient,
                           bool enable, uint16_t ssn, uint16_t win, bool amsdu)
{
    if (!out || tid > 7 || ssn > 4095 || win > 64) return false;
    memset(out, 0, 20); staHeader(out);
    uint8_t *t = out + 8;
    p16(t, 6); p16(t + 2, 12);
    t[4] = tid; t[5] = recipient ? 2 : 1; t[6] = amsdu; t[7] = enable ? (uint8_t)(1u << tid) : 0;
    p16(t + 8, ssn); p16(t + 10, win);
    return true;
}
bool mt7922_tx_free_begin(const uint8_t *p, uint32_t len, Mt7922TxFreeCursor *c)
{
    if (!p || !c || len < 8 || (g32(p) >> 27) != 6) return false;
    const uint32_t declared = g32(p) & 0xffff;
    if (declared < 8 || declared > len) return false;
    c->packet = p; c->length = declared; c->offset = 8; c->count = 0;
    c->total = (g32(p) >> 16) & 0x3ff;
    return true;
}
bool mt7922_tx_free_next(Mt7922TxFreeCursor *c, Mt7922TxFreeRecord *r)
{
    if (!c || !r) return false;
    while (c->count < c->total && c->offset <= c->length && c->length - c->offset >= 4) {
        const uint32_t word = g32(c->packet + c->offset);
        c->offset += 4;
        if (word & 0x80000000u) continue;
        c->count++;
        r->token = (uint16_t)((word >> 16) & 0x7fff);
        r->attempts = (uint16_t)(word & 0x1fff);
        r->status = (uint8_t)((word >> 13) & 3);
        return true;
    }
    return false;
}
