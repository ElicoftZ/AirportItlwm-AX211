#include "../AirportItlwm/AWDL/Frame.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>

int main() {
    using namespace AirportAWDL;
    uint8_t source[] = {1, 2, 3, 4};
    uint8_t output[] = {9, 9, 9, 9, 9, 9};
    assert(!validTemplateRequest(nullptr, 4));
    assert(!validTemplateRequest(source, 0));
    assert(!validTemplateRequest(source, MaxTemplateSize + 1));
    assert(copyTemplate(source, 4, nullptr, 4) == CopyResult::TooSmall);
    assert(copyTemplate(source, 4, output, 3) == CopyResult::TooSmall);
    for (auto byte : output) assert(byte == 9); // No writes on a short buffer.
    assert(copyTemplate(source, 4, output, 4) == CopyResult::Copied);
    assert(memcmp(source, output, 4) == 0 && output[4] == 9 && output[5] == 9);
    ActionView view;
    assert(!parseAction(nullptr, 0, view));
    std::vector<uint8_t> body = {127, 0, 0x17, 0xf2, 8, 0x10, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0,
                               21, 2, 0, 0x10, 1, 11, 0, 0};
    assert(parseAction(body.data(), body.size(), view) && view.tlvCount == 2);
    for (size_t n = 0; n < ActionHeaderSize; ++n)
        assert(!parseAction(body.data(), n, view));
    for (size_t n = ActionHeaderSize + 1; n < 21; ++n)
        assert(!parseAction(body.data(), n, view));
    auto bad = body;
    bad[17] = 255; bad[18] = 255;
    assert(!parseAction(bad.data(), bad.size(), view) && view.body == nullptr);
    bad = body; bad[2] = 0;
    assert(!parseAction(bad.data(), bad.size(), view));
    bad = body; bad[5] = 0x20;
    assert(!parseAction(bad.data(), bad.size(), view));
    bad = body; bad[6] = 1;
    assert(!parseAction(bad.data(), bad.size(), view));
    body[6] = 3;
    assert(parseAction(body.data(), body.size(), view) && view.subtype == 3);
    std::vector<uint8_t> frame(24);
    frame[0] = 0xd0;
    frame.insert(frame.end(), body.begin(), body.end());
    assert(parseAction(frame.data(), frame.size(), view) && view.tlvCount == 2);
    frame[1] = 0x40;
    assert(!parseAction(frame.data(), frame.size(), view));
    frame[1] = 0x80;
    assert(!parseAction(frame.data(), frame.size(), view));
    // Deterministic malformed-input coverage, executed with ASan and UBSan.
    uint32_t random = 0x211;
    for (size_t length = 0; length <= MaxTemplateSize; ++length) {
        std::vector<uint8_t> bytes(length);
        for (auto &byte : bytes) {
            random = random * 1664525u + 1013904223u;
            byte = uint8_t(random >> 24);
        }
        parseAction(bytes.data(), bytes.size(), view);
    }
    puts("AWDL bounds, template copies, and malformed-frame tests passed");
}
