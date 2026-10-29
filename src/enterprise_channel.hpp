// SPDX-License-Identifier: BSD-3-Clause-Clear
// Serialized kernel/user EAPOL channel; used by the enterprise development driver.
// The eventual caller must serialize all calls with a lock. owner is a kernel
// UserClient identity, NEVER a user-supplied identifier. epoch is a non-secret
// generation number: access control requires both owner and epoch.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace enterprise {
constexpr size_t max_frame = 4096;
constexpr size_t slots = 8;
constexpr uint64_t lease_ms = 10000;
enum class Result { ok, invalid, busy, stale, empty, full, not_associated, expired };
enum class State { idle, associating, authenticating, failed };
struct Frame {
    uint64_t epoch;
    uint32_t length;
    uint8_t bytes[max_frame];
};
inline void erase(void *data, size_t size) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size--) *p++ = 0;
}
// Supports EAP-Packet and EAPOL-Key. TLS fragmentation is handled by the
// supplicant; EAPOL itself has no transport fragmentation in this channel.
inline bool valid_frame(const uint8_t *p, size_t n) {
    if (!p || n < 4 || n > max_frame || p[0] < 1 || p[0] > 3) return false;
    size_t payload = (size_t(p[2]) << 8) | p[3];
    if (payload != n - 4) return false;
    switch (p[1]) {
    case 0: { // EAP
        if (payload < 4) return false;
        const size_t eap_size = (size_t(p[6]) << 8) | p[7];
        if (eap_size != payload) return false;
        if (p[4] == 1 || p[4] == 2) return payload >= 5; // Request/Response with Type
        return (p[4] == 3 || p[4] == 4) && payload == 4; // Success/Failure
    }
    case 1: case 2: return payload == 0; // Start/Logoff
    case 3: // RSN EAPOL-Key, body 95 bytes plus key data
        return payload >= 95 && p[4] == 2 &&
            ((size_t(p[97]) << 8) | p[98]) == payload - 95;
    default: return false;
    }
}
struct Queue {
    Frame frames[slots]{};
    size_t head = 0, count = 0;
    void clear() { erase(frames, sizeof(frames)); head = count = 0; }
    bool push(uint64_t epoch, const uint8_t *p, size_t n) {
        if (count == slots) return false;
        Frame &f = frames[(head + count) % slots];
        f.epoch = epoch; f.length = uint32_t(n); memcpy(f.bytes, p, n); ++count;
        return true;
    }
    bool pop(Frame &out) {
        erase(&out, sizeof(out));
        if (!count) return false;
        Frame &f = frames[head];
        out = f; erase(&f, sizeof(f)); head = (head + 1) % slots; --count;
        return true;
    }
};
class Channel {
    uint64_t owner_ = 0, epoch_ = 0, touched_ = 0;
    uint8_t bssid_[6]{};
    State state_ = State::idle;
    Queue rx_, tx_;
    bool cleanup_ = false;
    bool active() const {
        return state_ == State::associating || state_ == State::authenticating;
    }
    void fail() {
        rx_.clear(); tx_.clear(); erase(bssid_, sizeof(bssid_));
        owner_ = 0; touched_ = 0; state_ = State::failed; cleanup_ = true;
    }
    Result check(uint64_t owner, uint64_t epoch, uint64_t now) {
        if (!active() || owner == 0 || owner != owner_ || epoch != epoch_) return Result::stale;
        if (now < touched_ || now - touched_ >= lease_ms) { fail(); return Result::expired; }
        return Result::ok;
    }
public:
    ~Channel() { rx_.clear(); tx_.clear(); erase(bssid_, sizeof(bssid_)); }
    State state() const { return state_; }
    // Data authorization is deliberately absent: this prototype cannot open
    // a network port. Key installation / authorization must be implemented
    // and tested separately in the actual driver integration.
    bool cleanup_required() const { return cleanup_; }
    void cleanup_complete() { if (!active()) { cleanup_ = false; state_ = State::idle; } }
    Result begin(uint64_t owner, const uint8_t *bssid, uint64_t now, uint64_t &epoch) {
        epoch = 0;
        if (!owner || !bssid || (bssid[0] & 1)) return Result::invalid;
        uint8_t any = 0; for (size_t i = 0; i < 6; ++i) any |= bssid[i];
        if (!any || epoch_ == UINT64_MAX) return Result::invalid;
        if (active() || cleanup_) return Result::busy;
        rx_.clear(); tx_.clear(); memcpy(bssid_, bssid, sizeof(bssid_));
        owner_ = owner; touched_ = now; ++epoch_; epoch = epoch_;
        state_ = State::associating; return Result::ok;
    }
    Result bind_peer(uint64_t epoch, const uint8_t *bssid) {
        if (epoch != epoch_ || state_ != State::associating) return Result::stale;
        if (!bssid || (bssid[0] & 1)) return Result::invalid;
        memcpy(bssid_,bssid,6); return Result::ok;
    }
    Result associated(uint64_t epoch, uint64_t now) {
        if (epoch != epoch_ || state_ != State::associating) return Result::stale;
        const auto r = check(owner_, epoch, now); if (r != Result::ok) return r;
        state_ = State::authenticating; return Result::ok;
    }
    Result heartbeat(uint64_t owner, uint64_t epoch, uint64_t now) {
        const auto r = check(owner, epoch, now); if (r == Result::ok) touched_ = now;
        return r;
    }
    Result send(uint64_t owner, uint64_t epoch, const uint8_t *p, size_t n, uint64_t now) {
        const auto r = check(owner, epoch, now); if (r != Result::ok) return r;
        if (state_ != State::authenticating) return Result::not_associated;
        if (!valid_frame(p, n)) return Result::invalid;
        if (!tx_.push(epoch, p, n)) { fail(); return Result::full; }
        touched_ = now; return Result::ok;
    }
    Result receive(uint64_t epoch, const uint8_t *source, const uint8_t *p, size_t n, uint64_t now) {
        if (!active() || epoch != epoch_) return Result::stale;
        const auto r = check(owner_, epoch, now); if (r != Result::ok) return r;
        if (state_ != State::authenticating) return Result::not_associated;
        if (!source || memcmp(source,bssid_,6) || !valid_frame(p,n) || (p[1] != 0 && p[1] != 3))
            return Result::invalid;
        if (!rx_.push(epoch,p,n)) { fail(); return Result::full; }
        // Radio traffic must not refresh a dead helper's ownership lease.
        return Result::ok;
    }
    Result read(uint64_t owner, uint64_t epoch, Frame &out, uint64_t now) {
        erase(&out,sizeof(out));
        const auto r = check(owner,epoch,now); if (r != Result::ok) return r;
        touched_ = now;
        return rx_.pop(out) ? Result::ok : Result::empty;
    }
    Result transmit_next(uint64_t epoch, Frame &out, uint64_t now) {
        erase(&out,sizeof(out));
        if (!active() || epoch != epoch_) return Result::stale;
        const auto r = check(owner_,epoch,now); if (r != Result::ok) return r;
        return tx_.pop(out) ? Result::ok : Result::empty;
    }
    Result end(uint64_t owner,uint64_t epoch,uint64_t now) {
        const auto r = check(owner,epoch,now); if (r != Result::ok) return r;
        fail(); return Result::ok;
    }
    void owner_closed(uint64_t owner) { if (active() && owner == owner_) fail(); }
    void firmware_reset() { if (active()) fail(); }
    void tick(uint64_t now) {
        if (active() && (now < touched_ || now - touched_ >= lease_ms)) fail();
    }
};
}
