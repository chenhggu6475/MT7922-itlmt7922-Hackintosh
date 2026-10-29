// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef ITL_ENTERPRISE_IE_H
#define ITL_ENTERPRISE_IE_H
#include <stddef.h>
#include <stdint.h>
// wpa_driver_associate_params.wpa_ie is an IE STREAM. The kernel ABI carries
// exactly one RSN IE; Extended Capabilities are optional supplicant metadata.
// Do not silently discard security/FT/RSNX/vendor negotiation elements.
static inline int itl_ep_select_rsn(const uint8_t *ies, size_t length,
                                  const uint8_t **rsn, size_t *rsn_length)
{
    size_t off = 0;
    if (!rsn || !rsn_length) return 0;
    *rsn = NULL; *rsn_length = 0;
    if (!ies || !length || length > 2048) return 0;
    while (off < length) {
        size_t n;
        if (length - off < 2) return 0;
        n = (size_t)ies[off + 1] + 2;
        if (n > length - off) return 0;
        if (ies[off] == 48) {
            if (*rsn || n < 20 || n > 256) return 0;
            *rsn = ies + off; *rsn_length = n;
        } else if (ies[off] != 127) {
            return 0;
        }
        off += n;
    }
    return *rsn != NULL;
}
#endif
