#pragma once

// Helpers shared by the cutenes-api tests.

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>

#include "media.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include <cstdint>
#include <string>
#include <vector>

namespace cutenes::api::test {

inline std::string romDir() { return CUTENES_TEST_ROMS; }
inline std::string sourceDir() { return CUTENES_SOURCE_DIR; }

// A session whose ROM library is the test-ROM submodule and whose results
// are checked against their output schemas.
inline SessionConfig testConfig()
{
    SessionConfig c;
    c.libraryRoot = romDir();
    c.validateOutputs = true;
    return c;
}

// The call must succeed; returns its value.
inline Json ok(Session& s, const std::string& name, const Json& args = Json::object())
{
    const Result r = s.call(name, args);
    EXPECT_FALSE(r.isError()) << name << " " << args.dump() << ": "
                              << (r.error ? r.error->name + " " + r.error->message : "");
    return r.value;
}

// The call must fail with `errorName`; returns the error data.
inline Json fails(Session& s, const std::string& name, const Json& args,
    const std::string& errorName)
{
    const Result r = s.call(name, args);
    EXPECT_TRUE(r.isError()) << name << " " << args.dump() << " unexpectedly succeeded";
    if (!r.isError()) {
        return Json::object();
    }
    EXPECT_EQ(r.error->name, errorName) << name << ": " << r.error->message;
    return r.error->data;
}

inline std::vector<std::uint8_t> bytesOf(const Json& value)
{
    return media::base64Decode(value.get<std::string>()).value_or(std::vector<std::uint8_t>{});
}

inline std::string b64(const std::vector<std::uint8_t>& v) { return media::base64Encode(v); }

// A minimal NROM-128 image: 16 KiB PRG of an endless loop, 8 KiB CHR.
// `battery` sets flags 6 bit 1 (8 KiB battery-backed PRG-RAM).
inline std::vector<std::uint8_t> tinyRom(bool battery = false)
{
    std::vector<std::uint8_t> rom(16 + 16384 + 8192, 0);
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = battery ? 0x02 : 0x00;
    // $C000: JMP $C000. Vectors at $FFFA-$FFFF all point at it.
    rom[16] = 0x4C; rom[17] = 0x00; rom[18] = 0xC0;
    for (std::size_t v = 0x3FFA; v < 0x4000; v += 2) {
        rom[16 + v] = 0x00;
        rom[16 + v + 1] = 0xC0;
    }
    return rom;
}

// Uploads bytes through the blob commands and returns the committed id.
inline std::string upload(Session& s, const std::vector<std::uint8_t>& data)
{
    const std::string id = ok(s, "session.blob_begin", {{"size", data.size()}})["blobId"];
    for (std::size_t off = 0; off < data.size(); off += kMaxBytesPerCall) {
        const auto end = std::min(data.size(), off + kMaxBytesPerCall);
        ok(s, "session.blob_chunk", {{"blobId", id}, {"offset", off},
            {"bytes", b64(std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(off),
                          data.begin() + static_cast<std::ptrdiff_t>(end)))}});
    }
    ok(s, "session.blob_commit", {{"blobId", id}});
    return id;
}

// Reads a whole blob through session.blob_read.
inline std::vector<std::uint8_t> download(Session& s, const std::string& id)
{
    std::vector<std::uint8_t> out;
    for (;;) {
        const Json r = ok(s, "session.blob_read",
            {{"blobId", id}, {"offset", out.size()}, {"length", kMaxBytesPerCall}});
        const auto chunk = bytesOf(r["bytes"]);
        out.insert(out.end(), chunk.begin(), chunk.end());
        if (r["eof"] || chunk.empty()) {
            return out;
        }
    }
}

// The machine's full observable state: a capture_state blob. Equal bytes
// mean an operation changed nothing.
inline std::vector<std::uint8_t> stateBytes(Session& s)
{
    const Json r = ok(s, "core.capture_state");
    auto bytes = download(s, r["blobId"]);
    ok(s, "session.blob_delete", {{"blobId", r["blobId"]}});
    return bytes;
}

inline std::uint8_t peekByte(Session& s, std::uint16_t addr)
{
    return bytesOf(ok(s, "core.peek", {{"addr", addr}, {"length", 1}})["bytes"]).at(0);
}

} // namespace cutenes::api::test
