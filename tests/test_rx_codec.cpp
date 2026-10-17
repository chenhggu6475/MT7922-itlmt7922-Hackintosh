// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../codec/mt7922_rx_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static void p32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static std::vector<uint8_t> beacon()
{
    // An independently authored 5 GHz beacon: SSID "test", channel 36.
    const uint8_t bytes[] = {
        0x80,0x00,0x00,0x00, 0xff,0xff,0xff,0xff,0xff,0xff,
        0x02,0x11,0x22,0x33,0x44,0x55, 0x02,0x11,0x22,0x33,0x44,0x55,
        0x10,0x00, 0,0,0,0,0,0,0,0, 0x64,0, 0x11,0,
        0,4,'t','e','s','t', 3,1,36
    };
    return std::vector<uint8_t>(bytes, bytes + sizeof(bytes));
}

static std::vector<uint8_t> packet(uint32_t d1, uint32_t d2, unsigned off)
{
    const std::vector<uint8_t> frame = beacon();
    std::vector<uint8_t> p(off + frame.size(), 0);
    p32(p.data(), (2u << 27) | (uint32_t)p.size());
    p32(p.data() + 4, d1);
    p32(p.data() + 8, d2);
    p32(p.data() + 12, 36u << 8);
    memcpy(p.data() + off, frame.data(), frame.size());
    return p;
}

static void expect_status(const std::vector<uint8_t> &p, Mt7922RxStatus want)
{
    Mt7922RxView v, before;
    memset(&v, 0x5a, sizeof(v));
    memcpy(&before, &v, sizeof(v));
    const Mt7922RxStatus actual = mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v);
    if (actual != want) {
        fprintf(stderr, "wanted %s, got %s\n", mt7922_rx_status_name(want), mt7922_rx_status_name(actual));
        assert(actual == want);
    }
    if (actual != MT7922_RX_OK) assert(memcmp(&v, &before, sizeof(v)) == 0);
}

static void test_golden_offsets()
{
    // Fixed expected offsets are taken from mt7921/mac.c (6 word base RXD),
    // deliberately including the 24-byte case missed by the original heuristic.
    struct Fixture { uint32_t flags; unsigned pad; unsigned offset; };
    const Fixture cases[] = {
        {0x00000000, 0, 24},
        {0x00004000, 0, 40},   // G4
        {0x00000800, 0, 40},   // G1
        {0x00001000, 0, 32},   // G2
        {0x00002000, 0, 32},   // G3
        {0x0000a000, 0, 104},  // G3+G5: 8+72 bytes
        {0x0000f800, 0, 144},  // G4+G1+G2+G3+G5
        {0x0000f800, 3, 150},  // maximum header padding
        {0x00010000, 0, 24},   // Connac3's G1 bit is Connac2 security mode
    };
    for (const Fixture &f : cases) {
        const std::vector<uint8_t> p = packet(f.flags | 0x135, f.pad << 14, f.offset);
        Mt7922RxView v = {};
        assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
        assert(v.frame_offset == f.offset && v.frame_length == beacon().size());
        assert(v.wlan_index == 0x135 && v.channel == 36 && v.band == 5);
        Mt7922Beacon b = {};
        assert(mt7922_parse_beacon(p.data() + v.frame_offset, v.frame_length, &b));
        assert(b.ssid_length == 4 && memcmp(b.ssid, "test", 4) == 0);
        assert(b.channel == 36 && b.beacon_interval == 100 && b.capability == 0x11);
    }
}

