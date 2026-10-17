// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// itl_ba.cpp - see itl_ba.h. Plain C++, no OS calls; also built by test/ba_test.cpp on a normal host.
// The reorder logic follows Linux mt76 agg-rx.c (mt76_rx_aggr_reorder, _release_frames, _release_head, the reorder work).
#include "itl_ba.h"
#include <string.h>

static inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// ---------------------------------------------------------------------------------------------------------------------------
// reorder buffer
// ---------------------------------------------------------------------------------------------------------------------------
uint16_t itl_reorder_size(uint16_t requested)
{
    uint16_t want = (requested == 0 || requested > ITL_BA_MAX_WIN) ? ITL_BA_MAX_WIN : requested;
    uint16_t n = 1;
    while ((uint16_t)(n * 2) <= want) n = (uint16_t)(n * 2);
    return n;
}

void itl_reorder_start(itl_reorder *r, itl_ba_slot *slots, uint16_t ssn, uint16_t size)
{
    memset(r, 0, sizeof(*r));
    r->slot = slots;
    r->size = size;
    r->head = (uint16_t)(ssn & 0xfff);
    r->active = true;
}

static inline void missed_set(itl_reorder *r, uint16_t sn, bool on)
{
    uint8_t bit = (uint8_t)(1u << (sn & 7));
    if (on) r->missed[sn >> 3] |= bit;
    else r->missed[sn >> 3] &= (uint8_t)~bit;
}
static inline bool missed_get(const itl_reorder *r, uint16_t sn) { return (r->missed[sn >> 3] >> (sn & 7)) & 1; }

static void release_one(itl_reorder *r, uint16_t idx, itl_ba_deliver_fn fn, void *ctx)
{
    itl_ba_slot *s = &r->slot[idx];
    if (!s->used) return;
    s->used = 0;
    r->nframes--;
    if (s->len) fn(ctx, s->d, s->len);
}

// Releases everything in [head, to) in order and moves the window to `to`.
static void release_frames(itl_reorder *r, uint16_t to, itl_ba_deliver_fn fn, void *ctx)
{
    uint16_t sn = r->head;
    while (itl_sn_less(sn, to)) {
        uint16_t before = r->nframes;
        if (before) release_one(r, (uint16_t)(sn % r->size), fn, ctx);
        if (r->nframes != before) { r->nFlushed++; missed_set(r, sn, false); }
        else missed_set(r, sn, true);                                  // passed over without the frame
        sn = itl_sn_inc(sn);
    }
    r->head = to;
}

// Releases the run of consecutive frames that starts at the window head.
static void release_head(itl_reorder *r, itl_ba_deliver_fn fn, void *ctx)
{
    while (r->slot[r->head % r->size].used) {
        release_one(r, (uint16_t)(r->head % r->size), fn, ctx);
        missed_set(r, r->head, false);
        r->head = itl_sn_inc(r->head);
    }
}

void itl_reorder_rx(itl_reorder *r, uint16_t sn, const uint8_t *frame, uint32_t len, uint32_t nowMs, itl_ba_deliver_fn fn, void *ctx)
{
    sn &= 0xfff;
    r->nIn++;
    if (!r->active) {
        if (len) fn(ctx, frame, len);
        return;
    }
    if (itl_sn_less(sn, r->head)) {                                    // behind the window
        if (missed_get(r, sn)) {                                       // a hole we gave up on: late, but still the only copy
            missed_set(r, sn, false);
            r->nLate++;
            if (len) fn(ctx, frame, len);
        } else {
            r->nOld++;                                                 // already delivered: a duplicate
        }
        return;
    }

    if (sn == r->head) {                                               // the one we are waiting for
        missed_set(r, sn, false);
        r->head = itl_sn_inc(r->head);
        r->nDirect++;
        if (len) fn(ctx, frame, len);
        release_head(r, fn, ctx);
        return;
    }

    if (!itl_sn_less(sn, itl_sn_add(r->head, r->size)))               // beyond the window: slide it so that sn is its last entry
        release_frames(r, itl_sn_inc(itl_sn_sub(sn, r->size)), fn, ctx);

    itl_ba_slot *s = &r->slot[sn % r->size];
    if (s->used) { r->nDup++; return; }
    if (len > ITL_BA_SLOT_BYTES) { r->nTooBig++; len = 0; }           // cannot keep it: keep its place in the sequence only
    s->used = 1;
    s->len = (uint16_t)len;
    s->t = nowMs;
    if (len) memcpy(s->d, frame, len);
    r->nframes++;
    r->nBuffered++;
    release_head(r, fn, ctx);
}

