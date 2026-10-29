// SPDX-License-Identifier: BSD-3-Clause-Clear
// Real UserClient/worker bridge, isolated enterprise development path.
#include "itlmt7922.hpp"
#include "itl_enterprise_policy.h"
#include <string.h>
#include <libkern/libkern.h>

namespace {
struct EpLock {
    IOLock *lock;
    explicit EpLock(IOLock *l):lock(l){IOLockLock(lock);}
    ~EpLock(){IOLockUnlock(lock);}
};
IOReturn result(enterprise::Result r) {
    using R=enterprise::Result;
    switch(r){
    case R::ok:case R::empty:return kIOReturnSuccess;
    case R::busy:case R::full:return kIOReturnBusy;
    case R::invalid:return kIOReturnBadArgument;
    default:return kIOReturnNotReady;
    }
}
}

IOReturn itlmt7922::epCall(uintptr_t owner,uint32_t selector,const void *input,size_t length,itl_ep_reply *out)
{
    if (!owner || !out || !input || !cmdLock || shouldStop() || !m3On || !wstate[0] || nativeMode)
        return kIOReturnNotReady;
    bzero(out,sizeof(*out));
    EpLock lock(cmdLock);
    const uint64_t now=nowMs();
    if(selector==ITL_EP_JOIN){
        if(length!=sizeof(itl_ep_join))return kIOReturnBadArgument;
        const auto *j=static_cast<const itl_ep_join *>(input);
        if(j->abi!=ITL_EP_ABI || j->reserved || !j->ssid_length || j->ssid_length>32 ||
           memchr(j->ssid,0,j->ssid_length) || j->rsn_length>sizeof(j->rsn) ||
           !itl_ep_rsn(j->rsn,j->rsn_length,true))return kIOReturnUnsupported;
        if(epOwner || m3SessionActive || (cmdBits&(CMD_JOIN|CMD_DISCONNECT)))return kIOReturnBusy;
        // Reserved placeholder; worker binds the verified AP before association completes.
        const uint8_t placeholder[6]={2,0,0,0,0,1};
        auto r=epChannel.begin(owner,placeholder,now,epEpoch);
        if(r!=enterprise::Result::ok)return result(r);
        epOwner=owner;epState=1;epPmkReady=false;bzero(epPmk,sizeof(epPmk));
        epRequestRsnLen=epApRsnLen=0;bzero(epBssid,sizeof(epBssid));
        bzero(&reqNet,sizeof(reqNet));
        memcpy(reqNet.ssid,j->ssid,j->ssid_length);
        reqNet.enterprise=true;reqNet.enterpriseEpoch=epEpoch;
        reqNet.rsnLength=(uint16_t)j->rsn_length;memcpy(reqNet.rsn,j->rsn,j->rsn_length);
        cmdBits=(cmdBits&~(uint32_t)CMD_DISCONNECT)|CMD_JOIN;
    }else{
        if(length!=sizeof(itl_ep_request))return kIOReturnBadArgument;
        const auto *q=static_cast<const itl_ep_request *>(input);
        if(q->abi!=ITL_EP_ABI || q->length>ITL_EP_FRAME_MAX)return kIOReturnBadArgument;
        // Read-only bootstrap info for discovering the local MAC.
        bool bootstrap=selector==ITL_EP_POLL && q->epoch==0 && q->length==0;
        if(!bootstrap && (epOwner!=owner || q->epoch!=epEpoch))return kIOReturnNotReady;
        if(!bootstrap){
            auto r=epChannel.heartbeat(owner,epEpoch,now);
            if(r!=enterprise::Result::ok)return result(r);
            if(selector==ITL_EP_TX){
                if(q->length<4 || q->data[1]==3)return kIOReturnUnsupported;
                r=epChannel.send(owner,epEpoch,q->data,q->length,now);
                if(r!=enterprise::Result::ok)return result(r);
            }else if(selector==ITL_EP_PMK){
                if(q->length!=32 || epState!=2 || epPmkReady)return kIOReturnNotReady;
                memcpy(epPmk,q->data,32);epPmkReady=true;
            }else if(selector==ITL_EP_LEAVE){
                if(q->length)return kIOReturnBadArgument;
                epChannel.end(owner,epEpoch,now);epState=5;
                cmdBits=(cmdBits&~(uint32_t)CMD_JOIN)|CMD_DISCONNECT;
            }else if(selector==ITL_EP_POLL){
                if(q->length)return kIOReturnBadArgument;
                // Use pre-zeroed reply buffer directly; do not put a 4KB Frame on kernel stack.
                // A temporary is allocated only for polls and erased before freeing.
                auto *f=(enterprise::Frame *)IOMalloc(sizeof(enterprise::Frame));
                if(!f)return kIOReturnNoMemory;
                r=epChannel.read(owner,epEpoch,*f,now);
                if(r==enterprise::Result::ok && f->length<=ITL_EP_FRAME_MAX){
                    out->length=f->length;memcpy(out->data,f->bytes,f->length);
                }
                enterprise::erase(f,sizeof(*f));IOFree(f,sizeof(*f));
                if(r!=enterprise::Result::ok && r!=enterprise::Result::empty)return result(r);
            }else return kIOReturnUnsupported;
        }
    }
    out->abi=ITL_EP_ABI;out->epoch=epEpoch;out->state=epState;out->channel=tgtCh;
    memcpy(out->mac,mac,6);memcpy(out->bssid,epBssid,6);
    out->ap_rsn_length=epApRsnLen;out->request_rsn_length=epRequestRsnLen;
    memcpy(out->ap_rsn,epApRsn,epApRsnLen);memcpy(out->request_rsn,epRequestRsn,epRequestRsnLen);
    return kIOReturnSuccess;
}

