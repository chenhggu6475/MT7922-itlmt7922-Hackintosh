// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// itl_ba.h - 802.11 Block Ack helpers with no OS dependencies (compiles in the kext and in the host test, test/ba_test.cpp):
//   * the receive reorder buffer (port of the algorithm in Linux mt76 agg-rx.c: window, hole flush on timeout, BAR),
//   * ADDBA request / response, DELBA and BlockAckReq frame bodies (IEEE 802.11-2020 9.6.5, 9.3.1.8).
//
// The MT7922 acknowledges A-MPDUs in hardware once the firmware has been told about the agreement (STA_REC_BA); it does not
// reorder. Frames come out of the AP's retransmissions in any order, so the host has to put them back in sequence.
#ifndef ITL_BA_H
#define ITL_BA_H

#include <stdint.h>
#include <stddef.h>

// ---- 12-bit sequence numbers ---------------------------------------------------------------------------------------------
static inline uint16_t itl_sn_add(uint16_t a, uint16_t b) { return (uint16_t)((a + b) & 0xfff); }
static inline uint16_t itl_sn_sub(uint16_t a, uint16_t b) { return (uint16_t)((a - b) & 0xfff); }
static inline uint16_t itl_sn_inc(uint16_t a) { return (uint16_t)((a + 1) & 0xfff); }
static inline bool itl_sn_less(uint16_t a, uint16_t b) { return ((uint16_t)((a - b) & 0xfff)) > 0x800; }

// ---- RX reorder buffer ---------------------------------------------------------------------------------------------------
#define ITL_BA_MAX_WIN    64          // buffer size we offer (must be a power of two and divide 4096)
#define ITL_BA_SLOT_BYTES 4352        // one MPDU: up to 3895 bytes of A-MSDU (VHT max MPDU) as Ethernet frames, each with a 2-byte length

struct itl_ba_slot {
    uint8_t used;                     // slot holds an MPDU (len 0: a QoS-Null that only occupies its sequence number)
                                      // The reorder code treats the bytes as opaque; the driver stores a bundle (see below).
    uint16_t len;
    uint32_t t;                       // arrival time (ms)
    uint8_t d[ITL_BA_SLOT_BYTES];
};

struct itl_reorder {
    itl_ba_slot *slot;                // `size` entries, owned by the caller
    uint16_t head;                    // next sequence number to release
    uint16_t size;                    // window size, a power of two <= ITL_BA_MAX_WIN
    uint16_t nframes;                 // slots in use
    bool active;
    // Sequence numbers the window moved past without having received them (hole flushed on timeout, BAR, window slide).
    // A frame that arrives later for one of them is delivered out of order instead of being dropped as old: it is usually the
    // retransmission that fills the hole, and TCP copes with reordering far better than with loss.
    uint8_t missed[4096 / 8];
    // statistics
    uint32_t nIn, nDirect, nBuffered, nDup, nOld, nFlushed, nTimeout, nBar, nTooBig, nLate;
};

typedef void (*itl_ba_deliver_fn)(void *ctx, const uint8_t *frame, uint32_t len);

// Largest valid window size <= min(requested, ITL_BA_MAX_WIN); a request of 0 means "no preference".
uint16_t itl_reorder_size(uint16_t requested);

// slots must point at `size` zeroed itl_ba_slot. ssn is the starting sequence number of the agreement.
void itl_reorder_start(itl_reorder *r, itl_ba_slot *slots, uint16_t ssn, uint16_t size);
// An MPDU arrived (data frame, len > 0) or a QoS-Null (len == 0). Delivers in order through `fn`.
void itl_reorder_rx(itl_reorder *r, uint16_t sn, const uint8_t *frame, uint32_t len, uint32_t nowMs, itl_ba_deliver_fn fn, void *ctx);
// BlockAckReq: the originator gave up on everything before ssn.
void itl_reorder_bar(itl_reorder *r, uint16_t ssn, itl_ba_deliver_fn fn, void *ctx);
// Releases frames that have waited behind a hole for >= timeoutMs.
void itl_reorder_tick(itl_reorder *r, uint32_t nowMs, uint32_t timeoutMs, itl_ba_deliver_fn fn, void *ctx);
// Ends the agreement and releases whatever is buffered, in order.
void itl_reorder_stop(itl_reorder *r, itl_ba_deliver_fn fn, void *ctx);