void itl_reorder_bar(itl_reorder *r, uint16_t ssn, itl_ba_deliver_fn fn, void *ctx)
{
    ssn &= 0xfff;
    if (!r->active) return;
    r->nBar++;
    if (itl_sn_less(ssn, r->head)) return;
    release_frames(r, ssn, fn, ctx);
    release_head(r, fn, ctx);
}

void itl_reorder_tick(itl_reorder *r, uint32_t nowMs, uint32_t timeoutMs, itl_ba_deliver_fn fn, void *ctx)
{
    if (!r->active || !r->nframes) return;
    uint16_t h0 = r->head, left = r->nframes;
    for (uint16_t i = 0; i < r->size && left; i++) {
        uint16_t sn = (uint16_t)((h0 + i) & 0xfff);
        itl_ba_slot *s = &r->slot[sn % r->size];
        if (!s->used) continue;
        left--;
        if ((uint32_t)(nowMs - s->t) < timeoutMs) break;               // the oldest frame has not waited long enough yet
        r->nTimeout++;
        release_frames(r, itl_sn_inc(sn), fn, ctx);
    }
    release_head(r, fn, ctx);
}

void itl_reorder_stop(itl_reorder *r, itl_ba_deliver_fn fn, void *ctx)
{
    if (!r->active) return;
    for (uint16_t i = 0; i < r->size && r->nframes; i++) release_one(r, (uint16_t)((r->head + i) % r->size), fn, ctx);
    r->active = false;
}

// ---------------------------------------------------------------------------------------------------------------------------
// bundles
// ---------------------------------------------------------------------------------------------------------------------------
uint32_t itl_bundle_add(uint8_t *buf, uint32_t cap, uint32_t used, const uint8_t *frame, uint32_t len)
{
    if (!len || len > 0xffff || used + 2 + len > cap) return used;
    put16(buf + used, (uint16_t)len);
    memcpy(buf + used + 2, frame, len);
    return used + 2 + len;
}

void itl_bundle_each(const uint8_t *blob, uint32_t len, itl_ba_deliver_fn fn, void *ctx)
{
    uint32_t o = 0;
    while (o + 2 <= len) {
        uint32_t l = le16(blob + o);
        if (!l || o + 2 + l > len) return;
        fn(ctx, blob + o + 2, l);
        o += 2 + l;
    }
}