void itlmt7922::epClose(uintptr_t owner)
{
    if(!cmdLock)return;
    EpLock lock(cmdLock);
    if(epOwner!=owner)return;
    epChannel.owner_closed(owner);epState=5; // worker releases ownership after cleanup
    enterprise::erase(epPmk,sizeof(epPmk));epPmkReady=false;
    cmdBits=(cmdBits&~(uint32_t)CMD_JOIN)|CMD_DISCONNECT;
}

void itlmt7922::epAbort(bool cancelledOnly)
{
    EpLock lock(cmdLock);
    if(cancelledOnly && epState!=5)return;
    epChannel.firmware_reset();epChannel.cleanup_complete();
    epOwner=0;epState=5;epPmkReady=false;
    epRekey.reset();
    enterprise::erase(epPmk,sizeof(epPmk));
    bzero(epBssid,sizeof(epBssid));epApRsnLen=epRequestRsnLen=0;
}

bool itlmt7922::epService()
{
    if(!m3Enterprise || !cmdLock)return true;
    // Firmware commands pump RX/TX while waiting. Never re-enter the service
    // with the same scratch EAPOL frame or key-install transaction.
    if(epServicing)return true;
    epServicing=true;
    struct ServiceScope {bool &active;~ServiceScope(){active=false;}} scope{epServicing};
    bool ok=false,send=false;
    {
        EpLock lock(cmdLock);
        // A completed session is not a newly failed session. The supervisor
        // has released ownership; idle pumps must not keep queuing DISCONNECT.
        if(!epOwner)return true;
        // Every transition to failed already schedules its one disconnect.
        if(epState==5)return false;
        epChannel.tick(nowMs());
        ok=!epChannel.cleanup_required();
        if(ok)send=epChannel.transmit_next(epEpoch,epFrame,nowMs())==enterprise::Result::ok;
    }
    if(send){
        // EAP-Packet, Start and Logoff are transmitted without encryption during EAP.
        // Keep this in the worker context, with the same ordering as key installation.
        ok=m3TxData(tgtBssid,0x888e,epFrame.bytes,epFrame.length,false);
        enterprise::erase(&epFrame,sizeof(epFrame));
    }
    if(ok&&m3linkUp&&!cmdPending(CMD_JOIN|CMD_DISCONNECT))ok=epGroupService();
    if(!ok){
        if(!m3lost){m3lost=true;strlcpy(m3lostWhy,"enterprise helper/session ended",sizeof(m3lostWhy));}
        EpLock lock(cmdLock);
        epState=5;cmdBits=(cmdBits&~(uint32_t)CMD_JOIN)|CMD_DISCONNECT;
    }
    return ok;
}

