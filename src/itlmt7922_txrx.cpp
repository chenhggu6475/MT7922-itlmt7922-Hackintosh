// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors; MediaTek Inc. protocol references.
// MT7922 / Connac2 TXWI, shared hardware TXP, deterministic raw 802.11 RX.
#include "itlmt7922.hpp"
#include "mt7922_regs.h"
#include "../codec/mt7922_rx_codec.h"
#include "../codec/mt7922_data_codec.h"
#include "../codec/mt7922_tx_codec.h"
#include <string.h>
#include <libkern/libkern.h>

namespace {
void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
void barrier() { __sync_synchronize(); }
struct TxLock {
    IOLock *lock;
    explicit TxLock(IOLock *l) : lock(l) { if (lock) IOLockLock(lock); }
    ~TxLock() { if (lock) IOLockUnlock(lock); }
};
uint16_t basicRate(uint8_t channel, uint8_t basicCck, uint8_t basicOfdm)
{
    if (channel <= 14 && basicCck) {
        for (unsigned i = 0; i < 4; i++) if (basicCck & (1u << i)) return (uint16_t)i;
    }
    static const uint8_t ofdm[] = {11, 15, 10, 14, 9, 13, 8, 12};
    for (unsigned i = 0; i < 8; i++)
        if (basicOfdm & (1u << i)) return (uint16_t)((1u << 6) | ofdm[i]);
    return (1u << 6) | 11;
}
bool isEapol(const uint8_t *f, uint32_t len)
{
    uint32_t hl = 24 + ((f[0] & 0x80) ? 2u : 0u);
    if ((f[0] & 0x80) && (f[1] & 0x80)) hl += 4;
    static const uint8_t llc[] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e};
    return len >= hl + sizeof(llc) && !memcmp(f + hl, llc, sizeof(llc));
}
}

bool itlmt7922::m3Tx(const uint8_t *frame, uint32_t len, uint8_t type,
                     uint8_t subtype, const M3TxOpt &o)
{
    if (!frame || len > 1900 || len < 24 || !txLock || shouldStop()) return false;
    if (((frame[0] >> 2) & 3) != type || (frame[0] >> 4) != subtype) return false;
    TxLock lock(txLock);
    // A callback may have waited while quiesceHardware() stopped/freed DMA.
    if (shouldStop() || firmwareFault || !txData.n || !txData.desc.va || !txData.buf.va ||
        txData.head < 0 || txData.head >= txData.n) return false;
    // o.full marks network-stack output; the worker's handshake sends EAPOL
    // without it. A callback queued before teardown must not send even EAPOL
    // into a new association attempt after waiting for this lock.
    if (type == 2 && !m3linkUp && (o.full || !isEapol(frame, len))) return false;
    if ((txData.head + 1) % txData.n == (int)(rd(txData.regs + Q_DMA_IDX) & 0xffff)) {
        stTxNoSlot++;
        if (o.full) { *o.full = true; ethStalled = true; ethStallMs = nowMs(); }
        return false;
    }
    uint32_t slot;
    uint16_t token;
    if (!txSlotAlloc(&slot, &token)) {
        stTxNoSlot++;
        if (o.full) { *o.full = true; ethStalled = true; ethStallMs = nowMs(); }
        return false;
    }
    const size_t base = (size_t)slot * 2048;
    if (base > txData.buf.size || 128 + len > txData.buf.size - base) {
        txClrBusy(slot);
        return false;
    }
    uint8_t *w = txData.buf.va + base;
    const uint64_t pa = txData.buf.pa + base;
    Mt7922TxOptions options = {};
    options.wlan_index = 1; // To-DS multicast also uses the AP WCID.
    options.fixed_rate = basicRate(tgtCh, apBasicCck, apBasic);
    options.sequence = (int16_t)o.sn;
    options.queue = o.rateAdapt && type == 2 ? 1 : o.qidx; // AC_BE -> LMAC AC01, independent of BA level.
    options.tid = o.tid & 7;
    options.spe_index = 24; // Connac2 duplicate WF0+WF1 fixed-rate path.
    options.rate_adapt = o.rateAdapt;
    options.protect = o.protect;
    options.aggregate = o.aggr;
    bzero(w, 128);
    if (!mt7922_encode_txwi(w, frame, len, options)) { txClrBusy(slot); return false; }
    memcpy(w + 128, frame, len);
    // Shared mt76_connac_write_hw_txp layout: 32-byte HW TXP after TXWI.
    put16(w + 32, (uint16_t)(token | 0x8000));
    put32(w + 40, (uint32_t)(pa + 128));
    put16(w + 44, (uint16_t)(len | 0x8000));
    barrier();
    uint8_t *desc = txData.desc.va + txData.head * 16;
    put32(desc, (uint32_t)pa);
    put32(desc + 8, 0); put32(desc + 12, 0);
    put32(desc + 4, MT_DMA_CTL_SD_LEN0(64) | MT_DMA_CTL_LAST_SEC0);
    txData.head = (txData.head + 1) % txData.n;
    barrier();
    wr(txData.regs + Q_CPU_IDX, (uint32_t)txData.head);
    // Payload ownership ends at TXFREE, not at DMA descriptor consumption.
    return true;
}

