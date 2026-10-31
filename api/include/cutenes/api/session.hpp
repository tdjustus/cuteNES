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
// Long runs report (done, total) between frames, on the emulation thread
// (MCP notifications/progress).
using ProgressFn = std::function<void(double done, double total)>;

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
    // frame boundary with Cancelled; `progress` hears how far a long run
    // has got and must not block. Called from the emulation thread (a host
    // command calling another command), it runs inline instead.
    std::future<Result> submit(const std::string& name, const Json& args,
        CancelFlag cancel = nullptr, ProgressFn progress = {});
    // submit() and wait.
    Result call(const std::string& name, const Json& args = Json::object());

    Registry& registry();
    std::string mode() const;

    // Real-time play, for the GUI (feature 003, research R3). Off by
    // default, so a headless session never runs frames on its own.
    //
    // While it is on and the machine is Running, the emulation thread runs
    // one frame per frame requested, between queued commands. The host
    // requests frames from its clock (a display refresh or a timer). Frames
    // requested while the machine is not Running are dropped, and at most
    // twice the latest request is ever pending, so a stall never becomes a
    // burst. All four calls are safe from any thread.
    void setRealtime(bool on);
    void requestFrames(std::uint32_t frames = 1);
    // The buttons a human holds on `port`, applied at the start of each
    // real-time frame, combined (OR) with the last core.set_controller
    // value. A scripted core.run_frames ignores them (002 FR-019).
    void setLiveInput(Port port, ButtonSet buttons);
    // Extra sinks that receive every frame and every audio chunk the
    // machine produces, after the session's own capture. They run on the
    // emulation thread and must not block; null removes them.
    void setMediaSinks(IVideoSink* video, IAudioSink* audio);

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