static void test_rcpi_and_band()
{
    std::vector<uint8_t> p = packet(0x2000, 0, 32);
    p32(p.data() + 28, 0xff008c78); // RCPI chains: 120,140,0,255
    p32(p.data() + 12, 191u << 8);  // 6 GHz encoding -> channel 41
    Mt7922RxView v = {};
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
    assert(v.chain_valid_mask == 3 && v.chain_rssi_dbm[0] == -50 && v.chain_rssi_dbm[1] == -40);
    assert(v.channel_encoded == 191 && v.channel == 41 && v.band == 6);

    p = packet(0xa000, 0, 104);
    p32(p.data() + 28, 0x01010101); // P-RXV is superseded by group 5
    p32(p.data() + 56, 0x0096826e); // C-RXV starts after 6 skipped words
    p32(p.data() + 12, 6u << 8);
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
    assert(v.chain_valid_mask == 7 && v.chain_rssi_dbm[0] == -55);
    assert(v.chain_rssi_dbm[1] == -45 && v.chain_rssi_dbm[2] == -35);
    assert(v.channel == 6 && v.band == 2);
}

static void test_types_and_lengths()
{
    std::vector<uint8_t> p = packet(0, 0, 24);
    for (unsigned t = 0; t < 32; ++t) {
        p32(p.data(), (t << 27) | (uint32_t)p.size());
        expect_status(p, (t == 2 || t == 8) ? MT7922_RX_OK : MT7922_RX_TYPE);
    }
    p32(p.data(), (7u << 27) | (1u << 16) | (uint32_t)p.size());
    expect_status(p, MT7922_RX_OK);
    p32(p.data(), (2u << 27) | (uint32_t)p.size());
    p.push_back(0x42); // RXD0 length excludes trailing DMA padding
    Mt7922RxView v = {};
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
    assert(v.frame_length == beacon().size());
    p32(p.data(), (2u << 27) | (uint32_t)(p.size() + 1));
    expect_status(p, MT7922_RX_LENGTH);
    p32(p.data(), (2u << 27) | 23);
    expect_status(p, MT7922_RX_LENGTH);
}

static void test_phy_vectors()
{
    struct Golden { uint32_t word; uint8_t mode, mcs, nss, bw, gi; };
    // Independent P-RXV words use mt76_connac2_mac.h field positions, not
    // Connac3's different vector layout. STBC reports NSTS, not data NSS.
    const Golden cases[] = {
        {0x0400a089,4,9,2,2,1},   // VHT MCS9, 2SS, 80MHz, short GI
        {0x0440a189,4,9,2,2,1},   // STBC + NSTS4 -> data NSS2
        {0x0200908f,2,15,2,1,1},  // HT MCS15 -> NSS2, 40MHz, short GI
        {0x0100000b,1,11,1,0,0},  // OFDM hardware index11 = 6Mbps
        {0x00000006,0,6,1,0,0},   // CCK short-preamble hardware index6
        {0x0801309b,8,11,2,3,2},  // HE MCS11+DCM, NSS2,160MHz,3.2us GI
    };
    for (const Golden &g : cases) {
        std::vector<uint8_t> p = packet(0x2000, 0, 32);
        p32(p.data() + 24, g.word);
        Mt7922RxView v = {};
        assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
        assert(v.phy_valid && v.phy_mode == g.mode && v.phy_mcs == g.mcs &&
               v.phy_nss == g.nss && v.phy_bw == g.bw && v.phy_gi == g.gi);
    }
    // Group5 changes the RCPI location, not the P-RXV word0 supplying rates.
    std::vector<uint8_t> p = packet(0xa000, 0, 104);
    p32(p.data() + 24, 0x0400a089);
    p32(p.data() + 56, 0x06000000); // deliberately not a rate vector
    Mt7922RxView v = {};
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK);
    assert(v.phy_valid && v.phy_mode == 4 && v.phy_mcs == 9);
    // G4/G1/G2 precede G3: rate word is at64, frame starts at72.
    p = packet(0x7800, 0, 72); p32(p.data() + 64, 0x0400a089);
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK && v.phy_valid && v.phy_nss == 2);
    const uint32_t bad[] = {0x06000000,0x0400000c,0x04004009};
    for (uint32_t word : bad) {
        p = packet(0x2000, 0, 32); p32(p.data() + 24, word);
        assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK && !v.phy_valid);
    }
    p = packet(0, 0, 24);
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), &v) == MT7922_RX_OK && !v.phy_valid);
    for (unsigned n = 24; n < 32; n++) {
        p.assign(n, 0); p32(p.data(), (2u << 27) | n); p32(p.data() + 4, 0x2000);
        expect_status(p, MT7922_RX_GROUPS);
    }
}