bool itlmt7922::m3RxMgmt(const uint8_t *p, uint32_t len)
{
    Mt7922RxView view = {};
    if (mt7922_parse_rx_normal(p, len, &view) != MT7922_RX_OK ||
        view.frame_length < 24 || m3rxLen || !m3wantFc) return false;
    const uint8_t *f = p + view.frame_offset;
    if (f[0] != m3wantFc || (f[1] & 0x47) || (f[22] & 15) || view.fragmented || memcmp(f + 4, mac, 6) ||
        memcmp(f + 10, tgtBssid, 6) || memcmp(f + 16, tgtBssid, 6)) return false;
    uint32_t n = view.frame_length;
    if (n > sizeof(m3rx)) n = sizeof(m3rx);
    memcpy(m3rx, f, n);
    m3rxLen = n;
    return true;
}

bool itlmt7922::m3RxData(const uint8_t *p, uint32_t len)
{
    Mt7922RxView rx = {};
    if (mt7922_parse_rx_normal(p, len, &rx) != MT7922_RX_OK) {
        stRxDecodeReject++;
        return false;
    }
    if (rx.frame_length < 24) return false; // valid short control frames are not data decode failures.
    const uint8_t *f = p + rx.frame_offset;
    if ((f[0] & 15) != 8 || (f[1] & 3) != 2 || memcmp(f + 10, tgtBssid, 6)) return false;
    const bool group = f[4] & 1;
    if (!group && memcmp(f + 4, mac, 6)) return false;
    if (!group && !(f[0] & 0x40)) {
        // Record actual unicast data vectors, excluding beacons/multicast basic
        // rates and null frames. Negotiated maximum rate is a different value.
        if (rx.phy_valid) {
            rxPhyValid = true;
            rxPhyMode = rx.phy_mode; rxPhyMcs = rx.phy_mcs;
            rxPhyNss = rx.phy_nss; rxPhyBw = rx.phy_bw; rxPhyGi = rx.phy_gi;
            rxPhySamples++; rxPhyModeCounts[rx.phy_mode & 15]++;
        }
        int16_t best = -128;
        for (unsigned i = 0; i < 2; i++)
            if ((rx.chain_valid_mask & (1u << i)) && rx.chain_rssi_dbm[i] <= 0 &&
                rx.chain_rssi_dbm[i] >= -110 && rx.chain_rssi_dbm[i] > best)
                best = rx.chain_rssi_dbm[i];
        if (best != -128) tgtRssi = tgtRssi < 0 ? (int8_t)((tgtRssi * 3 + best) / 4) : (int8_t)best;
    }
    Mt7922DataView data = {};
    Mt7922DataStatus status = mt7922_parse_data(f, rx.frame_length, rx, &data);
    if (status != MT7922_DATA_OK && status != MT7922_DATA_NULL && status != MT7922_DATA_AMSDU) {
        stRxDrop++;
        if (status == MT7922_DATA_CIPHER) stRxCipherReject++;
        else if (status == MT7922_DATA_FRAGMENT) stRxFragmentDrop++;
        else stRxMalformed++;
        if (rxdLogged < 6) {
            rxdLogged++;
            baLog("RX reject status=%u sec=%u done=%u cm=%u frag=%u fc=%02x%02x len=%u", status,
                  rx.security_mode, rx.security_done, rx.cipher_mismatch, rx.fragmented,
                  f[0], f[1], rx.frame_length);
        }
        return true;
    }
    const uint8_t tid = data.tid;
    const uint16_t sn = data.sequence;
    const bool reorder = data.qos && !group && rxBa[tid].r.active &&
        (((data.qos_control >> 5) & 3) == 0 || ((data.qos_control >> 5) & 3) == 3);
    szRing[szClass(data.body_length)]++;
    if (status == MT7922_DATA_AMSDU) {
        // Classification precedes policy/security/link gates. Version0.2.0
        // silently dropped these at level1 while leaving its A-MSDU count zero.
        stAmsduPkt++;
        stAmsduAi[rx.payload_format & 3]++;
    }
    if (data.qos && !group && status != MT7922_DATA_NULL &&
        !(status == MT7922_DATA_AMSDU && rx.payload_format && rx.payload_format != 3)) {
        stRxPkt++;
        if (data.retry) stRxRetry++;
    }
    if (status == MT7922_DATA_NULL) {
        amsduFlush();
        if (reorder) itl_reorder_rx(&rxBa[tid].r, sn, nullptr, 0, nowMs(), baDeliverCb, this);
        return true;
    }
    // Protected ciphertext is rejected before LLC parsing; cleartext is only
    // admitted for EAPOL during the WPA2 handshake, never as IP/DHCP/ARP.
    if (!data.protected_frame && (status == MT7922_DATA_AMSDU || data.ethertype != 0x888e)) {
        stRxDrop++;
        stRxCipherReject++;
        return true;
    }
    if (status == MT7922_DATA_AMSDU) {
        // A-MSDU reception is an HT/VHT data-format requirement independent of
        // the A-MSDU-in-A-MPDU negotiation policy controlled by fastAmsdu.
        if (!m3linkUp) { stRxDrop++; stRxDirectDrop++; return true; }
        uint32_t expected = 0;
        const Mt7922AmsduStatus validity = mt7922_validate_amsdu(f + data.body_offset,
            data.body_length, mac, 1600, &expected);
        if (validity != MT7922_AMSDU_OK) {
            stRxDrop++; stAmsduBad++;
            if (validity == MT7922_AMSDU_LIMIT) stRxAmsduLimit++;
            else if (validity == MT7922_AMSDU_ADDRESS) stRxDirectDrop++;
            else stRxMalformed++;
            return true;
        }
        // Decoder removed the exact Connac2 A-MSDU padding; helper gets raw body.
        const uint32_t n = m3RxAmsdu(f + data.body_offset, data.body_length, rx.payload_format, reorder, tid, sn);
        stAmsduSub += n;
        stAmsduN[n > 4 ? 4 : n]++;
        if (n) { stRxData += n; lastRxDataMs = nowMs(); }
        if (n < expected) stRxDirectDrop += expected - n; // validated self-source echoes are suppressed by the callback.
        return true;
    }
    if (!group && !reorder && m3linkUp) {
        const uint8_t index = data.qos ? tid : 8;
        if (data.retry && ((rxLastValid >> index) & 1) && rxLastSn[index] == sn) {
            stRxDupL++;
            return true;
        }
        rxLastSn[index] = sn;
        rxLastValid |= (uint16_t)(1u << index);
    }
    stRxData++;
    lastRxDataMs = nowMs();
    const uint8_t *body = f + data.body_offset;
    if (data.ethertype == 0x888e) {
        stRxEapol++;
        if (m3wantEapol) m3Eapol(body, data.body_length);
        return true;
    }
    if (!m3linkUp || data.body_length > 1600) { stRxDrop++; stRxDirectDrop++; return true; }
    if (!memcmp(f + 16, mac, 6)) { stRxDirectDrop++; return true; }
    uint8_t ethernet[14 + 1600];
    memcpy(ethernet, f + 4, 6);
    memcpy(ethernet + 6, f + 16, 6);
    ethernet[12] = (uint8_t)(data.ethertype >> 8);
    ethernet[13] = (uint8_t)data.ethertype;
    memcpy(ethernet + 14, body, data.body_length);
    if (!group) stRxUni++;
    rxStore(ethernet, 14 + data.body_length, reorder, tid, sn, true);
    return true;
}
