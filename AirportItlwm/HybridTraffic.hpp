/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AIRPORT_HYBRID_TRAFFIC_HPP
#define AIRPORT_HYBRID_TRAFFIC_HPP
#include <stddef.h>
#include <stdint.h>
namespace AirportHybrid {
enum class Destination { Drop, Control, Data };
inline Destination destination(const uint8_t *header, size_t length) {
    if (!header || length < 14) return Destination::Drop;
    return header[12] == 0x88 && header[13] == 0x8e ?
        Destination::Control : Destination::Data;
}
inline bool carrier(bool running, bool associated, bool protectedNetwork, bool authorized) {
    return running && associated && (!protectedNetwork || authorized);
}
}
#endif
