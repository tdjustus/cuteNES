#include "host/gui_host.hpp"

#include "settings/app_settings.hpp"

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace cutenes::app {

namespace s = api::schema;
using api::CallContext;
using api::Json;
using api::Result;

namespace {

// The largest blob_chunk (002 command catalog).
constexpr qsizetype kChunk = 65536;

const std::vector<std::string> kSettingsPages = {
    "video", "audio", "input", "hotkeys", "emulation", "library", "servers"};
// The settings groups and keys implemented so far (data-model "Settings
// keys"); later phases add theirs.
const std::vector<std::string> kSettingKeys = {"library.root"};

void add(api::Registry& registry, const char* name, std::string description, Json input,
    Json output, bool mutating, bool needsRom, std::function<Result(CallContext&, const Json&)> handler)
{
    api::Command c;
    c.name = name;
    c.description = std::move(description);
    c.inputSchema = std::move(input);
    c.outputSchema = std::move(output);
    c.mutating = mutating;
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

Result invalid(const std::string& path, const std::string& message)
{
    return Result::fail("InvalidArgument", message, {{"path", path}});
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

std::string str(const QString& s)
{
    return s.toStdString();
}

Json entryJson(const LibraryEntry& e)
{
    return {{"path", str(e.path)}, {"relativePath", str(e.relativePath)}, {"identity", str(e.identity)},
        {"name", str(e.name)}, {"mapper", e.mapper}, {"submapper", e.submapper}, {"region", str(e.region)},
        {"battery", e.battery}, {"supported", e.supported}, {"reason", str(e.reason)},
        {"lastPlayed", e.lastPlayed.isValid() ? str(e.lastPlayed.toUTC().toString(Qt::ISODate)) : ""},
        {"hasSave", e.hasSave}, {"stateCount", e.stateCount}, {"recent", e.recent}};
}

// The real path, for telling whether two folders overlap.
QString canonical(const QString& path)
{
    const QString real = QFileInfo(path).canonicalFilePath();
    return real.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath()) : real;
}

bool within(const QString& path, const QString& folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

Json romFoldersValue(const AppSettings& settings)
{
    Json list = Json::array();
    for (const QString& folder : settings.romFolders()) list.push_back(str(folder));
    return list;
}

Json entrySchema()
{
    return s::object({{"path", s::string("absolute")}, {"relativePath", s::string("under roms/, or empty")},
        {"identity", s::string("SHA-1 of the ROM after its header")}, {"name", s::string()},
        {"mapper", s::integer(-1, 4095)}, {"submapper", s::integer(0, 15)},
        {"region", s::string()}, {"battery", s::boolean()}, {"supported", s::boolean()},
        {"reason", s::string("why the core rejects it")}, {"lastPlayed", s::string("ISO 8601, or empty")},
        {"hasSave", s::boolean()}, {"stateCount", s::integer(0, 10)}, {"recent", s::boolean()}});
}

} // namespace

GuiHost::GuiHost(Window& window, AppSettings& settings, BatterySaver& battery)
    : m_window(window), m_settings(settings), m_battery(battery)
{
}

void GuiHost::onSessionEvent(const api::Event& event)
{
    // Battery RAM is checked as frames run (frame events come at most 10
    // times a second, on the emulation thread, which owns the machine).
    if (api::Session* session = m_session.load(); event.type == "frame" && session) {
        m_battery.poll(session->machine());
    }
    m_window.sessionEvent(event);
}

Result GuiHost::openRom(CallContext& ctx, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return invalid("/path", "cannot read " + str(path) + ": " + str(file.errorString()));
    }
    const QByteArray bytes = file.readAll();

    // Check the ROM before touching the running game, so a rejected file
    // leaves it, and its battery save, alone (spec US1-4).
    {
        Machine scratch;
        const LoadResult check = scratch.loadCartridge(
            reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
        if (check.error != LoadError::Ok) {
            std::string blobId;
            Result r = upload(ctx, bytes, blobId);
            if (r.isError()) return r;
            Result rejected = call(ctx, "core.load_rom", {{"blobId", blobId}}); // the core's own error
            call(ctx, "session.blob_delete", {{"blobId", blobId}});
            return rejected;
        }
    }
    if (m_gameOpen) closeGame(ctx);

    std::string blobId;
    Result r = upload(ctx, bytes, blobId);
    if (r.isError()) return r;
    Result loaded = call(ctx, "core.load_rom", {{"blobId", blobId}});
    call(ctx, "session.blob_delete", {{"blobId", blobId}});
    if (loaded.isError()) return loaded;

    // The battery save goes in before the first frame runs.
    const GameIdentity id = identifyRom(bytes).value_or(GameIdentity{});
    BatterySaver::Opened battery;
    const Library library = m_window.library();
    if (!id.sha1.isEmpty() && library.isUsable()) {
        battery = m_battery.open(library, id, QFileInfo(path).completeBaseName(), ctx.session.machine());
    }
    m_gameOpen = true;
    m_settings.addRecent(path);
    if (!id.sha1.isEmpty()) m_settings.setLastPlayed(id.sha1, QDateTime::currentDateTimeUtc());
    m_window.gameOpened(QFileInfo(path).absoluteFilePath(), id, battery);

    Result resumed = call(ctx, "core.resume");
    if (resumed.isError()) return resumed;
    Json v = {{"cartridge", loaded.value["cartridge"]}, {"runState", "Running"},
        {"identity", str(id.sha1)}, {"batterySave", str(battery.path)}};
    if (!battery.warning.isEmpty()) v["warning"] = str(battery.warning);
    return Result::ok(v);
}

Result GuiHost::closeGame(CallContext& ctx)
{
    // FR-012: the battery save is written before the cartridge goes.
    m_battery.close(ctx.session.machine());
    m_gameOpen = false;
    Result r = plain(call(ctx, "core.eject"));
    if (!r.isError()) m_window.gameClosed();
    return r;
}

Result GuiHost::setLibraryRoot(CallContext& ctx, const QString& root)
{
    if (root.isEmpty() || QDir::isRelativePath(root)) return invalid("/value", "the library location must be an absolute path");
    const Library library(root);
    QString error;
    if (!library.ensureLayout(&error)) return invalid("/value", str(error));
    m_settings.setLibraryRoot(library.root());
    ctx.session.setLibraryRoot(str(library.folder(Library::Folder::Roms)));
    m_window.libraryChanged(library);
    return Result::ok({{"key", "library.root"}, {"value", str(library.root())}});
}

Result GuiHost::addRomFolder(const QString& path)
{
    if (path.isEmpty() || QDir::isRelativePath(path)) return invalid("/path", "the folder must be an absolute path");
    if (!QFileInfo(path).isDir()) return invalid("/path", "no such folder: " + str(path));
    const QString added = canonical(path);
    QStringList folders = m_settings.romFolders();
    QStringList taken = folders;
    taken.prepend(m_window.library().folder(Library::Folder::Roms));
    for (const QString& folder : taken) {
        const QString existing = canonical(folder);
        if (within(added, existing) || within(existing, added)) {
            return invalid("/path", "the library already scans " + str(folder) + ", which overlaps this folder");
        }
    }
    folders.append(QDir::cleanPath(path));
    m_settings.setRomFolders(folders);
    m_window.romFoldersChanged();
    return Result::ok({{"romFolders", romFoldersValue(m_settings)}});
}

Result GuiHost::removeRomFolder(const QString& path)
{
    const QString removed = canonical(path);
    if (removed == canonical(m_window.library().folder(Library::Folder::Roms))) {
        return invalid("/path", "the library's own roms folder is always scanned and cannot be removed");
    }
    QStringList folders = m_settings.romFolders();
    const auto it = std::find_if(folders.begin(), folders.end(),
        [&](const QString& folder) { return QDir::cleanPath(folder) == QDir::cleanPath(path) || canonical(folder) == removed; });
    if (it == folders.end()) return invalid("/path", "not an added ROM folder: " + str(path));
    folders.erase(it);
    m_settings.setRomFolders(folders);
    m_window.romFoldersChanged();
    return Result::ok({{"romFolders", romFoldersValue(m_settings)}});
}

void GuiHost::registerCommands(api::Registry& registry)
{
    add(registry, "gui.open_rom",
        "Open a .nes file from any folder and play it; without path, show the file dialog",
        s::object({{"path", s::string("a file path (not limited to the library)")}}),
        s::object({{"dialog", s::boolean("the file dialog was shown instead")},
            {"cartridge", Json{{"type", "object"}, {"description", "as core.load_rom"}}},
            {"runState", s::stringEnum({"Running"})}, {"identity", s::string()},
            {"batterySave", s::string("the battery save file, or empty")},
            {"warning", s::string("a battery save set aside, or another instance holds the game")}}),
        true, false, [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                m_window.showOpenRomDialog();
                return Result::ok({{"dialog", true}});
            }
            return openRom(ctx, QString::fromStdString(args["path"].get<std::string>()));
        });

    add(registry, "gui.close_game", "Close the game, writing its battery save", s::object({}),
        s::object({}), true, true, [this](CallContext& ctx, const Json&) { return closeGame(ctx); });

    add(registry, "gui.toggle_pause", "Pause or resume the game", s::object({}), runStateOutput(),
        true, true, [](CallContext& ctx, const Json&) {
            return runStateResult(call(ctx, isRunning(ctx) ? "core.pause" : "core.resume"));
        });

    add(registry, "gui.reset", "Press the console's reset button", s::object({}), s::object({}),
        true, true, [](CallContext& ctx, const Json&) { return plain(call(ctx, "core.reset")); });

    add(registry, "gui.power_cycle", "Turn the console off and on again", s::object({}),
        s::object({}), true, true, [](CallContext& ctx, const Json&) {
            const bool running = isRunning(ctx);
            Result r = call(ctx, "core.power_on");
            // Power-on pauses the machine; a game being played keeps playing.
            if (!r.isError() && running) r = call(ctx, "core.resume");
            return plain(r);
        });

    add(registry, "gui.exit", "Quit cuteNES (the battery save is written first)", s::object({}),
        s::object({}), true, false, [this](CallContext&, const Json&) {
            m_window.exit();
            return Result::ok();
        });

    add(registry, "gui.show_about", "Show the About window", s::object({}), s::object({}), false,
        false, [this](CallContext&, const Json&) {
            m_window.showAbout();
            return Result::ok();
        });

    // --- Library (G2) -----------------------------------------------------------

    add(registry, "gui.show_library", "Show the library browser (pauses a running game)",
        s::object({}), s::object({}), false, false, [this](CallContext& ctx, const Json&) {
            const bool paused = isRunning(ctx) && !call(ctx, "core.pause").isError();
            m_window.showLibrary({}, paused);
            return Result::ok();
        });

    add(registry, "gui.library_list", "The library browser's games (data-model LibraryEntry)",
        s::object({}), s::object({{"root", s::string()}, {"entries", s::array(entrySchema(), 100000)}},
            {"root", "entries"}),
        false, false, [this](CallContext&, const Json&) {
            Json entries = Json::array();
            for (const auto& e : m_window.libraryEntries()) entries.push_back(entryJson(e));
            return Result::ok({{"root", str(m_window.library().root())}, {"entries", entries}});
        });

    add(registry, "gui.library_select", "Select a game in the library browser by path or identity",
        s::object({{"path", s::string()}, {"identity", s::string()}}), s::object({{"path", s::string()}}, {"path"}),
        false, false, [this](CallContext& ctx, const Json& args) {
            if (args.contains("path") == args.contains("identity")) {
                return invalid("", "give exactly one of path and identity");
            }
            const QString wanted = QString::fromStdString(
                args.contains("path") ? args["path"].get<std::string>() : args["identity"].get<std::string>());
            const QString absolute = args.contains("path") ? QFileInfo(wanted).absoluteFilePath() : wanted;
            for (const auto& e : m_window.libraryEntries()) {
                if (e.path == absolute || e.identity == wanted) {
                    const bool paused = isRunning(ctx) && !call(ctx, "core.pause").isError();
                    m_window.showLibrary(e.path, paused);
                    return Result::ok({{"path", str(e.path)}});
                }
            }
            return invalid(args.contains("path") ? "/path" : "/identity", "no such game in the library");
        });

    add(registry, "gui.open_library_folder", "Show the library folder in the file manager",
        s::object({}), s::object({}), false, false, [this](CallContext&, const Json&) {
            m_window.openLibraryFolder();
            return Result::ok();
        });

    add(registry, "gui.list_recent", "Recently opened games, newest first", s::object({}),
        s::object({{"recent", s::array(s::object({{"path", s::string()}, {"identity", s::string()},
                                                   {"exists", s::boolean()}}),
                       AppSettings::kMaxRecent)}},
            {"recent"}),
        false, false, [this](CallContext&, const Json&) {
            Json list = Json::array();
            const auto entries = m_window.libraryEntries();
            for (const QString& path : m_settings.recent()) {
                QString identity;
                for (const auto& e : entries) {
                    if (e.path == path) identity = e.identity;
                }
                list.push_back({{"path", str(path)}, {"identity", str(identity)}, {"exists", QFileInfo::exists(path)}});
            }
            return Result::ok({{"recent", list}});
        });

    add(registry, "gui.open_recent", "Open a recent game by its place in File > Open Recent (0 = newest)",
        s::object({{"index", s::integer(0, AppSettings::kMaxRecent - 1)}}, {"index"}),
        s::object({{"cartridge", Json{{"type", "object"}}}, {"runState", s::stringEnum({"Running"})},
            {"identity", s::string()}, {"batterySave", s::string()}, {"warning", s::string()}}),
        true, false, [this](CallContext& ctx, const Json& args) {
            const QStringList recent = m_settings.recent();
            const int index = args["index"].get<int>();
            if (index >= recent.size()) return invalid("/index", "there are only " + std::to_string(recent.size()) + " recent games");
            return openRom(ctx, recent[index]);
        });

    add(registry, "gui.clear_recent", "Clear File > Open Recent", s::object({}), s::object({}), true,
        false, [this](CallContext&, const Json&) {
            m_settings.clearRecent();
            m_window.recentChanged();
            return Result::ok();
        });

    const Json romFoldersOutput = s::object({{"romFolders", s::array(s::string(), 1000)}}, {"romFolders"});
    add(registry, "gui.add_rom_folder",
        "Scan another folder of ROMs in place, beside the library's roms folder (nothing is written there)",
        s::object({{"path", s::string("an absolute folder path")}}, {"path"}), romFoldersOutput, true, false,
        [this](CallContext&, const Json& args) { return addRomFolder(QString::fromStdString(args["path"].get<std::string>())); });

    add(registry, "gui.remove_rom_folder",
        "Stop scanning an added ROM folder (its files are kept; the library's roms folder cannot be removed)",
        s::object({{"path", s::string()}}, {"path"}), romFoldersOutput, true, false,
        [this](CallContext&, const Json& args) {
            return removeRomFolder(QString::fromStdString(args["path"].get<std::string>()));
        });

    // --- Settings ---------------------------------------------------------------

    add(registry, "gui.show_settings", "Show Settings, at a page or the last one shown",
        s::object({{"page", s::stringEnum(kSettingsPages)}}), s::object({{"page", s::string()}}, {"page"}),
        false, false, [this](CallContext&, const Json& args) {
            const QString page = args.contains("page") ? QString::fromStdString(args["page"].get<std::string>())
                                                       : m_settings.lastSettingsPage();
            m_window.showSettings(page);
            return Result::ok({{"page", str(page)}});
        });

    add(registry, "gui.get_settings", "Every setting's value and schema, or one group's",
        s::object({{"group", s::stringEnum({"library"})}}),
        s::object({{"values", Json{{"type", "object"}}}, {"schema", Json{{"type", "object"}}}}, {"values", "schema"}),
        false, false, [this](CallContext&, const Json&) {
            const Library library = m_window.library();
            const Json values = {{"library", {{"root", str(library.root())},
                                     {"roms", str(library.folder(Library::Folder::Roms))},
                                     {"romFolders", romFoldersValue(m_settings)}}}};
            const Json schema = {{"library.root", s::string("absolute path of the library folder")},
                {"library.roms", s::string("the library's own roms folder (always scanned)")},
                {"library.romFolders", s::array(s::string("added with gui.add_rom_folder"), 1000)}};
            return Result::ok({{"values", values}, {"schema", schema}});
        });

    add(registry, "gui.set_setting", "Validate and apply a setting (gui.get_settings lists them)",
        s::object({{"key", s::stringEnum(kSettingKeys)}, {"value", Json::object()}}, {"key", "value"}),
        s::object({{"key", s::string()}, {"value", Json::object()}}, {"key", "value"}), true, false,
        [this](CallContext& ctx, const Json& args) {
            const std::string key = args["key"];
            if (key == "library.root") {
                if (!args["value"].is_string()) return invalid("/value", "must be a string");
                return setLibraryRoot(ctx, QString::fromStdString(args["value"].get<std::string>()));
            }
            return invalid("/key", "unknown setting");
        });
}

} // namespace cutenes::app
