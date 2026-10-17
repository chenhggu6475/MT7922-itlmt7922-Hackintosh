// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../codec/mt7922_data_codec.h"
#include "../codec/mt7922_tx_codec.h"
#include "../codec/mt7922_ba_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static std::vector<uint8_t> dataFrame(bool qos, bool protectedFrame)
{
    unsigned header = qos ? 26 : 24;
    std::vector<uint8_t> f(header + 8 + 4, 0);
    f[0] = qos ? 0x88 : 8; f[1] = protectedFrame ? 0x42 : 2;
    f[22] = 0xc0; f[23] = 0xab; // sequence 0xabc, fragment 0
    const uint8_t snap[] = {0xaa,0xaa,3,0,0,0,8,0,1,2,3,4};
    memcpy(f.data() + header, snap, sizeof(snap));
    return f;
}

static void test_tx_golden()
{
    uint8_t f[40] = {}, w[32] = {};
    f[0] = 0xb0; f[4] = 2;
    Mt7922TxOptions o = {};
    o.wlan_index = 1; o.queue = 16; o.fixed_rate = 0x4b; o.spe_index = 24; o.sequence = -1;
    assert(mt7922_encode_txwi(w, f, 30, o));
    // Linux Connac2 auth request, OFDM6, OMAC0/WCID1/ALTX0, 2-chain SPE.
    const uint32_t auth[] = {0x2000003e,0x80026001,0x8000200b,0x10007800,0,0x403,0x004b0004,0x000bc000};
    for (unsigned i = 0; i < 8; i++) assert(u32(w + 4*i) == auth[i]);
    memset(f, 0, sizeof(f)); f[0] = 0x88; f[1] = 0x41; f[4] = 2;
    o.queue = 1; o.tid = 5; o.sequence = 0xabc; o.rate_adapt = 1; o.protect = 1; o.aggregate = 1;
    assert(mt7922_encode_txwi(w, f, 34, o));
    const uint32_t qos[] = {0x02000042,0x80526801,0x28,0x8abc7802,0,0,0,0x00280000};
    for (unsigned i = 0; i < 8; i++) assert(u32(w + 4*i) == qos[i]);
    memset(f, 0, sizeof(f)); f[0] = 0xd0; f[4] = 2; f[24] = 3; f[25] = 0; f[27] = 12;
    o.rate_adapt = 0; o.protect = 0; o.tid = 2; o.sequence = -1;
    assert(mt7922_encode_txwi(w, f, 33, o));
    assert((u32(w + 4) & 0x00700000) == 0x00300000); // ADDBA body selects TID3
    assert((u32(w + 20) & 0x4000) != 0);
    o.wlan_index = 1024; assert(!mt7922_encode_txwi(w, f, 33, o));
    o.wlan_index = 1; o.tid = 8; assert(!mt7922_encode_txwi(w, f, 33, o));
    o.tid = 0; f[0] = 0x88; assert(!mt7922_encode_txwi(w, f, 25, o));
}

static void test_plaintext_and_cipher()
{
    Mt7922RxView rx = {};
    Mt7922DataView d = {};
    std::vector<uint8_t> f = dataFrame(false, false);
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_OK);
    assert(d.body_offset == 32 && d.body_length == 4 && d.ethertype == 0x800 && d.sequence == 0xabc);
    f[1] = 0x42; // A ciphertext packet containing a convincing SNAP prefix.
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_CIPHER);
    rx.security_mode = 4;
    // Upstream MT7922 does not require SEC_DONE; security mode and CM/CLM,
    // together with the descriptor's ICV/MIC rejection, establish plaintext.
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_OK);
    rx.security_done = 1;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_OK);
    for (unsigned bad = 1; bad < 4; ++bad) {
        rx.cipher_mismatch = bad;
        assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_CIPHER);
    }
    rx.cipher_mismatch = 0;
    rx.security_mode = 5; // WEP104 hardware mode is NOT a CCMP plaintext proof.
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_CIPHER);
    rx.security_mode = 4;
    f.insert(f.begin() + 24, 8, 0); // retained CCMP IV: no heuristic search ahead
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_FORMAT);
    f = dataFrame(false, true); f[1] |= 4;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_FRAGMENT);
    f[1] &= ~4; f[22] |= 1;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_FRAGMENT);
    f[22] &= 0xf0; rx.fragmented = 1;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_FRAGMENT);
}

