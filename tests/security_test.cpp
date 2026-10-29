// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../src/itl_security.h"
#include "../codec/mt7922_data_codec.h"
#include "../codec/mt7922_tx_codec.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

int main()
{
    // Old clients cannot accidentally request plaintext by omitting a password.
    for (unsigned sl = 0; sl <= 36; ++sl)
        for (unsigned pl = 0; pl <= 68; ++pl) {
            const bool ssid = sl >= 1 && sl <= 32;
            assert(itl_join_valid(1, sl, pl) == (ssid && pl >= 8 && pl <= 63));
            assert(itl_join_valid(2, sl, pl) == (ssid && pl == 0));
            assert(!itl_join_valid(0, sl, pl));
            assert(!itl_join_valid(3, sl, pl));
        }
    const uint8_t openIE[] = {0,4,'t','e','s','t',1,2,0x82,0x84,221,7,0,0x50,0xf2,2,0,1,0};
    const uint8_t wpaIE[] = {221,4,0,0x50,0xf2,1};
    const uint8_t rsnIE[] = {48,2,1,0};
    assert(itl_ap_matches_security(true, 1, openIE, sizeof(openIE)));
    assert(!itl_ap_matches_security(false, 1, openIE, sizeof(openIE)));
    assert(!itl_ap_matches_security(true, 0x11, openIE, sizeof(openIE))); // WEP/privacy
    assert(!itl_ap_matches_security(true, 1, wpaIE, sizeof(wpaIE)));
    assert(!itl_ap_matches_security(true, 1, rsnIE, sizeof(rsnIE))); // also deny OWE/enterprise
    assert(!itl_ap_matches_security(true, 1, openIE, sizeof(openIE)-1));
    assert(!itl_ap_matches_security(true, 1, openIE, 1));
    assert(!itl_ap_matches_security(true, 1, nullptr, 1));
    assert(itl_ap_matches_security(false, 0x11, rsnIE, sizeof(rsnIE)));
    for (unsigned g2 = 0; g2 < 2; ++g2) {
        assert(itl_assoc_capability(false, g2) == (g2 ? 0x0431 : 0x0411));
        assert(itl_assoc_capability(true, g2) == (g2 ? 0x0421 : 0x0401));
    }
    uint8_t assoc[32]; memset(assoc, 0x5a, sizeof(assoc));
    assert(itl_assoc_security_ie(true, assoc) == 0 && assoc[0] == 0x5a);
    assert(itl_assoc_security_ie(false, assoc) == 22);
    const uint8_t expected[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
    assert(!memcmp(assoc, expected, sizeof(expected)));
    // The actual frame parser and firmware TX encoder, not just UI policy.
    for (unsigned open = 0; open < 2; ++open) {
        uint8_t f[36] = {}, tx[32] = {};
        f[0]=8; f[1]=open ? 2 : 0x42; f[4]=2;
        const uint8_t llc[] = {0xaa,0xaa,3,0,0,0,8,0};
        memcpy(f+24, llc, sizeof(llc));
        Mt7922RxView rx={}; rx.security_mode=open ? 0 : 4;
        Mt7922DataView d={};
        assert(mt7922_parse_data(f, sizeof(f), rx, &d) == MT7922_DATA_OK);
        assert(itl_rx_security_allowed(open, true, d.protected_frame, false, d.ethertype));
        Mt7922TxOptions opts={}; opts.wlan_index=1; opts.sequence=-1; opts.protect=!open;
        f[1]=open ? 1 : 0x41; // to-DS transmission
        assert(mt7922_encode_txwi(tx, f, sizeof(f), opts));
        assert(bool(tx[12]&2) == !open);
    }
    for (uint16_t type : {uint16_t(0x0800),uint16_t(0x0806),uint16_t(0x86dd)}) {
        assert(itl_rx_security_allowed(true, true, false, false, type));
        assert(!itl_rx_security_allowed(true, false, false, false, type));
        assert(!itl_rx_security_allowed(true, true, true, false, type));
        assert(!itl_rx_security_allowed(false, true, false, false, type));
        assert(itl_rx_security_allowed(false, true, true, false, type));
    }
    assert(itl_rx_security_allowed(true, true, false, true, 0)); // open A-MSDU
    assert(!itl_rx_security_allowed(true, false, false, true, 0));
    assert(!itl_rx_security_allowed(false, true, false, true, 0x888e));
    assert(itl_rx_security_allowed(false, false, false, false, 0x888e));
    puts("open/WPA2 policy, association IEs and TX/RX regression checks passed");
}
