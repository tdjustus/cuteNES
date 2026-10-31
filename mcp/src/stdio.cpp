#include <cutenes/mcp/stdio.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <istream>
#include <map>
#include <mutex>
#include <ostream>
#include <thread>

namespace cutenes::mcp {

namespace {

// Lines from the client, read on their own thread so the server can watch
// `stop` and hear notifications/cancelled while calls run.
struct Input {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> lines;
    bool eof = false;
};

// Responses and notifications share stdout, one whole line at a time.
class Writer {
public:
    explicit Writer(std::ostream& out) : m_out(out) {}

    void write(const Json& message)
    {
        std::lock_guard lock(m_mutex);
        if (m_closed) return;
        m_out << serialize(message) << '\n';
        m_out.flush();
    }

    // Late notifications from the session are dropped once serving ends.
    void close()
    {
        std::lock_guard lock(m_mutex);
        m_closed = true;
    }

private:
    std::mutex m_mutex;
    std::ostream& m_out;
    bool m_closed = false;
};

bool blank(const std::string& line)
{
    return line.find_first_not_of(" \t\r") == std::string::npos;
}

} // namespace

bool serveStdio(McpServer& server, std::istream& in, std::ostream& out,
    const std::atomic<bool>& stop)
{
    auto input = std::make_shared<Input>();
    std::thread reader([input, &in] {
        std::string line;
        while (std::getline(in, line)) {
            std::lock_guard lock(input->mutex);
            input->lines.push_back(std::move(line));
            input->cv.notify_one();
        }
        std::lock_guard lock(input->mutex);
        input->eof = true;
        input->cv.notify_one();
    });
    auto writer = std::make_shared<Writer>(out);
    const Send send = [writer](const Json& m) { writer->write(m); };

    // In-flight tool calls by id, for notifications/cancelled.
    std::mutex flightMutex;
    std::map<std::string, api::CancelFlag> inFlight;
    std::map<std::string, std::shared_ptr<Subscription>> subscriptions;

    // The session runs calls in arrival order, so they finish in that order
    // too: one thread waits for each in turn and writes its response.
    struct Pending {
        std::string key;
        Reply reply;
    };
    std::mutex pendingMutex;
    std::condition_variable pendingCv;
    std::deque<Pending> pending;
    bool draining = false;
    std::thread responder([&] {
        for (;;) {
            Pending p;
            {
                std::unique_lock lock(pendingMutex);
                pendingCv.wait(lock, [&] { return draining || !pending.empty(); });
                if (pending.empty()) return;
                p = std::move(pending.front());
                pending.pop_front();
            }
            const auto message = p.reply.wait();
            {
                std::lock_guard lock(flightMutex);
                inFlight.erase(p.key);
            }
            if (message) writer->write(*message);
        }
    });

    Connection connection;
    connection.canNotify = true;
    bool endOfInput = false;
    while (!stop.load()) {
        std::string line;
        {
            std::unique_lock lock(input->mutex);
            input->cv.wait_for(lock, std::chrono::milliseconds(50),
                [&] { return input->eof || !input->lines.empty(); });
            if (input->lines.empty()) {
                if (input->eof) {
                    endOfInput = true;
                    break;
                }
                continue;
            }
            line = std::move(input->lines.front());
            input->lines.pop_front();
        }
        if (blank(line)) continue;
        if (line.back() == '\r') line.pop_back(); // a CRLF client

        const Json message = Json::parse(line, nullptr, false);
        if (message.is_discarded()) {
            writer->write(errorResponse(Json(), kParseError, "Parse error"));
            continue;
        }
        // stdio has no per-request stream to close, so cancellation is a
        // notification (MCP cancellation). Unknown ids are ignored.
        if (message.is_object() && !message.contains("id")
            && message.value("method", Json()) == "notifications/cancelled") {
            const Json params = message.value("params", Json::object());
            const std::string key = params.is_object() ? serialize(params.value("requestId", Json())) : "";
            std::lock_guard lock(flightMutex);
            if (const auto it = inFlight.find(key); it != inFlight.end()) {
                it->second->store(true);
            }
            if (const auto it = subscriptions.find(key); it != subscriptions.end()) {
                it->second->close();
                subscriptions.erase(it);
            }
            continue;
        }

        auto cancel = std::make_shared<std::atomic<bool>>(false);
        Reply reply = server.handle(message, connection, send, cancel);
        const std::string key = message.is_object() ? serialize(message.value("id", Json())) : "";
        switch (reply.kind) {
        case Reply::Kind::Now:
            writer->write(reply.response);
            break;
        case Reply::Kind::Later: {
            {
                std::lock_guard lock(flightMutex);
                inFlight[key] = cancel;
            }
            std::lock_guard lock(pendingMutex);
            pending.push_back({key, std::move(reply)});
            pendingCv.notify_one();
            break;
        }
        case Reply::Kind::Subscription: {
            std::lock_guard lock(flightMutex);
            subscriptions[key] = reply.subscription;
            break;
        }
        case Reply::Kind::None:
            break;
        }
    }

    // The client is gone, or the server is stopping: what runs is cancelled
    // at its next frame boundary (FR-013) and nothing more is written.
    {
        std::lock_guard lock(flightMutex);
        for (auto& [key, cancel] : inFlight) cancel->store(true);
        for (auto& [key, sub] : subscriptions) sub->close();
        subscriptions.clear();
    }
    {
        std::lock_guard lock(pendingMutex);
        draining = true; // every pending call is in inFlight, so cancelled
        pendingCv.notify_one();
    }
    responder.join();
    connection.legacyListChanged.reset();
    writer->close();
    if (endOfInput) {
        reader.join();
    } else {
        // Blocked reading a stream nobody will close (std::cin under a
        // signal); it owns its state and ends with the process.
        reader.detach();
    }
    return endOfInput;
}

} // namespace cutenes::mcp
