#include "../AirportItlwm/HybridTraffic.hpp"
#include <assert.h>
#include <stdio.h>
int main() {
    using namespace AirportHybrid;
    uint8_t header[14] = {};
    assert(destination(nullptr, 14) == Destination::Drop);
    for (size_t length = 0; length < 14; ++length)
        assert(destination(header, length) == Destination::Drop);
    header[12] = 0x88; header[13] = 0x8e;
    assert(destination(header, sizeof(header)) == Destination::Control);
    header[12] = 0x08; header[13] = 0x00;
    assert(destination(header, sizeof(header)) == Destination::Data);
    header[13] = 0x06;
    assert(destination(header, sizeof(header)) == Destination::Data);
    header[12] = 0x86; header[13] = 0xdd;
    assert(destination(header, sizeof(header)) == Destination::Data);
    assert(!carrier(false, true, true, true));
    assert(!carrier(true, false, true, true));
    assert(!carrier(true, true, true, false));
    assert(carrier(true, true, true, true));
    assert(carrier(true, true, false, false));
    puts("hybrid routing and authenticated carrier tests passed");
}
