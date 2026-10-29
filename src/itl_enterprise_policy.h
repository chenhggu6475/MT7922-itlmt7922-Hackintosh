// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// WPA2 CCMP + non-FT 802.1X only. The EAP method is decided entirely by
// user space. No SSID, username realm, CA or school-specific policy here.
static inline bool itl_ep_suite(const uint8_t *p,uint8_t type) {
    return p[0]==0 && p[1]==15 && p[2]==172 && p[3]==type;
}
static inline bool itl_ep_rsn(const uint8_t *p,size_t n,bool request) {
    if (!p || n<20 || n>256 || p[0]!=48 || (size_t)p[1]+2!=n || p[2]!=1 || p[3]!=0) return false;
    if (!itl_ep_suite(p+4,4)) return false;
    size_t off=8;
    auto list=[&](uint8_t suite)->bool {
        if (off+2>n) return false;
        unsigned count=unsigned(p[off])|(unsigned(p[off+1])<<8); off+=2;
        if (!count || count>(n-off)/4 || (request && count!=1)) return false;
        bool found=false;
        for(unsigned i=0;i<count;++i,off+=4) if(itl_ep_suite(p+off,suite)) found=true;
        return found;
    };
    if (!list(4) || !list(1)) return false;
    if (off==n) return true;
    if (n-off<2) return false;
    unsigned caps=unsigned(p[off])|(unsigned(p[off+1])<<8); off+=2;
    // PMF is not implemented by the MT7922 base; reject required PMF.
    // A station request must not advertise PMF capability it cannot honor.
    if ((caps&0x40) || (request && (caps&0x80))) return false;
    if (off==n) return true;
    if(n-off<2)return false;
    unsigned count=unsigned(p[off])|(unsigned(p[off+1])<<8);off+=2;
    if(count>(n-off)/16 || (request && count))return false;
    off+=16*count;
    return off==n || (!request && n-off==4);
}
static inline bool itl_ep_ap(uint16_t cap,const uint8_t *ie,size_t len) {
    if (!(cap&16) || (!ie&&len)) return false;
    bool found=false;
    for(size_t off=0;off<len;){
        if(len-off<2 || (size_t)ie[off+1]>len-off-2)return false;
        size_t n=ie[off+1]+2;
        if(ie[off]==48){if(found || !itl_ep_rsn(ie+off,n,false))return false;found=true;}
        off+=n;
    }
    return found;
}
// Validate authenticated, decrypted M3 key data before installing anything.
// RSN IE must match the AP's advertised negotiation; accept one CCMP GTK only.
static inline bool itl_ep_keydata(const uint8_t *p,size_t n,const uint8_t *ap,size_t apn,
                                  uint8_t gtk[16],uint8_t *keyId) {
    if(!p||!gtk||!keyId||!itl_ep_rsn(ap,apn,false))return false;
    bool rsn=false,group=false;
    const uint8_t *key=nullptr;uint8_t id=0;
    size_t off=0;
    while(off<n){
        if(p[off]==0xdd && (n-off==1 || p[off+1]==0)){
            for(size_t i=off+1;i<n;++i)if(p[i])return false;
            break;
        }
        if(n-off<2 || size_t(p[off+1])+2>n-off)return false;
        const size_t len=size_t(p[off+1])+2;
        const uint8_t *e=p+off;
        if(e[0]==48){
            if(rsn||len!=apn||memcmp(e,ap,len))return false;
            rsn=true;
        }else if(e[0]==0xdd && len>=6 && itl_ep_suite(e+2,1)){
            if(group||len!=24||(e[6]&0xf8)||e[7])return false;
            group=true;id=e[6]&3;key=e+8;
        }
        off+=len;
    }
    if(!rsn||!group)return false;
    memcpy(gtk,key,16);*keyId=id;
    return true;
}
