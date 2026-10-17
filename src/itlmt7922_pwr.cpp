// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// MT7922 Connac2 power setup, based on upstream mt7921/mt76_connac_mcu:
// CE SET_CLC (0x5c), then CE SET_RATE_TX_POWER (0x5d), batches of eight
// channels with 161-entry SKU tables. ACPI MTDS/MTGS SAR limits and the
// configured conservative ceiling bound the per-rate powers.
#include "itlmt7922.hpp"
#include "mt7922_regs.h"
#include "itl_power.h"

#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <string.h>

extern const uint8_t itl_fw_ram[], itl_fw_ram_end[];

static inline uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

#define ITL_EIO -5
#define ITL_EINVAL -22

// Connac2 SKU sends do not request command-result events (matching upstream).
int itlmt7922::powerCmd(const void *payload, size_t plen, const char *name)
{
    if (shouldStop() || firmwareFault) return ITL_EIO;
    int rc=mcuSend((1u<<18)|0x5d,payload,plen,false,nullptr);
    if (rc) m3log("%s failed rc=%d",name,rc);
    return rc;
}

#define FW_TYPE_CLC         2
#define CLC_HDR             16              // len, idx, ver, nr_country, type, reserved8

// ---------------------------------------------------------------------------------------------
// ACPI SAR tables (package of integers under the Wi-Fi device: \_SB.PCI0.GPP5.WLAN.MTDS / MTGS on the GA403WM)
// ---------------------------------------------------------------------------------------------
static int acpiTable(IOPCIDevice *pci, const char *name, uint8_t *out, uint32_t max)
{
    OSString *path = pci ? OSDynamicCast(OSString, pci->getProperty("acpi-path")) : nullptr;
    if (!path) return -1;
    IORegistryEntry *e = IORegistryEntry::fromPath(path->getCStringNoCopy());
    IOACPIPlatformDevice *dev = OSDynamicCast(IOACPIPlatformDevice, e);
    OSObject *r = nullptr;
    int n = -1;
    if (dev && dev->evaluateObject(name, &r) == kIOReturnSuccess) {
        OSArray *a = OSDynamicCast(OSArray, r);
        if (a && a->getCount() <= max) {
            n = (int)a->getCount();
            for (int i = 0; i < n; i++) {
                OSNumber *v = OSDynamicCast(OSNumber, a->getObject((unsigned)i));
                if (!v) { n = -1; break; }
                out[i] = (uint8_t)v->unsigned32BitValue();
            }
        }
    }
    OSSafeReleaseNULL(r);
    OSSafeReleaseNULL(e);
    return n;
}

// mt792x_asar_fits(): which layout a table really has (some BIOSes label a v2 table v1)
// sarPwr[i] = mt792x_asar_range_pwr() for the world regulatory domain (geo row 2), which is the domain the driver gives the
// firmware (set_domain "00")
void itlmt7922::sarInit()
{
    int8_t *sar = sarPwr;
    char log[192];
    const size_t logLen = sizeof(log);
    uint8_t dyn[128], geo[128];
    int dl = acpiTable(pci, "MTDS", dyn, sizeof(dyn));
    int gl = acpiTable(pci, "MTGS", geo, sizeof(geo));
    int dynVer, geoVer;
    parseSar(dyn, dl, geo, gl, sar, &dynVer, &geoVer);
    if (!dynVer) { m3log("sar: none (MTDS len %d)", dl); return; }
    snprintf(log, logLen, "sar: MTDS v%d MTGS v%d -> 2g %d 5g %d/%d/%d/%d 6g %d (0.5 dBm)", dynVer, geoVer, sar[0], sar[1], sar[2], sar[3], sar[4], sar[5]);
    m3log("%s", log);
}

// mt7922_mcu_read_eeprom(): EFUSE_CTRL query of the 16-byte block holding off
int itlmt7922::efuseRead(uint32_t off, uint8_t *val)
{
    if (!val) return ITL_EINVAL;
    uint8_t request[24]={}; uint32_t address=off&~15u;
    memcpy(request,&address,4);
    uint8_t event[128]={}; uint32_t length=0;
    int rc=mcuSend((1u<<16)|0x01ed,request,sizeof(request),true,nullptr,event,sizeof(event),&length,3000);
    if (rc) return rc;
    // Connac2 event36 + addr4 + valid4 + data16.
    if (length<60 || le32(event+36)!=address || !le32(event+40)) {
        m3log("efuse invalid response len=%u address=%08x valid=%u",length,
              length>=40?le32(event+36):0,length>=44?le32(event+40):0);
        return ITL_EIO;
    }
    *val=event[44+(off&15)];
    return 0;
}

