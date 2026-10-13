#include "../AirportItlwm/AWDL/Peers.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
int main() {
    using namespace AirportAWDL;
    std::vector<uint8_t> frame(24 + ActionHeaderSize);
    frame[0] = 0xd0; frame[10] = 2; frame[15] = 1;
    uint8_t header[] = {127, 0, 0x17, 0xf2, 8, 0x10, 0, 0};
    memcpy(frame.data() + 24, header, sizeof(header));
    PeerTable peers;
    assert(peers.observe(frame.data(), frame.size(), 0));
    assert(peers.count() == 1 && peers.find(frame.data() + 10)->frames == 1);
    assert(peers.observe(frame.data(), frame.size(), 1));
    assert(peers.count() == 1 && peers.find(frame.data() + 10)->frames == 2);
    assert(!peers.observe(frame.data(), frame.size(), 0));
    auto malformed = frame; malformed.push_back(4);
    assert(!peers.observe(malformed.data(), malformed.size(), 2));
    assert(peers.find(frame.data() + 10)->frames == 2);
    frame[10] = 3;
    assert(!peers.observe(frame.data(), frame.size(), 2));
    frame[10] = 0; frame[15] = 0;
    assert(!peers.observe(frame.data(), frame.size(), 2));
    frame[10] = 2;
    for (unsigned id = 1; id <= PeerCapacity; ++id) {
        frame[15] = uint8_t(id);
        assert(peers.observe(frame.data(), frame.size(), id + 2));
    }
    assert(peers.count() == PeerCapacity);
    frame[15] = 99;
    assert(peers.observe(frame.data(), frame.size(), 100));
    assert(peers.count() == PeerCapacity);
    uint8_t oldest[] = {2, 0, 0, 0, 0, 1};
    assert(peers.find(oldest) == nullptr);
    peers.expire(100 + PeerLifetimeMS);
    assert(peers.count() == 0);
    for (size_t length = 0; length < frame.size(); ++length)
        assert(!peers.observe(frame.data(), length, 20000));
    puts("AWDL peer identity, refresh, expiry, capacity and truncation tests passed");
}
