#include "../itlwm/hal_iwx/RxTiming.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

int main()
{
    using namespace IwxRxTiming;
    // Negotiated BlockAck inactivity is independent of the 100ms gap timeout.
    // At t=2s, a frame received at 1.8s keeps a negotiated 1s session alive.
    auto session = planDeadline(2000000, 1800000, 1000000);
    assert(session.enabled && !session.expired && session.milliseconds == 800);
    assert(planDeadline(2800000, 1800000, 1000000).expired);
    assert(!planDeadline(UINT64_MAX, 1, 0).enabled);
    // Short negotiated timeouts must expire without waiting 100ms.
    assert(planDeadline(10000, 0, 10000).expired);
    auto shortSession = planDeadline(9999, 0, 10000);
    assert(!shortSession.expired && shortSession.milliseconds == 1);

    // Simulate a missing sequence number while later MPDUs arrive every 10ms.
    // The first gap timer must still fire at 100ms, not slip to 190ms.
    uint64_t firingAt = 0;
    bool pending = false;
    for (uint64_t arrival = 0; arrival < 100000; arrival += 10000) {
        if (armGapTimer(true, false, pending)) {
            firingAt = arrival + 100000;
            pending = true;
        }
    }
    assert(firingAt == 100000);
    assert(planDeadline(firingAt, 0, 100000).expired);
    assert(!armGapTimer(false, false, true));
    assert(!armGapTimer(true, true, false));
    assert(armGapTimer(true, false, false));

    // If the older frame was released, an early callback for a newer frame
    // must wait only its remaining lifetime, not another full 100ms.
    auto newer = planDeadline(100000, 90000, 100000);
    assert(!newer.expired && newer.milliseconds == 90);
    // Repeated subframes sharing a sequence use their first arrival's time.
    const uint64_t firstSubframe = 0;
    auto subframe = planDeadline(99000, firstSubframe, 100000);
    assert(subframe.milliseconds == 1);
    assert(planDeadline(100000, firstSubframe, 100000).expired);

    // Positive microsecond delays round up and never become a zero-ms spin.
    for (uint64_t remainder = 1; remainder <= 100000; ++remainder) {
        auto deadline = planDeadline(100000 - remainder, 0, 100000);
        assert(!deadline.expired);
        assert(uint64_t(deadline.milliseconds) * 1000 >= remainder);
        assert(uint64_t(deadline.milliseconds) * 1000 - remainder < 1000);
    }
    assert(planDeadline(0, UINT64_MAX, 100000).milliseconds == 100);
    assert(planDeadline(UINT64_MAX - 100, UINT64_MAX - 200, 100000).milliseconds == 100);
    assert(planDeadline(0, 0, UINT64_MAX).milliseconds == 0x7fffffffU);

    // The wrap must happen before modulo for non-power-of-two BA windows.
    assert(sequenceAt(4095, 1) == 0);
    assert(sequenceAt(4095, 1) % 63 == 0);
    assert(sequenceAt(4095, 2) % 63 == 1);
    assert(sequenceAt(4090, 10) == 4);
    puts("iwx RX timing tests passed");
}
