// SPDX-License-Identifier: BSD-3-Clause-Clear
// Explicit open-network policy for the local 2026-10-26 experimental build.
#ifndef ITL_SECURITY_H
#define ITL_SECURITY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "itl_uc_abi.h"

static inline bool itl_join_valid(uint32_t abi, size_t ssidLen, size_t pskLen)
{
    if (ssidLen < 1 || ssidLen > 32) return false;
    // ABI 1 remains strictly WPA2. ABI 2 explicitly requests an open link.
    return (abi == ITL_UC_ABI_VERSION && pskLen >= 8 && pskLen <= 63) ||
           (abi == ITL_UC_OPEN_ABI_VERSION && pskLen == 0);
}

static inline bool itl_ap_matches_security(bool open, uint16_t cap, const uint8_t *ie, size_t len)
{
    if (!open) return (cap & 0x10) != 0; // never downgrade a PSK request to open
    if ((cap & 0x10) || (!ie && len)) return false;
    size_t pos = 0;
    while (pos < len) {
        if (len - pos < 2) return false;
        const size_t n = ie[pos + 1];
        if (n > len - pos - 2) return false;
        if (ie[pos] == 48) return false; // RSN, including OWE, is not open
        if (ie[pos] == 221 && n >= 4 && ie[pos + 2] == 0 &&
            ie[pos + 3] == 0x50 && ie[pos + 4] == 0xf2 && ie[pos + 5] == 1) return false;
        pos += n + 2;
    }
    return true;
}

static inline uint16_t itl_assoc_capability(bool open, bool band2g)
{
    return (uint16_t)((band2g ? 0x0431 : 0x0411) & (open ? ~0x10 : ~0));
}

static inline size_t itl_assoc_security_ie(bool open, uint8_t *out)
{
    static const uint8_t rsn[22] = {0x30,0x14,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0,0};
    if (open) return 0;
    memcpy(out, rsn, sizeof(rsn));
    return sizeof(rsn);
}

static inline bool itl_rx_security_allowed(bool open, bool linkUp, bool protectedFrame,
                                          bool amsdu, uint16_t ethertype)
{
    if (open) return linkUp && !protectedFrame;
    // Keep the existing WPA2 gate: cleartext only for a non-aggregated EAPOL.
    return protectedFrame || (!amsdu && ethertype == 0x888e);
}
#endif