static void test_errors()
{
    // FCS/ICV/MIC and band-1 are rejected using Connac2 positions.
    const unsigned d1_bits[] = {25, 26, 27, 28};
    for (unsigned bit : d1_bits) expect_status(packet(1u << bit, 0, 24), MT7922_RX_ERROR);
    const unsigned d2_bits[] = {23, 24};
    for (unsigned bit : d2_bits) expect_status(packet(0, 1u << bit, 24), MT7922_RX_ERROR);
    expect_status(packet(0, 1u << 13, 24), MT7922_RX_TRANSLATED);
    expect_status(packet(1u << 15, 0, 24), MT7922_RX_GROUPS);

    // Under-sized declared packets cannot expose bytes from a truncated group.
    for (unsigned n = 24; n < 150; ++n) {
        std::vector<uint8_t> p(n, 0);
        p32(p.data(), (2u << 27) | n);
        p32(p.data() + 4, 0xf800);
        p32(p.data() + 8, 3u << 14);
        expect_status(p, MT7922_RX_GROUPS);
    }
    std::vector<uint8_t> p(150, 0);
    p32(p.data(), (2u << 27) | 150);
    p32(p.data() + 4, 0xf800);
    p32(p.data() + 8, 3u << 14);
    expect_status(p, MT7922_RX_SHORT);
}

static void test_beacon_rejections()
{
    Mt7922Beacon b = {};
    const std::vector<uint8_t> good = beacon();
    for (unsigned n = 0; n < 36; ++n) assert(!mt7922_parse_beacon(good.data(), n, &b));
    std::vector<uint8_t> p = good;
    p[10] ^= 2; assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p[1] = 1; assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p[4] = 1; assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p[37] = 33; assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p.pop_back(); assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p.push_back(0); p.push_back(0); // duplicate SSID
    assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p.push_back(255); assert(!mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b));
    p = good; p[0] = 0x50; p[4] = 2; // unicast probe response
    assert(mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b) && b.probe_response);
    p.assign(good.begin(), good.begin() + 36);
    p.push_back(0); p.push_back(0); // hidden SSID
    assert(mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b) && b.ssid_length == 0);
    p.push_back(61); p.push_back(1); p.push_back(149); // HT-only primary channel
    assert(mt7922_parse_beacon(p.data(), (uint32_t)p.size(), &b) && b.channel == 149);
}

static void test_adversarial_buffers()
{
    uint32_t state = 0xc7922;
    for (unsigned n = 0; n < 4096; ++n) {
        std::vector<uint8_t> p(n);
        for (uint8_t &b : p) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; b = state; }
        if (n >= 4) p32(p.data(), (2u << 27) | n);
        Mt7922RxView v = {};
        if (mt7922_parse_rx_normal(p.data(), n, &v) == MT7922_RX_OK) {
            assert(v.frame_offset < n && v.frame_length <= n - v.frame_offset);
            Mt7922Beacon b = {};
            (void)mt7922_parse_beacon(p.data() + v.frame_offset, v.frame_length, &b);
        }
        Mt7922Beacon b = {};
        (void)mt7922_parse_beacon(p.data(), n, &b);
    }
    Mt7922RxView v = {};
    assert(mt7922_parse_rx_normal(nullptr, 4096, &v) == MT7922_RX_SHORT);
    std::vector<uint8_t> p = packet(0, 0, 24);
    assert(mt7922_parse_rx_normal(p.data(), (uint32_t)p.size(), nullptr) == MT7922_RX_SHORT);
}

int main()
{
    test_golden_offsets();
    test_rcpi_and_band();
    test_phy_vectors();
    test_types_and_lengths();
    test_errors();
    test_beacon_rejections();
    test_adversarial_buffers();
    puts("MT7922 RX codec: golden offsets/PHY/STBC, RCPI, packet types, truncation, beacon IEs, adversarial buffers passed");
}
