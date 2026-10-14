/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IWX_RX_TIMING_HPP
#define IWX_RX_TIMING_HPP

#include <stdint.h>

namespace IwxRxTiming {

struct Deadline {
    bool enabled;
    bool expired;
    uint32_t milliseconds;
};

// Use elapsed monotonic time, avoiding overflow when constructing an absolute
// deadline. A disabled negotiated BA timeout must never tear down the session.
inline Deadline planDeadline(uint64_t nowUsec, uint64_t receivedUsec,
                             uint64_t timeoutUsec)
{
    if (!timeoutUsec)
        return {false, false, 0};
    const uint64_t elapsed = nowUsec >= receivedUsec ? nowUsec - receivedUsec : 0;
    if (elapsed >= timeoutUsec)
        return {true, true, 0};
    const uint64_t remaining = timeoutUsec - elapsed;
    const uint64_t milliseconds = remaining / 1000 + (remaining % 1000 != 0);
    // The macOS timeout wrapper accepts a signed number of milliseconds.
    return {true, false, uint32_t(milliseconds > 0x7fffffffULL ?
                                0x7fffffffULL : milliseconds)};
}

// Do not postpone an already armed gap timer on each incoming MPDU/release.
inline bool armGapTimer(bool stored, bool removed, bool pending)
{
    return stored && !removed && !pending;
}

// Sequence numbers wrap at 4096 before indexing the negotiated buffer size.
// A non-power-of-two window does not divide 4096 evenly.
inline uint16_t sequenceAt(uint16_t head, unsigned offset)
{
    return uint16_t((unsigned(head) + offset) & 0xfff);
}

} // namespace IwxRxTiming
#endif
