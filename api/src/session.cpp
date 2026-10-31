#include "session_impl.hpp"

#include <cutenes/api/schema.hpp>

#include <set>

namespace cutenes::api {

namespace {

// Commands whose success is a state_changed event (transports.md "Events").
const std::set<std::string>& stateChangingCommands()
{
    static const std::set<std::string> s = {"core.load_rom", "core.eject",
        "core.power_on", "core.reset", "core.load_state", "core.write_memory",
        "core.write_cpu_register", "core.set_cpu", "core.write_vram",
        "core.write_oam", "core.write_palette", "core.load_sram",
        "session.load_slot", "core.set_mmc3_revision"};
    return s;
}

std::future<Result> ready(Result r)
{
    std::promise<Result> p;
    p.set_value(std::move(r));
    return p.get_future();
}

} // namespace

Session::Impl::Impl(SessionConfig c)
    : config(std::move(c)),
      host(config.host != nullptr ? config.host : &headlessHost),
      machine(config.machine),
      audio(config.machine.audioSampleRate)
{
}

std::string Session::Impl::newBlobId()
{
    return "blob-" + std::to_string(nextBlob++);
}

std::size_t Session::Impl::blobBytes() const
{
    std::size_t total = 0;
    for (const auto& [id, b] : blobs) {
        total += b.committed ? b.data.size() : b.expectedSize;
    }
    return total;
}

void Session::Impl::evictFor(std::size_t incoming)
{
    while (blobBytes() + incoming > kMaxBlobTotal) {
        auto oldest = blobs.end();
        for (auto it = blobs.begin(); it != blobs.end(); ++it) {
            if (it->second.committed
                && (oldest == blobs.end() || it->second.order < oldest->second.order)) {
                oldest = it;
            }
        }
        if (oldest == blobs.end()) {
            return;
        }
        blobs.erase(oldest);
    }
}

std::string Session::Impl::addBlob(std::vector<std::uint8_t> data)
{
    evictFor(data.size());
    const std::string id = newBlobId();
    Blob b;
    b.expectedSize = data.size();
    b.data = std::move(data);
    b.committed = true;
    b.order = nextBlob;
    blobs.emplace(id, std::move(b));
    return id;
}

void Session::Impl::expireUploads()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = blobs.begin(); it != blobs.end();) {
        if (!it->second.committed && it->second.deadline < now) {
            it = blobs.erase(it);
        } else {
            ++it;
        }
    }
}

void Session::Impl::loop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock lock(queueMutex);
            queueCv.wait(lock, [&] { return stopping || !queue.empty(); });
            if (queue.empty()) {
                return; // stopping, and every queued call has been failed
            }
            job = std::move(queue.front());
            queue.pop_front();
        }
        job.done.set_value(job.run());
    }
}

std::future<Result> Session::Impl::post(std::function<Result()> run)
{
    std::lock_guard lock(queueMutex);
    if (stopping) {
        return ready(Result::fail("ShuttingDown", "the session is shutting down"));
    }
    Job job{std::move(run), {}};
    auto future = job.done.get_future();
    queue.push_back(std::move(job));
    queueCv.notify_one();
    return future;
}

void Session::Impl::emit(const std::string& type, Json data)
{
    Event e{type, machine.frameCounter(), machine.masterCycle(), std::move(data)};
    std::vector<std::function<void(const Event&)>> targets;
    {
        std::lock_guard lock(listenerMutex);
        for (const auto& [id, fn] : listeners) {
            targets.push_back(fn);
        }
    }
    host->onSessionEvent(e);
    for (const auto& fn : targets) {
        fn(e);
    }
}

void Session::Impl::emitFrame()
{
    // At most 10 per second (transports.md "Events"); wall-clock time only
    // thins the events and never touches emulation.
    const auto now = std::chrono::steady_clock::now();
    if (now - lastFrameEvent < std::chrono::milliseconds(100)) {
        return;
    }
    lastFrameEvent = now;
    emit("frame", {{"frameHash", media::hashHex(frame.hash())}});
}

