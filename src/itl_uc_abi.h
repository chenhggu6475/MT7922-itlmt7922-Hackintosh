// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Interface between itlmt7922 and user space (the ItlWiFi menu bar app). Shared by the kext and the app; keep it plain C.
//
// Commands go through IOServiceOpen(itlmt7922) + IOConnectCallStructMethod. State comes back as registry properties on the
// itlmt7922 service, readable without opening a connection (IORegistryEntryCreateCFProperty):
//   wifi-state     string  off | idle | scanning | connecting | connected | failed
//   wifi-error     string  why the last attempt failed or the link went down ("" when none)
//   wifi-ssid      string  network of the current / last attempt
//   wifi-channel   number  primary channel of the AP
//   wifi-rssi      number  signal in dBm (signed 32-bit, 0 = unknown)
//   wifi-rate      number  estimated PHY rate, Mb/s
//   wifi-scan-gen  number  incremented after every finished scan
//   wifi-networks  array of { ssid string, channel number, rssi number (dBm, 0 = unknown), security string, age number (s) }
//                  security: open | wep | wpa | wpa2 | wpa2/wpa3 | wpa3 (only wpa2 and wpa2/wpa3 can be joined today)
#ifndef ITL_UC_ABI_H
#define ITL_UC_ABI_H

#include <stdint.h>

#define ITL_UC_ABI_VERSION 1

enum {
    kItlUcScan = 0,        // no input; refused (kIOReturnBusy) within 10 s of the previous scan
    kItlUcJoin = 1,        // input: struct itl_uc_join
    kItlUcDisconnect = 2,  // no input; leaves the network and stops reconnecting until the next join
    kItlUcCount
};

struct itl_uc_join {
    uint32_t abi;          // ITL_UC_ABI_VERSION
    char ssid[36];         // NUL terminated, 1..32 bytes
    char psk[68];          // NUL terminated passphrase, 8..63 characters
};

#endif
