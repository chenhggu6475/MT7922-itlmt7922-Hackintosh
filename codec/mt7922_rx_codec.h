// SPDX-License-Identifier: BSD-3-Clause-Clear
// Portable, bounded MT7922 / Connac2 receive decoding for the scan diagnostic.
#ifndef MT7922_RX_CODEC_H
#define MT7922_RX_CODEC_H

#include <stdint.h>

enum Mt7922RxStatus {
    MT7922_RX_OK = 0,
    MT7922_RX_SHORT,
    MT7922_RX_TYPE,
    MT7922_RX_LENGTH,
    MT7922_RX_GROUPS,
    MT7922_RX_ERROR,
    MT7922_RX_TRANSLATED
};

struct Mt7922RxView {
    uint32_t frame_offset;
    uint32_t frame_length;
    uint16_t packet_length;
    uint16_t wlan_index;
    uint8_t packet_type;
    uint8_t channel_encoded;
    uint8_t channel;
    uint8_t band;                 // 2, 5, or 6 GHz
    uint8_t payload_format;       // RXD4[1:0]: 0 whole, 3 first, 2 middle, 1 last
    uint8_t security_mode;        // RXD1[20:16], AES-CCMP is 4
    uint8_t security_done;        // RXD1 bit 31
    uint8_t cipher_mismatch;      // RXD1 CM / CLM; never deliver protected data
    uint8_t fragmented;           // RXD2 fragment indication
    uint8_t chain_valid_mask;
    int16_t chain_rssi_dbm[4];
    uint8_t phy_valid;
    uint8_t phy_mode;             // Connac2: CCK0 OFDM1 HT2 HT-GF3 VHT4 HE8..11
    uint8_t phy_mcs;              // HT/VHT/HE MCS; CCK/OFDM hardware rate index
    uint8_t phy_nss;              // data streams, NSTS corrected for STBC
    uint8_t phy_bw;               // raw RX vector code: 0/1/2/3 = 20/40/80/160
    uint8_t phy_gi;               // HT/VHT: 0 long / 1 short; HE raw 0/1/2
};

// data_length is the DMA-reported length. RXD0's shorter declared length excludes
// any trailing DMA padding; a larger declared length is a truncated packet.
// Returns a view only for unmodified 802.11 NORMAL / NORMAL_MCU frames. This
// diagnostic intentionally rejects 802.3 header translation and bad FCS/MIC.
Mt7922RxStatus mt7922_parse_rx_normal(const uint8_t *data, uint32_t data_length,
                                     Mt7922RxView *out);

struct Mt7922Beacon {
    uint8_t bssid[6];
    uint8_t ssid[32];              // raw bytes, not necessarily printable or UTF-8
    uint8_t ssid_length;
    uint8_t channel;               // DS parameter / HT primary channel; 0 absent
    uint16_t capability;
    uint16_t beacon_interval;
    uint8_t probe_response;
};

// Exact frame offset from mt7922_parse_rx_normal is required: no pattern search.
// Rejects malformed/truncated IEs and duplicate SSID elements.
bool mt7922_parse_beacon(const uint8_t *frame, uint32_t frame_length,
                          Mt7922Beacon *out);

const char *mt7922_rx_status_name(Mt7922RxStatus status);

#endif