static void test_qos_padding_and_bounds()
{
    Mt7922RxView rx = {};
    rx.security_mode = 4; rx.security_done = 1;
    Mt7922DataView d = {};
    std::vector<uint8_t> f = dataFrame(true, true);
    f[24] = 5;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_OK);
    assert(d.qos && d.tid == 5 && d.body_offset == 34);
    f[1] |= 0x80; f.insert(f.begin() + 26, 4, 0); // +HTC, exact SNAP offset
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_OK && d.body_offset == 38);
    f[24] = 15;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_FORMAT);
    f = dataFrame(true, true); f[24] = 0x80; rx.payload_format = 3;
    f.insert(f.begin() + 26, 2, 0); // normalized split A-MSDU starts at byte28
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_AMSDU && d.body_offset == 28);
    rx.payload_format = 0; f = dataFrame(true, true); f[0] = 0xc8;
    assert(mt7922_parse_data(f.data(), (uint32_t)f.size(), rx, &d) == MT7922_DATA_NULL);
    f = dataFrame(true, true);
    for (unsigned n = 0; n < 34; n++) {
        std::vector<uint8_t> shortFrame(f.begin(), f.begin() + n);
        assert(mt7922_parse_data(shortFrame.data(), n, rx, &d) != MT7922_DATA_OK);
    }
    uint32_t seed = 0x7922;
    for (unsigned n = 0; n < 4096; n++) {
        std::vector<uint8_t> bytes(n);
        for (uint8_t &b : bytes) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; b = seed; }
        Mt7922DataStatus s = mt7922_parse_data(bytes.data(), n, rx, &d);
        if (s == MT7922_DATA_OK || s == MT7922_DATA_NULL || s == MT7922_DATA_AMSDU)
            assert(d.body_offset <= n && d.body_length <= n - d.body_offset);
        if (n <= 65503) {
            uint8_t w[32]; Mt7922TxOptions o = {}; o.sequence = -1;
            (void)mt7922_encode_txwi(w, bytes.data(), n, o);
        }
    }
}

static void test_ba_and_txfree()
{
    const uint8_t peer[] = {2,0x11,0x22,0x33,0x44,0x55};
    uint8_t wtbl[48], sta[20];
    assert(mt7922_encode_wtbl_ba(wtbl, peer, 3, true, true, 0xabc, 64));
    // STA hdr8, WTBL TLV40, nested WTBL hdr8, WTBL_BA28. Recipient reset
    // matches the peer MAC + TID; no Connac3-only 16-byte STA BA tail exists.
    assert(wtbl[1] == 1 && wtbl[2] == 1 && wtbl[4] == 1);
    assert(wtbl[8] == 13 && wtbl[10] == 40 && wtbl[13] == 2 && wtbl[14] == 1);
    assert(wtbl[20] == 8 && wtbl[22] == 28 && wtbl[24] == 3 && wtbl[25] == 2);
    assert(wtbl[32] == 64 && !memcmp(wtbl + 34, peer, 6));
    assert(wtbl[40] == 3 && wtbl[41] == 0 && wtbl[42] == 1);
    assert(mt7922_encode_wtbl_ba(wtbl, peer, 0, false, true, 0xabc, 32));
    assert(wtbl[25] == 1 && wtbl[28] == 0xbc && wtbl[29] == 0x0a && wtbl[30] == 1 && wtbl[32] == 32);
    assert(mt7922_encode_sta_ba(sta, 3, true, true, 0xabc, 64, true));
    const uint8_t expectedSta[] = {0,1,1,0,1,0,0,0, 6,0,12,0, 3,2,1,8, 0xbc,0x0a,64,0};
    assert(!memcmp(sta, expectedSta, sizeof(sta)));
    assert(!mt7922_encode_sta_ba(sta, 8, true, true, 0, 64, false));
    // Two MSDUs and two WCID pair markers, no Connac3 version4/header words.
    const uint8_t freePacket[] = {
        24,0,2,0x30, 0,0,0,0,
        0,0x40,0,0x80, // WCID1 pair
        1,0,0x00,0x01, // token0x100, one attempt, success
        0,0x80,0,0x80, // WCID2 pair
        3,0x20,0x01,0x01 // token0x101, three attempts, status1
    };
    Mt7922TxFreeCursor cursor = {}; Mt7922TxFreeRecord r = {};
    assert(mt7922_tx_free_begin(freePacket, sizeof(freePacket), &cursor));
    assert(mt7922_tx_free_next(&cursor, &r) && r.token == 0x100 && r.attempts == 1 && !r.status);
    assert(mt7922_tx_free_next(&cursor, &r) && r.token == 0x101 && r.attempts == 3 && r.status == 1);
    assert(!mt7922_tx_free_next(&cursor, &r) && cursor.count == cursor.total);
    for (unsigned n = 0; n < sizeof(freePacket); n++)
        assert(!mt7922_tx_free_begin(freePacket, n, &cursor));
}

