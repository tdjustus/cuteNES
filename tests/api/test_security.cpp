// Q10 security (tasks A031, FR-008/FR-009/FR-024, research R11).

#include "http_support.hpp"

#include <server.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

int statusOf(const httplib::Result& r) { return r ? r->status : -1; }

const std::string kRpc = R"({"jsonrpc":"2.0","id":1,"method":"api.version"})";

std::filesystem::path tempFile(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("cutenes-security-" + name);
}

} // namespace

TEST(ApiSecurity, TokensAreRandomHexAndComparedWhole)
{
    const std::string a = token::generate();
    const std::string b = token::generate();
    ASSERT_EQ(a.size(), 64u); // 32 bytes
    EXPECT_EQ(a.find_first_not_of("0123456789abcdef"), std::string::npos);
    EXPECT_NE(a, b);
    EXPECT_TRUE(token::constantTimeEquals(a, a));
    EXPECT_FALSE(token::constantTimeEquals(a, b));
    EXPECT_FALSE(token::constantTimeEquals(a, a.substr(0, 63)));
}

TEST(ApiSecurity, RequestsNeedTheBearerToken)
{
    HttpFixture f;
    httplib::Client anonymous("127.0.0.1", f.server.port());
    const auto none = anonymous.Post("/api/v1/rpc", kRpc, "application/json");
    EXPECT_EQ(statusOf(none), 401);
    ASSERT_TRUE(none);
    EXPECT_EQ(none->get_header_value("WWW-Authenticate"), "Bearer");
    EXPECT_EQ(statusOf(anonymous.Get("/api/v1/events")), 401);

    httplib::Client wrong("127.0.0.1", f.server.port());
    wrong.set_bearer_token_auth(token::generate());
    EXPECT_EQ(statusOf(wrong.Post("/api/v1/rpc", kRpc, "application/json")), 401);

    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", kRpc, "application/json")), 200);
}

TEST(ApiSecurity, APreviousLaunchsTokenIsRejected)
{
    std::string oldToken;
    {
        HttpFixture first;
        oldToken = first.token;
    }
    HttpFixture second;
    httplib::Client stale("127.0.0.1", second.server.port());
    stale.set_bearer_token_auth(oldToken);
    EXPECT_EQ(statusOf(stale.Post("/api/v1/rpc", kRpc, "application/json")), 401);
}

TEST(ApiSecurity, RejectedRequestsDoNothing)
{
    HttpFixture f;
    httplib::Client anonymous("127.0.0.1", f.server.port());
    const std::string load = Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "core.load_rom"},
        {"params", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}}}}.dump();
    EXPECT_EQ(statusOf(anonymous.Post("/api/v1/rpc", load, "application/json")), 401);
    EXPECT_FALSE(ok(f.session, "session.status")["cartridgeLoaded"]);
}

TEST(ApiSecurity, ForeignHostAndOriginAreForbidden)
{
    HttpFixture f;
    // DNS rebinding: a page on evil.example resolving to 127.0.0.1.
    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", {{"Host", "evil.example"}}, kRpc,
                  "application/json")),
        403);
    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", {{"Origin", "http://evil.example"}}, kRpc,
                  "application/json")),
        403);
    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", {{"Origin", "null"}}, kRpc, "application/json")),
        403);
    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", {{"Origin", "http://localhost:3000"}}, kRpc,
                  "application/json")),
        200);
    EXPECT_EQ(statusOf(f.client.Post("/api/v1/rpc", {{"Host", "localhost"}}, kRpc,
                  "application/json")),
        200);
}

TEST(ApiSecurity, ListensOnLoopbackByDefault)
{
    HttpFixture f;
    EXPECT_EQ(f.server.url().rfind("http://127.0.0.1:", 0), 0u) << f.server.url();
    EXPECT_TRUE(isLoopbackHost("127.0.0.1"));
    EXPECT_TRUE(isLoopbackHost("::1"));
    EXPECT_TRUE(isLoopbackHost("localhost"));
    EXPECT_FALSE(isLoopbackHost("0.0.0.0"));
    EXPECT_FALSE(isLoopbackHost("192.168.1.2"));
    EXPECT_FALSE(isLoopbackHost("localhost.evil.example"));
}

TEST(ApiSecurity, TokenFileIsOwnerOnly)
{
    const auto path = tempFile("owner.token");
    std::string error;
    ASSERT_TRUE(token::writeFile(path, "http://127.0.0.1:1/api/v1", "abc", error)) << error;
    EXPECT_TRUE(token::isOwnerOnly(path));
    std::ifstream in(path);
    const Json j = Json::parse(in);
    EXPECT_EQ(j["token"], "abc");
    EXPECT_EQ(j["url"], "http://127.0.0.1:1/api/v1");
    // Rewriting replaces the file and keeps it owner-only.
    ASSERT_TRUE(token::writeFile(path, "u", "def", error)) << error;
    EXPECT_TRUE(token::isOwnerOnly(path));
#if !defined(_WIN32)
    std::filesystem::permissions(path, std::filesystem::perms::group_read,
        std::filesystem::perm_options::add);
    EXPECT_FALSE(token::isOwnerOnly(path));
#endif
    std::filesystem::remove(path);
}

TEST(ApiSecurity, ServerWithoutFlagsOpensNoListener)
{
    const std::atomic<bool> stop{true};
    std::ostringstream err;
    EXPECT_EQ(cutenes::server::run({}, err, stop), cutenes::server::kExitUsage);
    EXPECT_NE(err.str().find("--api-port"), std::string::npos);
    std::ostringstream usage;
    EXPECT_EQ(cutenes::server::run({"--bogus"}, usage, stop), cutenes::server::kExitUsage);
    EXPECT_EQ(cutenes::server::run({"--api-port", "70000"}, usage, stop), cutenes::server::kExitUsage);
}

TEST(ApiSecurity, ServerWritesTheTokenFileAndNeverPrintsTheToken)
{
    const auto path = tempFile("server.token");
    std::filesystem::remove(path);
    std::atomic<bool> stop{false};
    std::ostringstream err;
    int code = -1;
    std::thread server([&] {
        code = cutenes::server::run({"--api-port", "0", "--token-file", path.string(),
                                        "--library", romDir()},
            err, stop);
    });
    for (int i = 0; i < 200 && !std::filesystem::exists(path); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(token::isOwnerOnly(path));
    Json file;
    for (int i = 0; i < 40 && file.is_null(); ++i) { // the writer may still be mid-write
        std::ifstream in(path);
        file = Json::parse(in, nullptr, false);
        if (file.is_discarded()) file = Json();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const std::string tok = file.value("token", "");
    const std::string url = file.value("url", "");
    ASSERT_EQ(tok.size(), 64u);

    // The file is all a script needs.
    const auto hostStart = std::string("http://").size();
    const auto colon = url.find(':', hostStart);
    httplib::Client client(url.substr(hostStart, colon - hostStart),
        std::stoi(url.substr(colon + 1)));
    client.set_bearer_token_auth(tok);
    const auto res = client.Post("/api/v1/rpc", kRpc, "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(Json::parse(res->body)["result"]["mode"], "headless");

    stop = true;
    server.join();
    EXPECT_EQ(code, cutenes::server::kExitOk);
    EXPECT_FALSE(std::filesystem::exists(path)); // removed on clean exit
    EXPECT_EQ(err.str().find(tok), std::string::npos) << "the token reached stderr";
    EXPECT_NE(err.str().find(path.string()), std::string::npos);
}
