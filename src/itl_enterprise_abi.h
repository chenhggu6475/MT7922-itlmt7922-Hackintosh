// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef ITL_ENTERPRISE_ABI_H
#define ITL_ENTERPRISE_ABI_H
#include <stdint.h>
#define ITL_EP_ABI 1u
#define ITL_EP_JOIN 16u
#define ITL_EP_POLL 17u
#define ITL_EP_TX 18u
#define ITL_EP_PMK 19u
#define ITL_EP_LEAVE 20u
#define ITL_EP_FRAME_MAX 1536u
// Requires one long-lived administrator IOUserClient connection. The kernel
// owns the client identity; epoch rejects stale commands from that connection.
struct itl_ep_request {
    uint64_t epoch;
    uint32_t abi, length;
    uint8_t data[ITL_EP_FRAME_MAX];
};
struct itl_ep_join {
    uint32_t abi, ssid_length, rsn_length, reserved;
    uint8_t ssid[32], rsn[256];
};
// state: 0 idle, 1 association, 2 EAP, 3 key handshake, 4 data up, 5 failed
struct itl_ep_reply {
    uint64_t epoch;
    uint32_t abi, state, length, channel, ap_rsn_length, request_rsn_length;
    uint8_t mac[6], bssid[6], reserved[4];
    uint8_t ap_rsn[256], request_rsn[256], data[ITL_EP_FRAME_MAX];
};
#endif
