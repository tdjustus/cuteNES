#include "host/gui_host.hpp"

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace cutenes::app {

namespace s = api::schema;
using api::CallContext;
using api::Json;
using api::Result;

namespace {

// The largest blob_chunk (002 command catalog).
constexpr qsizetype kChunk = 65536;

void add(api::Registry& registry, const char* name, std::string description, Json input,
    Json output, bool needsRom, std::function<Result(CallContext&, const Json&)> handler)
{
    api::Command c;
    c.name = name;
    c.description = std::move(description);
    c.inputSchema = std::move(input);
    c.outputSchema = std::move(output);
    c.mutating = true;
    c.needsRom = needsRom;
    c.handler = std::move(handler);
    registry.add(std::move(c));
}

// Calls another command inline (handlers run on the emulation thread).
Result call(CallContext& ctx, const char* name, const Json& args = Json::object())
{
    return ctx.session.call(name, args);
}

Json runStateOutput()
{
    return s::object({{"runState", s::stringEnum({"NeedsRom", "Paused", "Running"})}}, {"runState"});
}

Result runStateResult(const Result& r)
{
    if (r.isError()) return r;
    return Result::ok({{"runState", r.value["runState"]}});
}

// Drops whatever a core command returned beyond success.
Result plain(const Result& r)
{
    return r.isError() ? r : Result::ok();
}

bool isRunning(CallContext& ctx)
{
    const Result r = call(ctx, "core.run_state");
    return !r.isError() && r.value.value("runState", "") == "Running";
}

// Uploads `bytes` as a blob, the way any API client sends a ROM that lives
// outside the library.
Result upload(CallContext& ctx, const QByteArray& bytes, std::string& blobId)
{
    Result begin = call(ctx, "session.blob_begin", {{"size", bytes.size()}});
    if (begin.isError()) return begin;
    blobId = begin.value["blobId"].get<std::string>();
    for (qsizetype offset = 0; offset < bytes.size(); offset += kChunk) {
        const QByteArray chunk = bytes.mid(offset, kChunk).toBase64();
        Result r = call(ctx, "session.blob_chunk", {{"blobId", blobId}, {"offset", offset},
            {"bytes", std::string(chunk.constData(), static_cast<std::size_t>(chunk.size()))}});
        if (r.isError()) return r;
    }
    return call(ctx, "session.blob_commit", {{"blobId", blobId}});
}

Result openRom(CallContext& ctx, const std::string& path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return Result::fail("InvalidArgument", "cannot read " + path + ": "
            + file.errorString().toStdString(), {{"path", "/path"}});
    }
    std::string blobId;
    Result r = upload(ctx, file.readAll(), blobId);
    if (r.isError()) return r;
    // core.load_rom checks the ROM before ejecting anything, so a rejected
    // file leaves the running game alone (spec US1-4).
    Result loaded = call(ctx, "core.load_rom", {{"blobId", blobId}});
    call(ctx, "session.blob_delete", {{"blobId", blobId}});
    if (loaded.isError()) return loaded;
    Result resumed = call(ctx, "core.resume");
    if (resumed.isError()) return resumed;
    return Result::ok({{"cartridge", loaded.value["cartridge"]}, {"runState", "Running"}});
}

} // namespace

GuiHost::GuiHost(Hooks hooks) : m_hooks(std::move(hooks)) {}

void GuiHost::onSessionEvent(const api::Event& event)
{
    if (m_hooks.event) m_hooks.event(event);
}

void GuiHost::registerCommands(api::Registry& registry)
{
    add(registry, "gui.open_rom",
        "Open a .nes file from any folder and play it; without path, show the file dialog",
        s::object({{"path", s::string("a file path (not limited to the library)")}}),
        s::object({{"dialog", s::boolean("the file dialog was shown instead")},
            {"cartridge", Json{{"type", "object"}, {"description", "as core.load_rom"}}},
            {"runState", s::stringEnum({"Running"})}}),
        false,
        [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                if (m_hooks.showOpenRomDialog) m_hooks.showOpenRomDialog();
                return Result::ok({{"dialog", true}});
            }
            const std::string path = args["path"].get<std::string>();
            Result r = openRom(ctx, path);
            if (!r.isError() && m_hooks.gameLoaded) m_hooks.gameLoaded(path);
            return r;
        });

    add(registry, "gui.close_game", "Close the game", s::object({}), s::object({}), true,
        [this](CallContext& ctx, const Json&) {
            Result r = plain(call(ctx, "core.eject"));
            if (!r.isError() && m_hooks.gameClosed) m_hooks.gameClosed();
            return r;
        });

    add(registry, "gui.toggle_pause", "Pause or resume the game", s::object({}), runStateOutput(),
        true, [](CallContext& ctx, const Json&) {
            return runStateResult(call(ctx, isRunning(ctx) ? "core.pause" : "core.resume"));
        });

    add(registry, "gui.reset", "Press the console's reset button", s::object({}), s::object({}),
        true, [](CallContext& ctx, const Json&) { return plain(call(ctx, "core.reset")); });

    add(registry, "gui.power_cycle", "Turn the console off and on again", s::object({}),
        s::object({}), true,
        [](CallContext& ctx, const Json&) {
            const bool running = isRunning(ctx);
            Result r = call(ctx, "core.power_on");
            // Power-on pauses the machine; a game being played keeps playing.
            if (!r.isError() && running) r = call(ctx, "core.resume");
            return plain(r);
        });

    add(registry, "gui.exit", "Quit cuteNES", s::object({}), s::object({}), false,
        [this](CallContext&, const Json&) {
            if (m_hooks.exit) m_hooks.exit();
            return Result::ok();
        });

    add(registry, "gui.show_about", "Show the About window", s::object({}), s::object({}), false,
        [this](CallContext&, const Json&) {
            if (m_hooks.showAbout) m_hooks.showAbout();
            return Result::ok();
        });
}

} // namespace cutenes::app
