// SPDX-License-Identifier: BSD-3-Clause-Clear
// Connac2 station removal, matching mt76_connac_mcu_sta_cmd(enable=false).
#ifndef ITL_SESSION_H
#define ITL_SESSION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline size_t itl_sta_remove_message(uint8_t *out, size_t capacity,
                                           const uint8_t bssid[6])
{
    if (!out || !bssid || capacity < 40) return 0;
    memset(out, 0, 40);
    // STA request: BSS 0, AP WCID 1, two top-level TLVs, append enabled.
    out[1] = 1; out[2] = 2; out[4] = 1;
    // STA_REC_BASIC (0), 20 bytes; CONNECTION_INFRA_AP = 0x10002.
    uint8_t *basic = out + 8;
    basic[2] = 20; basic[4] = 2; basic[6] = 1;
    basic[8] = 0; // CONN_STATE_DISCONNECT; NOT the 802.11 state TLV
    memcpy(basic + 12, bssid, 6);
    basic[18] = 1; // EXTRA_INFO_VER without EXTRA_INFO_NEW
    // STA_REC_WTBL (13), nested header only: RESET_AND_SET with zero TLVs.
    uint8_t *wtbl = out + 28;
    wtbl[0] = 13; wtbl[2] = 12; wtbl[4] = 1; wtbl[5] = 1;
    return 40;
}

// Preserve complete, recent log records rather than freezing at buffer full.
static inline void itl_log_append(char *buffer, size_t capacity, size_t &used, const char *line)
{
    const size_t len = strlen(line);
    if (!capacity || len + 2 > capacity) return;
    if (used >= capacity) used = 0;
    while (used && used + len + 2 > capacity) {
        size_t cut = 0;
        while (cut < used && buffer[cut] != ';') ++cut;
        if (cut < used) ++cut;
        memmove(buffer, buffer + cut, used - cut);
        used -= cut;
    }
    memcpy(buffer + used, line, len);
    used += len;
    buffer[used++] = ';';
    buffer[used] = 0;
}
#endif