// ---------------------------------------------------------------------------------------------------------------------------
// A-MSDU
// ---------------------------------------------------------------------------------------------------------------------------
uint32_t itl_amsdu_each(const uint8_t *b, uint32_t n, uint32_t maxPayload, itl_amsdu_fn fn, void *ctx)
{
    uint32_t o = 0, count = 0;
    while (o + 14 + 8 <= n) {
        uint32_t L = (uint32_t)((b[o + 12] << 8) | b[o + 13]);
        const uint8_t *l = b + o + 14;
        if (L < 8 || L > 2304 || o + 14 + L > n) break;
        if (l[0] != 0xaa || l[1] != 0xaa || l[2] != 0x03) break;       // not LLC/SNAP: no way to find the ethertype
        count++;
        if (L - 8 <= maxPayload) fn(ctx, b + o, b + o + 6, (uint16_t)((l[6] << 8) | l[7]), l + 8, L - 8);
        o += (14 + L + 3) & ~3u;
    }
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------------
// action frames
// ---------------------------------------------------------------------------------------------------------------------------
bool itl_ba_action_parse(const uint8_t *b, size_t len, itl_ba_action *a)
{
    memset(a, 0, sizeof(*a));
    if (len < 2 || b[0] != 3) return false;                            // category: Block Ack
    a->action = b[1];
    switch (b[1]) {
    case ITL_BA_ACT_ADDBA_REQ: {                                       // token, parameters, timeout, starting sequence control
        if (len < 9) return false;
        uint16_t p = le16(b + 3);
        a->token = b[2];
        a->amsdu = p & 1;
        a->immediate = (p >> 1) & 1;
        a->tid = (uint8_t)((p >> 2) & 0xf);
        a->bufsize = (uint16_t)(p >> 6);
        a->timeout = le16(b + 5);
        a->ssn = (uint16_t)(le16(b + 7) >> 4);
        return a->tid < 8;
    }
    case ITL_BA_ACT_ADDBA_RESP: {                                      // token, status, parameters, timeout
        if (len < 9) return false;
        uint16_t p = le16(b + 5);
        a->token = b[2];
        a->status = le16(b + 3);
        a->amsdu = p & 1;
        a->immediate = (p >> 1) & 1;
        a->tid = (uint8_t)((p >> 2) & 0xf);
        a->bufsize = (uint16_t)(p >> 6);
        a->timeout = le16(b + 7);
        return a->tid < 8;
    }
    case ITL_BA_ACT_DELBA: {                                           // parameters (initiator bit 11, TID bits 12-15), reason
        if (len < 6) return false;
        uint16_t p = le16(b + 2);
        a->initiator = (p >> 11) & 1;
        a->tid = (uint8_t)(p >> 12);
        a->reason = le16(b + 4);
        return a->tid < 8;
    }
    default:
        return false;
    }
}

size_t itl_addba_req_build(uint8_t *b, uint8_t token, uint8_t tid, uint16_t bufsize, uint16_t ssn, uint16_t timeout, bool amsdu)
{
    b[0] = 3; b[1] = ITL_BA_ACT_ADDBA_REQ; b[2] = token;
    put16(b + 3, (uint16_t)((amsdu ? 1 : 0) | (1 << 1) | ((tid & 0xf) << 2) | ((bufsize & 0x3ff) << 6)));
    put16(b + 5, timeout);
    put16(b + 7, (uint16_t)((ssn & 0xfff) << 4));
    return 9;
}

size_t itl_addba_resp_build(uint8_t *b, uint8_t token, uint16_t status, uint8_t tid, uint16_t bufsize, bool amsdu, uint16_t timeout)
{
    b[0] = 3; b[1] = ITL_BA_ACT_ADDBA_RESP; b[2] = token;
    put16(b + 3, status);
    put16(b + 5, (uint16_t)((amsdu ? 1 : 0) | (1 << 1) | ((tid & 0xf) << 2) | ((bufsize & 0x3ff) << 6)));
    put16(b + 7, timeout);
    return 9;
}

size_t itl_delba_build(uint8_t *b, uint8_t tid, bool initiator, uint16_t reason)
{
    b[0] = 3; b[1] = ITL_BA_ACT_DELBA;
    put16(b + 2, (uint16_t)(((initiator ? 1 : 0) << 11) | ((tid & 0xf) << 12)));
    put16(b + 4, reason);
    return 6;
}

bool itl_bar_parse(const uint8_t *f, size_t len, const uint8_t ra[6], const uint8_t ta[6], uint8_t *tid, uint16_t *ssn)
{
    if (len < 20 || f[0] != 0x84) return false;                        // control frame, BlockAckReq
    if (memcmp(f + 4, ra, 6) != 0 || memcmp(f + 10, ta, 6) != 0) return false;
    uint16_t ctl = le16(f + 16);
    if (ctl & 0x2) return false;                                       // multi-TID variant: not used by anyone in practice
    *tid = (uint8_t)(ctl >> 12);
    *ssn = (uint16_t)(le16(f + 18) >> 4);
    return *tid < 8;
}
