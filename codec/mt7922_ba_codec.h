// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef MT7922_BA_CODEC_H
#define MT7922_BA_CODEC_H
#include <stdint.h>

bool mt7922_encode_wtbl_ba(uint8_t out[48], const uint8_t peer[6], uint8_t tid,
                            bool recipient, bool enable, uint16_t ssn, uint16_t window);
bool mt7922_encode_sta_ba(uint8_t out[20], uint8_t tid, bool recipient,
                           bool enable, uint16_t ssn, uint16_t window, bool amsdu);
struct Mt7922TxFreeCursor {
    const uint8_t *packet;
    uint32_t length, offset, count, total;
};
struct Mt7922TxFreeRecord { uint16_t token, attempts; uint8_t status; };
bool mt7922_tx_free_begin(const uint8_t *packet, uint32_t length, Mt7922TxFreeCursor *cursor);
bool mt7922_tx_free_next(Mt7922TxFreeCursor *cursor, Mt7922TxFreeRecord *record);
#endif