bool itlmt7922::epGroupService()
{
    for(unsigned drained=0;drained<4&&m3EapolNext();++drained){
        ItlEnterpriseRekey::Update update;
        if(!epRekey.evaluate(m3eapol,m3eapolLen,m3ptk,apRsn,apRsnLen,update)){
            ++epKeyRejects;continue;
        }
        if(update.reconnect){
            m3lost=true;
            strlcpy(m3lostWhy,update.pairwise?"enterprise pairwise rekey requires reassociation":
                    "enterprise GTK replay floor requires reassociation",sizeof(m3lostWhy));
            setProperty("wifi-enterprise-key-updates",update.pairwise?"reconnect: pairwise rekey":
                        "reconnect: GTK replay floor not supported");
            return false;
        }
        if(update.install){
            if(m3Key(M3_BMC_WCID,false,update.id,update.gtk,16))return false;
            ++epGroupUpdates;
        }else ++epKeyRetries;
        // Commit after install even if ACK TX fails: the retry must never
        // reinstall the same key and reset its packet-number state.
        epRekey.commit(update);
        if(!shouldStop()&&!firmwareFault&&!cmdPending(CMD_JOIN|CMD_DISCONNECT))
            m3TxData(tgtBssid,0x888e,update.reply,sizeof(update.reply),true);
        char summary[100];
        snprintf(summary,sizeof(summary),"group=%u retries=%u rejected=%u",epGroupUpdates,epKeyRetries,epKeyRejects);
        setProperty("wifi-enterprise-key-updates",summary);
    }
    return true;
}

void itlmt7922::epReceive(const uint8_t *source,const uint8_t *p,uint32_t length)
{
    if(!p || !source || length<4 || memcmp(source,tgtBssid,6))return;
    // Ethernet-origin EAP may carry padding. Bound and normalize the declared
    // EAPOL length before applying the strict queue/parser validation.
    const uint32_t wireLength=4u+(uint32_t(p[2])<<8)+p[3];
    if(wireLength>length || wireLength>ITL_EP_FRAME_MAX)return;
    length=wireLength;
    if(!enterprise::valid_frame(p,length) || (p[1]!=0 && p[1]!=3))return;
    // Key frames are handled in the worker, outside RX parsing. EAP
    // reauthentication still needs a new session; duplicate EAP Success
    // must not disconnect an authorized link.
    if(m3linkUp){
        if(p[1]==3){if(!m3Eapol(p,length))++epKeyRejects;return;}
        if(p[4]==3)return;
        m3lost=true;strlcpy(m3lostWhy,"enterprise EAP reauthentication requires reconnect",sizeof(m3lostWhy));
        {EpLock lock(cmdLock);epState=5;cmdBits=(cmdBits&~(uint32_t)CMD_JOIN)|CMD_DISCONNECT;}
        return;
    }
    if(p[1]==3){if(m3wantEapol)m3Eapol(p,length);return;}
    EpLock lock(cmdLock);
    const auto r=epChannel.receive(epEpoch,source,p,length,nowMs());
    if(r==enterprise::Result::full){epState=5;cmdBits|=CMD_DISCONNECT;}
}

int itlmt7922::epAuthenticate()
{
    epRekey.reset();epGroupUpdates=epKeyRetries=epKeyRejects=0;
    removeProperty("wifi-enterprise-key-updates");
    {
        EpLock lock(cmdLock);
        if(!epOwner || epState!=1 || epChannel.heartbeat(epOwner,epEpoch,nowMs())!=enterprise::Result::ok)return -5;
        if(epChannel.bind_peer(epEpoch,tgtBssid)!=enterprise::Result::ok ||
           epChannel.associated(epEpoch,nowMs())!=enterprise::Result::ok)return -5;
        epApRsnLen=apRsnLen;memcpy(epApRsn,apRsn,apRsnLen);
        memcpy(epBssid,tgtBssid,6);epState=2;
    }
    m3dataOn=true;m3wantEapol=true;
    m3eapolHead=m3eapolTail=m3eapolDrops=0;
    m3Roc(false); // EAP can take much longer than the management ROC grant
    m3Publish("enterprise-eap");
    const uint32_t started=nowMs();
    bool ready=false;
    while(!shouldStop() && !firmwareFault && !cmdPending(CMD_JOIN|CMD_DISCONNECT) && nowMs()-started<120000){
        m3Pump(1);
        EpLock lock(cmdLock);
        if(epState==5 || !epOwner)break;
        if(epPmkReady){
            memcpy(m3pmk,epPmk,32);enterprise::erase(epPmk,32);epPmkReady=false;
            epState=3;ready=true;break;
        }
    }
    if(!ready){m3Publish("enterprise-eap-fail");return -110;}
    m3Publish("enterprise-keys");
    int rc=m3Handshake();
    if(cmdPending(CMD_JOIN|CMD_DISCONNECT) || m3lost)rc=-5;
    return rc;
}
