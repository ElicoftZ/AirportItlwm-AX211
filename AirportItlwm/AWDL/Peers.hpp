// SPDX-License-Identifier: GPL-2.0
#ifndef AX211_AWDL_PEERS_HPP
#define AX211_AWDL_PEERS_HPP
#include "Frame.hpp"

namespace AirportAWDL {
constexpr size_t PeerCapacity = 16;
constexpr uint64_t PeerLifetimeMS = 10000;
struct ObservedPeer {
    bool used;
    uint8_t address[6];
    uint64_t lastSeenMS;
    uint64_t frames;
    uint8_t subtype;
};

// Observation is not authentication or an established AWDL data link.
// No frame pointers are retained beyond the receive callback.
class PeerTable {
    ObservedPeer peers[PeerCapacity] = {};
public:
    void expire(uint64_t nowMS) {
        for (auto &peer : peers)
            if (peer.used && nowMS >= peer.lastSeenMS &&
                nowMS - peer.lastSeenMS >= PeerLifetimeMS) peer = {};
    }
    size_t count() const {
        size_t result = 0;
        for (const auto &peer : peers) if (peer.used) ++result;
        return result;
    }
    const ObservedPeer *find(const uint8_t *address) const {
        if (!address) return nullptr;
        for (const auto &peer : peers)
            if (peer.used && memcmp(peer.address, address, 6) == 0) return &peer;
        return nullptr;
    }
    bool observe(const void *frame, size_t length, uint64_t nowMS) {
        ActionView view;
        if (!frame || length < 24 + ActionHeaderSize ||
            (static_cast<const uint8_t *>(frame)[0] & 0xfc) != 0xd0 ||
            !parseAction(frame, length, view)) return false;
        const auto address = static_cast<const uint8_t *>(frame) + 10;
        uint8_t bits = 0;
        for (unsigned i = 0; i < 6; ++i) bits |= address[i];
        if (!bits || (address[0] & 1)) return false;
        expire(nowMS);
        ObservedPeer *slot = nullptr;
        for (auto &peer : peers) {
            if (peer.used && memcmp(peer.address, address, 6) == 0) {
                if (nowMS < peer.lastSeenMS) return false;
                slot = &peer;
                break;
            }
        }
        if (!slot) {
            for (auto &peer : peers) if (!peer.used) { slot = &peer; break; }
            if (!slot) {
                slot = &peers[0];
                for (auto &peer : peers)
                    if (peer.lastSeenMS < slot->lastSeenMS) slot = &peer;
                if (nowMS < slot->lastSeenMS) return false;
            }
            *slot = {};
            slot->used = true;
            memcpy(slot->address, address, 6);
        }
        slot->lastSeenMS = nowMS;
        ++slot->frames;
        slot->subtype = view.subtype;
        return true;
    }
};
}
#endif
