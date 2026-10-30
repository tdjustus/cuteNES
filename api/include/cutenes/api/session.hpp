#pragma once

#include <cutenes/api/command.hpp>
#include <cutenes/api/host.hpp>
#include <cutenes/api/registry.hpp>
#include <cutenes/machine.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace cutenes::api {

struct SessionConfig {
    MachineConfig machine;
    // ROM library root for core.load_rom paths; empty means uploads only.
    std::string libraryRoot;
    // Where session.write_dump writes <datetime>.dmp files.
    std::string debugDir = "debug";
    // Registers host commands (gui.*); null means a HeadlessHost.
    IHost* host = nullptr;
    // core.run_frames refuses more frames per call (LimitExceeded); the
    // schema allows up to 216000 (one hour of NTSC).
    std::uint32_t maxRunFrames = 3600;
    // Check every result against its command's outputSchema and report a
    // mismatch as InternalError (the test suite turns this on).
    bool validateOutputs = false;
};

using CancelFlag = std::shared_ptr<std::atomic<bool>>;

// The one emulation session of a process (data-model.md "Session"). Only
// its emulation thread touches the Machine: every call is queued and runs
// to completion in arrival order (FR-007, research R5).
class Session {
public:
    explicit Session(SessionConfig config = {});
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Queue a command. Setting *cancel makes a long run stop at the next
    // frame boundary with Cancelled. Called from the emulation thread (a
    // host command calling another command), it runs inline instead.
    std::future<Result> submit(const std::string& name, const Json& args,
        CancelFlag cancel = nullptr);
    // submit() and wait.
    Result call(const std::string& name, const Json& args = Json::object());

    Registry& registry();
    std::string mode() const;

    // Event subscribers (API SSE, MCP list-changed, the host). Callbacks run
    // on the emulation thread and must not block.
    int subscribe(std::function<void(const Event&)> listener);
    void unsubscribe(int id);

    // Fail queued and future calls with ShuttingDown and stop the thread.
    // The destructor calls it.
    void shutdown();

    // Emulation-thread-only access for host commands.
    Machine& machine();
    bool onEmulationThread() const;

    struct Impl;
    Impl& impl() { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace cutenes::api
