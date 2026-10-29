// SPDX-License-Identifier: BSD-3-Clause-Clear
// WPA2/CCMP group rekey and authenticated M3 retransmission handling.
// No hardware calls: evaluate is read-only; commit follows successful install.
#pragma once
#include "itl_wpa.h"
#include "itl_enterprise_policy.h"
#include "enterprise_channel.hpp"

class ItlEnterpriseRekey {
    uint8_t replay_[8]{}, anonce_[32]{}, gtk_[4][16]{};
    bool active_=false, valid_[4]{};
    uint8_t initialId_=0;
    static bool nonzero(const uint8_t *p,size_t n) {
        uint8_t v=0;for(size_t i=0;i<n;++i)v|=p[i];return v!=0;
    }
public:
    struct Update {
        bool install=false, pairwise=false, reconnect=false;
        uint8_t id=0, gtk[16]{}, replay[8]{}, reply[99]{};
        ~Update(){enterprise::erase(this,sizeof(*this));}
    };
    ~ItlEnterpriseRekey(){reset();}
    void reset(){enterprise::erase(replay_,sizeof(replay_));enterprise::erase(anonce_,sizeof(anonce_));
        enterprise::erase(gtk_,sizeof(gtk_));memset(valid_,0,sizeof(valid_));active_=false;initialId_=0;}
    void begin(const uint8_t replay[8],const uint8_t anonce[32],const uint8_t gtk[16],uint8_t id){
        reset();if(id>3)return;memcpy(replay_,replay,8);memcpy(anonce_,anonce,32);
        memcpy(gtk_[id],gtk,16);valid_[id]=true;initialId_=id;active_=true;
    }
    bool evaluate(const uint8_t *p,size_t n,const uint8_t ptk[48],const uint8_t *rsn,size_t rsnLen,Update &u)const{
        enterprise::erase(&u,sizeof(u));
        itl_eapol_key k{};
        if(!active_||!ptk||!p||n<99||p[0]<1||p[0]>3||p[1]!=3||p[4]!=2||
           size_t((unsigned(p[2])<<8)|p[3])+4!=n||itl_eapol_parse(p,n,&k)||
           n!=size_t(99)+k.datalen||k.keylen!=16)return false;
        u.pairwise=(k.info&ITL_KI_PAIRWISE)!=0;
        // M1 intentionally has no MIC. Preserve the existing reconnect policy
        // for a new pairwise exchange, without installing or acknowledging keys.
        if(k.info==0x008a&&memcmp(k.replay,replay_,8)>0&&nonzero(k.nonce,32)&&memcmp(k.nonce,anonce_,32)){
            u.reconnect=true;return true;
        }
        // Key index/reserved/request/error bits are not meaningful in RSN.
        if(k.info!=(u.pairwise?0x13ca:0x1382))return false;
        if(memcmp(k.replay,replay_,8)<0||!itl_eapol_mic_ok(ptk,p,n))return false;
        if(k.datalen<24||k.datalen>328||(k.datalen&7))return false;
        uint8_t plain[320]{};
        struct Erase {uint8_t *p;~Erase(){enterprise::erase(p,320);}} erase{plain};
        if(itl_aes128_unwrap(ptk+16,k.data,k.datalen,plain))return false;
        if(u.pairwise){
            if(memcmp(k.nonce,anonce_,32)||!itl_ep_keydata(plain,k.datalen-8,rsn,rsnLen,u.gtk,&u.id))return false;
            // A retransmitted M3 only gets M4 again. Never reinstall either key.
            if(u.id!=initialId_||!valid_[u.id]||memcmp(u.gtk,gtk_[u.id],16))return false;
        }else{
            // CCMP RSC is 48-bit. Do not accept reserved high bits.
            if(k.rsc[6]||k.rsc[7])return false;
            bool found=false;size_t off=0,total=k.datalen-8;
            while(off<total){
                if(plain[off]==0xdd&&(total-off==1||plain[off+1]==0)){
                    for(size_t i=off+1;i<total;++i)if(plain[i])return false;
                    break;
                }
                if(total-off<2||size_t(plain[off+1])+2>total-off)return false;
                const uint8_t *e=plain+off;size_t len=size_t(e[1])+2;
                if(e[0]==0xdd&&len>=6&&itl_ep_suite(e+2,1)){
                    if(found||len!=24||(e[6]&0xf8)||e[7])return false;
                    found=true;u.id=e[6]&3;memcpy(u.gtk,e+8,16);
                }else if(e[0]==48){
                    if(len!=rsnLen||memcmp(e,rsn,len))return false;
                }else return false; // No PMF/MLO/other security KDE negotiated.
                off+=len;
            }
            if(!found||!nonzero(u.gtk,16))return false;
            bool same=false;
            for(unsigned i=0;i<4;++i)if(valid_[i]&&!memcmp(gtk_[i],u.gtk,16)){
                if(i!=u.id)return false; // Never reset PN by moving an in-use key.
                same=true;
            }
            // An equal replay counter is only a retry of an already installed key.
            if(!same&&memcmp(k.replay,replay_,8)==0)return false;
            u.install=!same;
            // The base firmware command has no RX sequence parameter. Do not
            // install a GTK that has already sent traffic without preserving
            // its replay floor. Reassociate instead of silently starting at 0.
            u.reconnect=u.install&&nonzero(k.rsc,6);
        }
        memcpy(u.replay,k.replay,8);
        uint8_t zero[32]{};
        itl_eapol_build(u.reply,u.pairwise?0x030a:0x0302,0,k.replay,zero,nullptr,0);
        itl_eapol_mic(ptk,u.reply,sizeof(u.reply));
        return true;
    }
    void commit(const Update &u){
        memcpy(replay_,u.replay,8);
        if(u.install){memcpy(gtk_[u.id],u.gtk,16);valid_[u.id]=true;}
    }
};
