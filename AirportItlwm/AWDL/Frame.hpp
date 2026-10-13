// SPDX-License-Identifier: GPL-2.0
// Independent AWDL wire-format validation. No radio or authentication claims.
#ifndef AX211_AWDL_FRAME_HPP
#define AX211_AWDL_FRAME_HPP
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace AirportAWDL {
constexpr size_t MaxTemplateSize = 4096;
constexpr size_t ActionHeaderSize = 16;

inline bool validTemplateRequest(const void *payload, size_t length) {
    return payload && length && length <= MaxTemplateSize;
}

enum class CopyResult { Invalid, TooSmall, Copied };
inline CopyResult copyTemplate(const void *source, size_t length,
                               void *output, size_t capacity) {
    if (!validTemplateRequest(source, length)) return CopyResult::Invalid;
    if (!output || capacity < length) return CopyResult::TooSmall;
    memcpy(output, source, length);
    return CopyResult::Copied;
}

inline uint16_t readLE16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

struct ActionView {
    const uint8_t *body;
    size_t length;
    uint8_t subtype;
    size_t tlvCount;
};

// Accept a vendor action body or an unprotected 24-byte 802.11 action frame.
// Full frames must not include radiotap headers or an FCS trailer.
inline bool parseAction(const void *packet, size_t length, ActionView &out) {
    out = {};
    if (!packet || length > MaxTemplateSize) return false;
    const uint8_t *p = static_cast<const uint8_t *>(packet);
    if (length >= 24 && (p[0] & 0xfc) == 0xd0) {
        // Do not interpret encrypted payloads or unsupported ordered headers.
        if (p[1] & 0xc0) return false;
        p += 24;
        length -= 24;
    }
    if (length < ActionHeaderSize || p[0] != 127 || p[1] != 0 ||
        p[2] != 0x17 || p[3] != 0xf2 || p[4] != 8 || p[5] != 0x10 ||
        (p[6] != 0 && p[6] != 3)) return false;
    size_t count = 0;
    for (size_t offset = ActionHeaderSize; offset < length; ) {
        if (length - offset < 3) return false;
        size_t payload = readLE16(p + offset + 1);
        if (payload > length - offset - 3) return false;
        offset += 3 + payload;
        ++count;
    }
    out = {p, length, p[6], count};
    return true;
}
}
#endif
