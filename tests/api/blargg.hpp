#pragma once

// The blargg $6000 console protocol, driven only through API commands:
// signature DE B0 61 at $6001-$6003, status at $6000 ($80 running, $81
// reset request, $00-$7F the result code), text at $6004.

#include "support.hpp"

namespace cutenes::api::test {

struct BlarggVerdict {
    bool signature = false;
    int status = -1;
    std::string text;
};

inline BlarggVerdict runBlargg(Session& s, int maxFrames = 3600)
{
    BlarggVerdict v;
    for (int frame = 0; frame < 600 && !v.signature; ++frame) {
        ok(s, "core.run_frames", {{"frames", 1}});
        const auto sig = bytesOf(ok(s, "core.peek", {{"addr", 0x6001}, {"length", 3}})["bytes"]);
        v.signature = sig == std::vector<std::uint8_t>{0xDE, 0xB0, 0x61};
    }
    if (!v.signature) {
        return v;
    }
    for (int frame = 0; frame < maxFrames; ++frame) {
        v.status = peekByte(s, 0x6000);
        if (v.status == 0x81) {
            ok(s, "core.run_frames", {{"frames", 7}});
            ok(s, "core.reset");
            ok(s, "core.run_frames", {{"frames", 7}});
            continue;
        }
        if (v.status < 0x80) {
            break;
        }
        ok(s, "core.run_frames", {{"frames", 1}});
    }
    const auto text = bytesOf(ok(s, "core.peek", {{"addr", 0x6004}, {"length", 512}})["bytes"]);
    for (const auto c : text) {
        if (c == 0) break;
        v.text += static_cast<char>(c);
    }
    return v;
}

} // namespace cutenes::api::test