Result Session::Impl::dispatch(Session& session, const std::string& name,
    const Json& rawArgs, const CancelFlag& cancel, const ProgressFn& progress)
{
    if (cancel && cancel->load()) {
        // Cancelled while queued (a client gone, or a listener closing).
        return Result::fail("Cancelled", "the call was cancelled before it ran");
    }
    expireUploads();
    const auto command = registry.find(name);
    if (!command) {
        return Result::fail("UnknownCommand", "no command named " + name,
            {{"command", name}});
    }
    const Json args = rawArgs.is_null() ? Json::object() : rawArgs;
    if (auto failure = validate(command->inputSchema, args)) {
        return Result::fail("InvalidArguments",
            (failure->path.empty() ? std::string("arguments") : failure->path)
                + " " + failure->message,
            {{"path", failure->path}});
    }
    if (command->needsRom && machine.cartridgeInfo().prgSize == 0) {
        return Result::fail("NoCartridge", name + " needs a loaded cartridge");
    }

    const std::uint64_t frameBefore = machine.frameCounter();
    const RunState runBefore = machine.runState();
    const bool pausedBefore = pausedByAutomation;
    CallContext ctx{session,
        [this, cancel] { return stopFlag.load() || (cancel && cancel->load()); },
        progress};
    Result result;
    try {
        result = command->handler(ctx, args);
    } catch (const std::exception& e) {
        result = Result::fail("InternalError", e.what());
    }
    if (result.isError()) {
        return result;
    }
    if (!result.value.is_object()) {
        result.value = Json::object();
    }
    result.value["frame"] = machine.frameCounter();
    result.value["masterCycle"] = machine.masterCycle();
    if (config.validateOutputs) {
        if (auto failure = validate(command->outputSchema, result.value)) {
            return Result::fail("InternalError",
                name + " result " + failure->path + " " + failure->message,
                {{"path", failure->path}});
        }
    }
    if (stateChangingCommands().count(name)) {
        emit("state_changed", {{"command", name}});
    }
    if (machine.frameCounter() != frameBefore) {
        emitFrame();
    }
    if (machine.runState() != runBefore || pausedByAutomation != pausedBefore) {
        emit("run_state_changed", {{"runState", runStateName(machine.runState())},
                                      {"pausedByAutomation", pausedByAutomation}});
    }
    return result;
}

Session::Session(SessionConfig config)
    : m_impl(std::make_unique<Impl>(std::move(config)))
{
    auto& impl = *m_impl;
    impl.machine.setVideoSink(&impl.frame);
    impl.machine.setAudioSink(&impl.audio);
    registerCoreCommands(impl.registry);
    registerSessionCommands(impl.registry);
    impl.thread = std::thread([this] {
        m_impl->threadId = std::this_thread::get_id();
        m_impl->loop();
    });
    // The host registers on the emulation thread (host.hpp); catalog
    // changes from then on are events.
    impl.post([this] {
             auto& i = *m_impl;
             i.host->registerCommands(i.registry);
             i.registry.setOnChange([&i] { i.emit("catalog_changed"); });
             return Result::ok();
         })
        .wait();
}

Session::~Session() { shutdown(); }

std::future<Result> Session::submit(const std::string& name, const Json& args,
    CancelFlag cancel, ProgressFn progress)
{
    if (onEmulationThread()) {
        return ready(m_impl->dispatch(*this, name, args, cancel, progress));
    }
    return m_impl->post([this, name, args, cancel, progress] {
        return m_impl->dispatch(*this, name, args, cancel, progress);
    });
}

Result Session::call(const std::string& name, const Json& args)
{
    if (onEmulationThread()) {
        return m_impl->dispatch(*this, name, args, nullptr);
    }
    return submit(name, args).get();
}

Registry& Session::registry() { return m_impl->registry; }

std::string Session::mode() const { return m_impl->host->mode(); }

int Session::subscribe(std::function<void(const Event&)> listener)
{
    std::lock_guard lock(m_impl->listenerMutex);
    const int id = m_impl->nextListener++;
    m_impl->listeners.emplace(id, std::move(listener));
    return id;
}

void Session::unsubscribe(int id)
{
    std::lock_guard lock(m_impl->listenerMutex);
    m_impl->listeners.erase(id);
}

void Session::shutdown()
{
    auto& impl = *m_impl;
    std::deque<Impl::Job> failed;
    {
        std::lock_guard lock(impl.queueMutex);
        if (impl.stopping && !impl.thread.joinable()) {
            return;
        }
        impl.stopping = true;
        impl.stopFlag = true;
        failed.swap(impl.queue);
        impl.queueCv.notify_all();
    }
    for (auto& job : failed) {
        job.done.set_value(Result::fail("ShuttingDown", "the session is shutting down"));
    }
    if (impl.thread.joinable() && !onEmulationThread()) {
        impl.thread.join();
    }
}

Machine& Session::machine() { return m_impl->machine; }

bool Session::onEmulationThread() const
{
    return std::this_thread::get_id() == m_impl->threadId;
}

} // namespace cutenes::api
