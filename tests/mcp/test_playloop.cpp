// Q3 over MCP (A042): an agent's play loop through cutenes-server on stdio.
// The expectation is the ROM's own verdict: blargg instr_test-v5
// official_only reports status 0 and "All 16 tests passed" (readme.txt).
// Q7's MCP leg: the same script gives the same hashes in process.

#include "mcp_support.hpp"

#include "blargg.hpp"

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

namespace {

class McpClient {
public:
    explicit McpClient(StdioServer& s) : m_server(s) {}

    Json call(const std::string& tool, const Json& args = Json::object())
    {
        return m_server.request(toolCall(tool, args, ++m_id));
    }
    Json ok(const std::string& tool, const Json& args = Json::object())
    {
        return structured(call(tool, args));
    }
    int peekByte(int addr)
    {
        const auto bytes = cutenes::api::test::bytesOf(ok("core.peek", {{"addr", addr}, {"length", 1}})["bytes"]);
        return bytes.empty() ? -1 : bytes[0];
    }

private:
    StdioServer& m_server;
    int m_id = 0;
};

std::vector<std::uint8_t> decode(const Json& base64)
{
    return cutenes::api::test::bytesOf(base64);
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16)
        | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

} // namespace

TEST(McpPlayLoop, AnAgentRunsATestRomToItsDocumentedVerdict)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    McpClient mcp(server);
    Json loaded = mcp.ok("core.load_rom", {{"path", "instr_test-v5/official_only.nes"}});
    EXPECT_EQ(loaded["cartridge"]["mapper"], 1);

    // The blargg $6000 protocol: signature DE B0 61 at $6001, then the
    // status at $6000 ($80 running, $81 reset request, below $80 done).
    bool signature = false;
    for (int i = 0; i < 600 && !signature; ++i) {
        mcp.ok("core.run_frames", {{"frames", 1}});
        signature = decode(mcp.ok("core.peek", {{"addr", 0x6001}, {"length", 3}})["bytes"])
            == std::vector<std::uint8_t>{0xDE, 0xB0, 0x61};
    }
    ASSERT_TRUE(signature);
    int status = -1;
    for (int i = 0; i < 400; ++i) {
        status = mcp.peekByte(0x6000);
        if (status == 0x81) {
            mcp.ok("core.run_frames", {{"frames", 7}});
            mcp.ok("core.reset");
            mcp.ok("core.run_frames", {{"frames", 7}});
            continue;
        }
        if (status < 0x80) break;
        mcp.ok("core.run_frames", {{"frames", 30}});
    }
    EXPECT_EQ(status, 0);
    std::string text;
    for (const auto c : decode(mcp.ok("core.peek", {{"addr", 0x6004}, {"length", 512}})["bytes"])) {
        if (c == 0) break;
        text += static_cast<char>(c);
    }
    EXPECT_NE(text.find("All 16 tests passed"), std::string::npos) << text;

    // The screen arrives as an MCP image block, a 256x240 PNG.
    Json screen = mcp.call("core.screen");
    const Json& content = screen["result"]["content"];
    const auto image = std::find_if(content.begin(), content.end(),
        [](const Json& b) { return b.value("type", "") == "image"; });
    ASSERT_NE(image, content.end()) << screen.dump().substr(0, 400);
    EXPECT_EQ((*image)["mimeType"], "image/png");
    const auto png = decode((*image)["data"]);
    ASSERT_GE(png.size(), 24u);
    EXPECT_EQ(std::vector<std::uint8_t>(png.begin(), png.begin() + 8),
        (std::vector<std::uint8_t>{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}));
    EXPECT_EQ(std::string(png.begin() + 12, png.begin() + 16), "IHDR");
    EXPECT_EQ(be32(png, 16), 256u);
    EXPECT_EQ(be32(png, 20), 240u);
    // The text block is for the model: it carries the result but not the
    // image's base64 a second time.
    const auto text_block = std::find_if(content.begin(), content.end(),
        [](const Json& b) { return b.value("type", "") == "text"; });
    ASSERT_NE(text_block, content.end());
    EXPECT_EQ((*text_block)["text"].get<std::string>().find((*image)["data"].get<std::string>().substr(0, 64)),
        std::string::npos);
    EXPECT_EQ(screen["result"]["structuredContent"]["frameHash"], mcp.ok("core.frame_hash")["frameHash"]);
}

TEST(McpPlayLoop, TheSameScriptGivesTheSameHashesOverMcpAndInProcess)
{
    const std::vector<std::pair<std::string, Json>> script = {
        {"core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}}},
        {"core.run_frames", {{"frames", 90}, {"script", Json::array({Json::array({"Start"}), Json::array()})}}},
        {"core.frame_hash", Json::object()},
        {"core.run_frames", {{"frames", 45}}},
        {"core.get_cpu", Json::object()},
    };
    StdioServer server;
    ASSERT_TRUE(server.started);
    McpClient mcp(server);
    cutenes::api::Session local(cutenes::api::test::testConfig());
    for (const auto& [name, args] : script) {
        Json over = mcp.ok(name, args);
        Json here = cutenes::api::test::ok(local, name, args);
        EXPECT_EQ(over, here) << name;
    }
}
