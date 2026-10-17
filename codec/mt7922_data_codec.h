// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef MT7922_DATA_CODEC_H
#define MT7922_DATA_CODEC_H
#include "mt7922_rx_codec.h"

enum Mt7922DataStatus {
    MT7922_DATA_OK = 0,
    MT7922_DATA_NULL,
    MT7922_DATA_AMSDU,
    MT7922_DATA_SHORT,
    MT7922_DATA_FORMAT,
    MT7922_DATA_CIPHER,
    MT7922_DATA_FRAGMENT
};
struct Mt7922DataView {
    uint32_t body_offset;
    uint32_t body_length;
    uint16_t ethertype;
    uint16_t sequence;
    uint8_t tid;
    uint8_t qos;
    uint8_t qos_control;
    uint8_t protected_frame;
    uint8_t retry;
};
// Raw From-DS frame after RX descriptor removal. CCMP IV/MIC are stripped by
// hardware when SEC_MODE=4 and !CM/CLM, with ICV/MIC errors rejected upstream.
// This is mt7921_mac_fill_rx's decrypted test; SEC_DONE is diagnostic metadata,
// not a validity requirement in the upstream MT7922 path. Failed ciphertext
// is never searched for LLC or exposed as plaintext.
Mt7922DataStatus mt7922_parse_data(const uint8_t *frame, uint32_t length,
                                   const Mt7922RxView &rx, Mt7922DataView *out);

enum Mt7922AmsduStatus {
    MT7922_AMSDU_OK = 0,
    MT7922_AMSDU_MALFORMED,
    MT7922_AMSDU_LIMIT,
    MT7922_AMSDU_ADDRESS
};
// Validate the complete normalized body before delivering any subframe. Each
// subframe must target this station or a group address, with a unicast source,
// bounded length, valid RFC1042/802.1H SNAP and an Ethernet-II protocol field.
Mt7922AmsduStatus mt7922_validate_amsdu(const uint8_t *body, uint32_t length,
                                       const uint8_t local_address[6], uint32_t max_payload,
                                       uint32_t *subframes);
// Connac2 payload format: 0 = whole MPDU, 1 = last deaggregated subframe,
// 2 = middle, 3 = first. Only whole/last completes a reorder sequence.
inline bool mt7922_amsdu_complete(uint8_t payload_format)
{
    return payload_format == 0 || payload_format == 1;
}
#endif