// ---------------------------------------------------------------------------------------------
// CLC: __mt7922_mcu_set_clc() for the POWER and POWER_EXT tables
// ---------------------------------------------------------------------------------------------
static const uint8_t *clcRegion(uint32_t *length)
{
    return findClcRegion(itl_fw_ram,(size_t)(itl_fw_ram_end-itl_fw_ram),length);
}

// sends the rules of one table for country cc; returns the number sent, -1 on a command failure
int itlmt7922::clcSend(const uint8_t *clc, const char *cc, uint8_t env, uint8_t idx)
{
    const uint32_t length=le32(clc);
    uint32_t offset=CLC_HDR; int sent=0;
    while (!shouldStop() && !firmwareFault) {
        ClcRule item={}; int state=nextClcRule(clc,length,&offset,&item);
        if (state<0) return -1;
        if (!state) return sent;
        const uint8_t *rule=item.header; uint16_t bytes=item.bytes;
        if (rule[0]!=(uint8_t)cc[0] || rule[1]!=(uint8_t)cc[1]) continue;
        if (bytes>2048) return -1;
        uint8_t request[76+2048]={};
        request[0]=1; put16(request+2,76+bytes); request[4]=idx; request[5]=env;
        request[8]=rule[0]; request[9]=rule[1]; request[10]=rule[2]; request[11]=rule[3];
        memcpy(request+76,item.data,bytes);
        if (mcuSend((1u<<18)|0x5c,request,76+bytes,false,nullptr)) return -1;
        sent++;
    }
    return -1;
}

int itlmt7922::m3Clc(const char *cc, uint8_t env)
{
    uint32_t length=0; const uint8_t *base=clcRegion(&length);
    if (!base) { m3log("CLC region absent"); return ITL_EINVAL; }
    ClcTables selected={};
    if (!selectClcTables(base,length,hwEncap,&selected)) return ITL_EINVAL;
    const uint8_t *tables[2]={selected.power,selected.channels};
    int counts[2]={};
    for (uint8_t idx=0;idx<2;idx++) if (tables[idx]) {
        counts[idx]=clcSend(tables[idx],cc,env,idx);
        if (!counts[idx]) counts[idx]=clcSend(tables[idx],"00",1,idx);
        if (counts[idx]<0) return ITL_EIO;
    }
    m3log("Connac2 CLC %c%c encap=%u power=%d channels=%d extensions=%u",cc[0],cc[1],hwEncap,counts[0],counts[1],selected.skipped);
    if (!tables[0] || counts[0]<=0) return ITL_EIO;
    memcpy(clcCc,cc,2); clcCc[2]=0;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// rate power limits: mt7922_mcu_rate_txpower_band() for 2.4 and 5 GHz
// ---------------------------------------------------------------------------------------------
// mt7922_mcu_build_sku() with every rate at the same limit v (unused entries 127 = no limit)
int itlmt7922::m3TxPower()
{
    // mt76_connac_sku_tlv: channel1 + 161 power entries. MT7925's449-entry
    // UNI SET_POWER_LIMIT payload is incompatible with this CE command.
    static const uint8_t ch2[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    static const uint8_t ch5[]={36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,132,134,136,138,140,142,144,149,151,153,155,157,159,161,165,169,173,177};
    uint8_t message[44+8*162]; int commands=0;
    for (unsigned band=0;band<2;band++) {
        const bool g2=band==0; const uint8_t *channels=g2?ch2:ch5;
        const unsigned count=g2?sizeof(ch2):sizeof(ch5);
        for (unsigned first=0;first<count;first+=8) {
            if (shouldStop() || firmwareFault) return ITL_EIO;
            const unsigned batch=count-first<8?count-first:8;
            bzero(message,sizeof(message)); message[4]=(uint8_t)batch; message[5]=g2?1:2;
            message[6]=!g2 && first+batch==count;
            message[8]='0'; message[9]='0';
            for (unsigned j=0;j<batch;j++) {
                const uint8_t channel=channels[first+j]; uint8_t *entry=message+44+j*162;
                entry[0]=channel; int8_t *sku=(int8_t *)(entry+1);
                uint32_t frequency=g2?(channel==14?2484:2407+5*channel):5000+5*channel;
                int8_t limit=sarFor(sarPwr,frequency);
                if (limit>(int8_t)pwrCeil) limit=(int8_t)pwrCeil;
                buildSku(sku,limit,g2);
            }
            int rc=powerCmd(message,44+batch*162,"SKU CE5d");
            if (rc) return rc;
            // Same PSE register-read fence as the mt76 batch loop.
            uint8_t reg[8]={}; uint32_t address=0x820c8000; memcpy(reg,&address,4);
            rc=mcuSend((1u<<18)|(1u<<16)|0xc0,reg,sizeof(reg),true,nullptr);
            if (rc) return rc;
            commands++;
        }
    }
    m3log("Connac2 SKU commands=%d ceiling=%u (0.5dBm)",commands,(unsigned)pwrCeil);
    return 0;
}