// ---- bundles --------------------------------------------------------------------------------------------------------------
// What the driver hands to the reorder buffer for one MPDU: the Ethernet frames it carries (one, or several for an A-MSDU),
// each preceded by its length (2 bytes, little endian). An A-MSDU must be released as a unit, in sequence with the other MPDUs.
// Returns the new used size, or `used` unchanged when the frame does not fit in `cap` bytes or is empty / larger than 65535.
uint32_t itl_bundle_add(uint8_t *buf, uint32_t cap, uint32_t used, const uint8_t *frame, uint32_t len);
// Calls fn for every frame of a bundle (stops at a truncated entry).
void itl_bundle_each(const uint8_t *blob, uint32_t len, itl_ba_deliver_fn fn, void *ctx);

// ---- A-MSDU ---------------------------------------------------------------------------------------------------------------
// Subframes: DA(6) SA(6) length(2, big endian) LLC/SNAP + payload (length bytes), the whole padded to a multiple of 4 bytes except the
// last. `b` points at the first subframe (after the QoS header and, for a subframe the hardware split off, a 2-byte pad).
typedef void (*itl_amsdu_fn)(void *ctx, const uint8_t *da, const uint8_t *sa, uint16_t ethertype, const uint8_t *payload, uint32_t plen);
// Calls fn for every well-formed subframe (LLC/SNAP header present, length consistent) starting at `b`, stops at the first
// malformed one. Returns the number of subframes found. Subframes whose payload exceeds maxPayload are counted but not reported.
uint32_t itl_amsdu_each(const uint8_t *b, uint32_t n, uint32_t maxPayload, itl_amsdu_fn fn, void *ctx);

// ---- Block Ack action frames and BAR -------------------------------------------------------------------------------------
#define ITL_BA_ACT_ADDBA_REQ  0
#define ITL_BA_ACT_ADDBA_RESP 1
#define ITL_BA_ACT_DELBA      2
#define ITL_STATUS_SUCCESS    0
#define ITL_STATUS_DECLINED   37
#define ITL_REASON_QSTA_LEAVE 36      // "STA is leaving" - the usual DELBA reason

struct itl_ba_action {
    uint8_t action;                   // ITL_BA_ACT_*
    uint8_t token;                    // ADDBA dialog token
    uint16_t status;                  // ADDBA response status
    bool amsdu;                       // A-MSDU supported
    bool immediate;                   // immediate Block Ack policy
    uint8_t tid;
    uint16_t bufsize;
    uint16_t timeout;
    uint16_t ssn;                     // ADDBA request starting sequence number
    bool initiator;                   // DELBA: sent by the originator of the agreement
    uint16_t reason;                  // DELBA reason
};

// `body` starts at the Category byte (right after the 24-byte management header). Returns false for anything that is not a
// well-formed Block Ack action (category 3, ADDBA request / response, DELBA).
bool itl_ba_action_parse(const uint8_t *body, size_t len, itl_ba_action *a);

// Builders write the body starting at the Category byte and return its size.
size_t itl_addba_req_build(uint8_t *b, uint8_t token, uint8_t tid, uint16_t bufsize, uint16_t ssn, uint16_t timeout, bool amsdu);
size_t itl_addba_resp_build(uint8_t *b, uint8_t token, uint16_t status, uint8_t tid, uint16_t bufsize, bool amsdu, uint16_t timeout);
size_t itl_delba_build(uint8_t *b, uint8_t tid, bool initiator, uint16_t reason);

// BlockAckReq control frame (basic or compressed variant), `f` starts at the frame control. Returns false if it is not a
// single-TID BAR addressed to `ra` and sent by `ta`.
bool itl_bar_parse(const uint8_t *f, size_t len, const uint8_t ra[6], const uint8_t ta[6], uint8_t *tid, uint16_t *ssn);

#endif