static std::vector<uint8_t> amsduSubframe(const uint8_t da[6], uint8_t protocolLow, uint32_t payload)
{
    std::vector<uint8_t> f(22 + payload, 0);
    const uint8_t sa[] = {2,0x22,0x33,0x44,0x55,0x66};
    memcpy(f.data(), da, 6); memcpy(f.data() + 6, sa, 6);
    f[12] = (uint8_t)((8 + payload) >> 8); f[13] = (uint8_t)(8 + payload);
    const uint8_t snap[] = {0xaa,0xaa,3,0,0,0,8,0};
    memcpy(f.data() + 14, snap, 8); f[21] = protocolLow;
    return f;
}
static void test_mandatory_amsdu()
{
    assert(mt7922_amsdu_complete(0)); // whole MPDU is complete immediately.
    assert(mt7922_amsdu_complete(1)); // last hardware-deaggregated subframe.
    assert(!mt7922_amsdu_complete(2));
    assert(!mt7922_amsdu_complete(3));
    const uint8_t local[] = {2,0x11,0x22,0x33,0x44,0x55};
    const uint8_t group[] = {1,0,0x5e,0,0,1};
    uint32_t count = 123;
    std::vector<uint8_t> first = amsduSubframe(local, 0, 5);
    std::vector<uint8_t> second = amsduSubframe(group, 6, 1500);
    std::vector<uint8_t> body = first;
    while (body.size() & 3) body.push_back(0);
    body.insert(body.end(), second.begin(), second.end());
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_OK && count == 2);
    // No final padding, or up to three final alignment bytes, stays bounded.
    for (unsigned padding = 1; padding <= 3; padding++) {
        std::vector<uint8_t> padded = body;
        padded.insert(padded.end(), padding, 0);
        assert(mt7922_validate_amsdu(padded.data(), (uint32_t)padded.size(), local, 1600, &count) == MT7922_AMSDU_OK && count == 2);
    }
    // Valid standalone and split forms do not depend on a BA/fast policy flag.
    Mt7922RxView rx = {}; rx.security_mode = 4;
    Mt7922DataView data = {};
    std::vector<uint8_t> frame(26, 0); frame[0] = 0x88; frame[1] = 0x42; frame[24] = 0x80;
    frame.insert(frame.end(), body.begin(), body.end());
    assert(mt7922_parse_data(frame.data(), (uint32_t)frame.size(), rx, &data) == MT7922_DATA_AMSDU);
    assert(data.body_offset == 26 && data.body_length == body.size());
    rx.payload_format = 3; frame.insert(frame.begin() + 26, 2, 0);
    assert(mt7922_parse_data(frame.data(), (uint32_t)frame.size(), rx, &data) == MT7922_DATA_AMSDU && data.body_offset == 28);
    assert(mt7922_validate_amsdu(frame.data() + data.body_offset, data.body_length, local, 1600, &count) == MT7922_AMSDU_OK && count == 2);
    // A malformed later subframe rejects the body before any earlier delivery.
    body[28 + 14] = 0;
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_MALFORMED && count == 0);
    body = first; body[0] = 4;
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_ADDRESS);
    body = first; body[6] = 1;
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_ADDRESS);
    body = first; body[19] = 1;
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_MALFORMED);
    body = amsduSubframe(local, 0, 1601);
    assert(mt7922_validate_amsdu(body.data(), (uint32_t)body.size(), local, 1600, &count) == MT7922_AMSDU_LIMIT);
    for (uint32_t n = 0; n < first.size(); n++)
        assert(mt7922_validate_amsdu(first.data(), n, local, 1600, &count) != MT7922_AMSDU_OK);
}
int main()
{
    test_tx_golden(); test_plaintext_and_cipher(); test_qos_padding_and_bounds(); test_ba_and_txfree(); test_mandatory_amsdu();
    puts("MT7922 TX/data codecs: TXWI/BA/TXFREE, mandatory A-MSDU validation, exact LLC/CCMP, fragment/QoS bounds passed");
}
