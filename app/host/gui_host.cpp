#include "host/gui_host.hpp"

#include "settings/app_settings.hpp"
#include "settings/key_bindings.hpp"

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>
#include <cutenes/qt/audio_output.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>

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
const std::vector<std::string> kSettingKeys = {"video.scale", "video.fullscreen", "video.integerScaling",
    "video.aspect", "video.overscan", "video.filter", "video.alwaysOnTop", "video.showFps", "video.showOsd",
    "audio.device", "audio.volume", "audio.muted", "audio.latencyMs", "audio.fastForwardAudio",
    "emulation.pauseWhenInactive", "emulation.resumeOnOpen", "emulation.fastForwardSpeed",
    "library.root", "servers.apiEnabled", "servers.apiPort",
    "servers.mcpEnabled", "servers.mcpPort", "input.turboRate", "input.allowOppositeDirections",
    "input.pauseOnGamepadDisconnect"};

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
    return Result::fail("InvalidArguments", message, {{"path", path}});
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

Json pairJson(const KeyBindings::Pair& pair)
{
    return Json::array({str(pair[0]), str(pair[1])});
}

// One Key Bindings entry (contract "Key Bindings page").
Json bindingJson(const KeyBindings::Entry& e)
{
    return {{"id", str(e.definition.id)}, {"label", str(e.definition.label)},
        {"kind", e.definition.control ? "control" : "hotkey"}, {"bindings", pairJson(e.bindings)},
        {"defaults", pairJson(e.definition.defaults)}};
}

Json bindingSchema()
{
    const Json pair = s::array(s::string("Qt's portable key text (Ctrl+S), a gamepad control (Pad:South, Pad:Back+LeftShoulder), or \"\" for none"), 2);
    return s::object({{"id", s::string()}, {"label", s::string()}, {"kind", s::stringEnum({"hotkey", "control"})},
                         {"bindings", pair}, {"defaults", pair}},
        {"id", "label", "kind", "bindings", "defaults"});
}

Json storedBindings(const KeyBindings& bindings, bool controls)
{
    Json out = Json::object();
    const auto stored = bindings.stored(controls);
    for (auto it = stored.begin(); it != stored.end(); ++it) out[str(it.key())] = pairJson(it.value());
    return out;
}

// A KeyBindings change as a command's result.
Result bindingResult(const KeyBindings::Change& change)
{
    using Status = KeyBindings::Change::Status;
    switch (change.status) {
    case Status::UnknownId:
        return invalid("/id", str(change.why));
    case Status::BadIndex:
        return invalid("/index", str(change.why));
    case Status::BadBinding:
        return invalid("/binding", str(change.why));
    case Status::Conflicted: {
        Json conflicts = Json::array();
        for (const auto& c : change.conflicts) conflicts.push_back({{"id", str(c.id)}, {"label", str(c.label)}});
        return Result::fail("InvalidArguments", str(change.why) + " (pass force to take it)",
            {{"path", "/binding"}, {"conflicts", conflicts}});
    }
    case Status::Done:
        break;
    }
    return Result::ok(bindingJson(*change.entry));
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

// A blob's bytes, read the way any API client reads them.
std::optional<QByteArray> download(CallContext& ctx, const std::string& blobId)
{
    QByteArray bytes;
    for (;;) {
        const Result r = call(ctx, "session.blob_read", {{"blobId", blobId}, {"offset", bytes.size()}, {"length", kChunk}});
        if (r.isError()) return std::nullopt;
        bytes += QByteArray::fromBase64(QByteArray::fromStdString(r.value["bytes"].get<std::string>()));
        if (r.value["eof"].get<bool>()) return bytes;
    }
}

// The last frame as a 256x240 image (null before the first frame).
QImage currentFrame(CallContext& ctx)
{
    const Result screen = call(ctx, "core.screen", {{"format", "raw"}});
    if (screen.isError() || !screen.value.contains("blobId")) return {};
    const std::string blobId = screen.value["blobId"].get<std::string>();
    const std::optional<QByteArray> raw = download(ctx, blobId);
    call(ctx, "session.blob_delete", {{"blobId", blobId}});
    if (!raw || raw->size() != 256 * 240 * 4) return {};
    return QImage(reinterpret_cast<const uchar*>(raw->constData()), 256, 240, QImage::Format_RGB32).copy();
}

Json slotSchema()
{
    return s::integer(1, StateSlots::kSlots);
}

Json nameSchema()
{
    return s::string("what the menus call the state; default: when it was saved");
}

QString stateName(const Json& args)
{
    return QString::fromStdString(args.value("name", std::string()));
}

Json slotInfoJson(const StateSlots::Info& info)
{
    Json v = {{"slot", info.slot}, {"empty", info.empty}};
    if (!info.empty) {
        v["name"] = str(info.name);
        v["savedAt"] = str(info.savedAt.toUTC().toString(Qt::ISODate));
        v["size"] = info.size;
        v["state"] = str(info.statePath);
        if (QFileInfo::exists(info.thumbnailPath)) v["thumbnail"] = str(info.thumbnailPath);
    }
    return v;
}

Json slotInfoSchema()
{
    return s::object({{"slot", slotSchema()}, {"empty", s::boolean()}, {"name", s::string()},
        {"savedAt", s::string("ISO 8601, UTC")}, {"size", s::u64()},
        {"state", s::string("the .state file")}, {"thumbnail", s::string("the 256x240 PNG")}},
        {"slot", "empty"});
}

} // namespace

GuiHost::GuiHost(Window& window, AppSettings& settings, BatterySaver& battery)
    : m_window(window), m_settings(settings), m_battery(battery)
{
}

IAudioSink* GuiHost::audioTap(IAudioSink* output)
{
    m_tap.output = output;
    return &m_tap;
}

void GuiHost::attach(api::Session& session)
{
    m_fastForwardSpeed = m_settings.emulation().fastForwardSpeed;
    m_session = &session;
    session.setFrameRunner([this](Machine& machine) { runFrame(machine); });
}

void GuiHost::detach()
{
    if (api::Session* session = m_session.exchange(nullptr)) session->setFrameRunner({});
}

const std::vector<int>& GuiHost::speeds()
{
    static const std::vector<int> list = {25, 50, 75, 100, 150, 200, 300, 400};
    return list;
}

void GuiHost::runFrame(Machine& machine)
{
    if (m_rewinding) {
        // Back one frame: the stored state, run with its own input, shows
        // the frame before the one on screen (R6). With no history left the
        // picture holds.
        std::optional<RewindBuffer::Entry> entry = m_rewind.pop();
        if (!entry) return;
        if (machine.loadState(entry->state.data(), entry->state.size()) != LoadStateError::Ok) return;
        machine.resume(); // a load leaves the core paused; rewinding plays on
        machine.setController(Port::One, entry->input[0]);
        machine.setController(Port::Two, entry->input[1]);
        machine.runFrames(1);
        m_rewound = std::move(entry);
        return;
    }
    if (m_rewound) {
        // Play goes on from the last frame shown, which is history again.
        m_rewind.push(m_rewound->state.data(), m_rewound->state.size(), m_rewound->input[0], m_rewound->input[1]);
        m_rewound.reset();
    }
    const SaveStateBlob state = machine.captureState();
    m_rewind.push(state.data, state.size, machine.controller(Port::One), machine.controller(Port::Two));
    machine.runFrames(1);
}

Result GuiHost::setRewind(CallContext& ctx, bool on)
{
    if (on == m_rewinding) return Result::ok({{"rewinding", on}});
    if (on) {
        if (m_rewind.empty() && !m_rewound) {
            m_window.showMessage(QObject::tr("Nothing to rewind yet"));
            return Result::ok({{"rewinding", false}});
        }
        m_pauseAfterRewind = !isRunning(ctx);
        m_rewinding = true;
        updatePlayback();
        if (m_pauseAfterRewind) {
            Result r = call(ctx, "core.resume");
            if (r.isError()) return r;
        }
    } else {
        m_rewinding = false;
        updatePlayback();
        if (m_pauseAfterRewind) {
            m_pauseAfterRewind = false;
            Result r = call(ctx, "core.pause");
            if (r.isError()) return r;
        }
    }
    return Result::ok({{"rewinding", m_rewinding}});
}

Result GuiHost::setSpeed(int percent)
{
    m_speed = percent;
    updatePlayback();
    return Result::ok({{"speed", percent == 0 ? Json("unlimited") : Json(percent)}});
}

void GuiHost::updatePlayback()
{
    GuiHost::Window::Playback playback;
    const int percent = m_fastForward ? m_fastForwardSpeed : m_speed;
    playback.speed = percent / 100.0;
    playback.fastForward = m_fastForward;
    playback.rewinding = m_rewinding;
    // Sound is sped up (or slowed) with the game up to 2x and muted beyond
    // that, at Unlimited and while rewinding (R4, R6).
    playback.muted = m_rewinding || percent == 0 || percent > 200
        || (m_fastForward && m_settings.audio().fastForwardAudio == QLatin1String("mute"));
    m_window.playbackChanged(playback);
}

void GuiHost::onSessionEvent(const api::Event& event)
{
    // A load, reset, power cycle or new game makes the history a different
    // timeline (data-model "RewindBuffer").
    if (event.type == "state_changed") {
        m_rewind.clear();
        m_rewound.reset();
    }
    // Battery RAM is checked as frames run (frame events come at most 10
    // times a second, on the emulation thread, which owns the machine).
    if (api::Session* session = m_session.load(); event.type == "frame" && session) {
        m_battery.poll(session->machine());
    }
    m_window.sessionEvent(event);
}

QString GuiHost::autoStatePath() const
{
    const Library library = m_window.library();
    if (!m_gameOpen || m_identity.sha1.isEmpty() || !library.isUsable()) return {};
    return QDir(library.gameFile(Library::Folder::States, m_identity, m_gameName, {})).filePath(QStringLiteral("auto.state"));
}

Result GuiHost::stopRecording()
{
    const QString path = m_tap.wav.path();
    const std::uint64_t samples = m_tap.wav.close();
    m_window.recordingChanged({});
    m_window.showMessage(QObject::tr("Audio saved to %1").arg(QFileInfo(path).fileName()));
    return Result::ok({{"recording", false}, {"path", str(path)}, {"seconds", double(samples) / 48000.0}});
}

Result GuiHost::openRom(CallContext& ctx, const QString& path, std::optional<bool> resume)
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
    m_identity = id;
    m_gameName = QFileInfo(path).completeBaseName();
    m_undoState.reset();
    {
        std::lock_guard lock(m_slotsMutex);
        m_slots = id.sha1.isEmpty() || !library.isUsable()
            ? StateSlots()
            : StateSlots(library, id, QFileInfo(path).completeBaseName());
    }
    m_selectedSlot = id.sha1.isEmpty() ? 1 : m_settings.selectedSlot(id.sha1);
    m_settings.addRecent(path);
    if (!id.sha1.isEmpty()) m_settings.setLastPlayed(id.sha1, QDateTime::currentDateTimeUtc());
    m_window.gameOpened(QFileInfo(path).absoluteFilePath(), id, battery);

    // Where the player left off (FR-037): loaded, ignored, or offered.
    const QString autoState = m_settings.emulation().resumeOnOpen ? autoStatePath() : QString();
    const bool hasAuto = !autoState.isEmpty() && QFile::exists(autoState);
    bool resumedState = false;
    if (hasAuto && resume.value_or(false)) {
        QString error;
        const std::optional<QByteArray> state = StateSlots::readFile(autoState, &error);
        resumedState = state && !loadStateBytes(ctx, *state, false).isError();
    }
    Json v = {{"cartridge", loaded.value["cartridge"]}, {"runState", "Running"},
        {"identity", str(id.sha1)}, {"batterySave", str(battery.path)}};
    if (!battery.warning.isEmpty()) v["warning"] = str(battery.warning);
    if (hasAuto) {
        v["autoState"] = str(autoState);
        v["resumed"] = resumedState;
    }
    if (hasAuto && !resume) {
        v["runState"] = "Paused"; // until the player answers
        m_window.offerResume(autoState);
        return Result::ok(v);
    }
    Result resumed = call(ctx, "core.resume");
    if (resumed.isError()) return resumed;
    return Result::ok(v);
}

Result GuiHost::closeGame(CallContext& ctx)
{
    if (m_tap.wav.isOpen()) stopRecording();
    // FR-037: where the player left off, for the next open.
    if (m_settings.emulation().resumeOnOpen) {
        if (const QString path = autoStatePath(); !path.isEmpty() && QDir().mkpath(QFileInfo(path).path())) {
            const SaveStateBlob blob = ctx.session.machine().captureState();
            StateSlots::writeFile(path, QByteArray(reinterpret_cast<const char*>(blob.data), qsizetype(blob.size)));
        }
    }
    // FR-012: the battery save is written before the cartridge goes.
    m_battery.close(ctx.session.machine());
    m_gameOpen = false;
    m_identity = {};
    m_rewind.clear();
    m_rewound.reset();
    m_rewinding = false;
    m_pauseAfterRewind = false;
    m_fastForward = false;
    updatePlayback();
    m_undoState.reset();
    {
        std::lock_guard lock(m_slotsMutex);
        m_slots = StateSlots();
    }
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

StateSlots GuiHost::stateSlots() const
{
    std::lock_guard lock(m_slotsMutex);
    return m_slots;
}

Result GuiHost::saveState(CallContext& ctx, int slot, const QString& name)
{
    const StateSlots gameSlots = stateSlots();
    if (!gameSlots.isOpen()) return Result::fail("NotFound", "this game has no library folder for save states");
    const SaveStateBlob blob = ctx.session.machine().captureState();
    const QByteArray state(reinterpret_cast<const char*>(blob.data), static_cast<qsizetype>(blob.size));
    QString error;
    StateSlots writer = gameSlots;
    if (!writer.save(slot, state, currentFrame(ctx), name, &error)) return Result::fail("WriteError", str(error), {{"slot", slot}});
    m_window.showMessage(QObject::tr("State saved to slot %1").arg(slot));
    return Result::ok(slotInfoJson(gameSlots.info(slot)));
}

Result GuiHost::loadState(CallContext& ctx, int slot)
{
    QString error;
    const std::optional<QByteArray> state = stateSlots().load(slot, &error);
    if (!state) return Result::fail("NotFound", str(error), {{"slot", slot}});
    Result r = loadStateBytes(ctx, *state);
    if (r.isError()) return r;
    m_window.showMessage(QObject::tr("State loaded from slot %1").arg(slot));
    return Result::ok({{"slot", slot}});
}

Result GuiHost::loadStateBytes(CallContext& ctx, const QByteArray& state, bool keepUndo)
{
    // The core leaves a loaded machine paused (001 data-model "State
    // transitions"), and so does the GUI: the player sees PAUSED and goes
    // on when ready.
    const SaveStateBlob before = ctx.session.machine().captureState();
    QByteArray replaced(reinterpret_cast<const char*>(before.data), static_cast<qsizetype>(before.size));
    std::string blobId;
    Result r = upload(ctx, state, blobId);
    if (r.isError()) return r;
    r = call(ctx, "core.load_state", {{"blobId", blobId}});
    call(ctx, "session.blob_delete", {{"blobId", blobId}});
    // A rejected state changes nothing, Undo included (the core never
    // loads part of one).
    if (r.isError()) return r;
    if (keepUndo) m_undoState = std::move(replaced);
    return Result::ok();
}

Result GuiHost::selectSlot(int slot)
{
    m_selectedSlot = slot;
    if (!m_identity.sha1.isEmpty()) m_settings.setSelectedSlot(m_identity.sha1, slot);
    m_window.showMessage(QObject::tr("Slot %1 selected").arg(slot));
    return Result::ok({{"slot", slot}});
}

namespace {

Json overscanJson(const AppSettings::VideoSettings& v)
{
    return {{"top", v.overscanTop}, {"bottom", v.overscanBottom}, {"left", v.overscanLeft}, {"right", v.overscanRight}};
}

Json videoJson(const AppSettings::VideoSettings& v, const AppSettings& settings)
{
    return {{"scale", v.scale}, {"fullscreen", v.fullscreen}, {"integerScaling", v.integerScaling},
        {"aspect", str(v.aspect)}, {"overscan", overscanJson(v)}, {"filter", str(v.filter)},
        {"alwaysOnTop", v.alwaysOnTop}, {"showFps", settings.showFps()}, {"showOsd", settings.showOsd()}};
}

} // namespace

Result GuiHost::setVideo(const std::string& key, const Json& value, bool showPage)
{
    AppSettings::VideoSettings v = m_settings.video();
    const auto oneOf = [&](std::initializer_list<const char*> names) {
        if (!value.is_string()) return false;
        for (const char* n : names) {
            if (value.get<std::string>() == n) return true;
        }
        return false;
    };
    if (key == "video.scale") {
        if (!value.is_number_integer() || value.get<long long>() < 1 || value.get<long long>() > 6) {
            return invalid("/value", "must be a scale from 1 to 6");
        }
        v.scale = value.get<int>();
    } else if (key == "video.aspect") {
        if (!oneOf({"square", "8:7", "4:3"})) return invalid("/value", "must be square, 8:7 or 4:3");
        v.aspect = QString::fromStdString(value.get<std::string>());
    } else if (key == "video.filter") {
        if (!oneOf({"nearest", "smooth", "scanlines"})) return invalid("/value", "must be nearest, smooth or scanlines");
        v.filter = QString::fromStdString(value.get<std::string>());
    } else if (key == "video.overscan") {
        // Any of the edges; the others keep their value.
        if (!value.is_object()) return invalid("/value", "must be an object of top, bottom, left and right");
        for (const auto& [edge, crop] : value.items()) {
            int* field = edge == "top" ? &v.overscanTop
                : edge == "bottom"     ? &v.overscanBottom
                : edge == "left"       ? &v.overscanLeft
                : edge == "right"      ? &v.overscanRight
                                       : nullptr;
            if (!field) return invalid("/value/" + edge, "must be top, bottom, left or right");
            if (!crop.is_number_integer() || crop.get<long long>() < 0 || crop.get<long long>() > 16) {
                return invalid("/value/" + edge, "must be 0 to 16 pixels");
            }
            *field = crop.get<int>();
        }
    } else {
        if (!value.is_boolean()) return invalid("/value", "must be true or false");
        const bool on = value.get<bool>();
        if (key == "video.showFps" || key == "video.showOsd") {
            key == "video.showFps" ? m_settings.setShowFps(on) : m_settings.setShowOsd(on);
            m_window.displayChanged();
            return Result::ok({{"key", key}, {"value", value}});
        }
        (key == "video.fullscreen"       ? v.fullscreen
            : key == "video.integerScaling" ? v.integerScaling
                                            : v.alwaysOnTop) = on;
    }
    m_settings.setVideo(v);
    m_window.videoChanged(showPage);
    const Json saved = videoJson(v, m_settings);
    return Result::ok({{"key", key}, {"value", saved[key.substr(6)]}});
}

void GuiHost::registerCommands(api::Registry& registry)
{
    add(registry, "gui.open_rom",
        "Open a .nes file from any folder and play it; without path, show the file dialog",
        s::object({{"path", s::string("a file path (not limited to the library)")},
            {"resume", s::boolean("with emulation.resumeOnOpen: load the automatic state (true) or start afresh "
                                  "(false); without it the window asks")}}),
        s::object({{"dialog", s::boolean("the file dialog was shown instead")},
            {"cartridge", Json{{"type", "object"}, {"description", "as core.load_rom"}}},
            {"runState", s::stringEnum({"Running"})}, {"identity", s::string()},
            {"batterySave", s::string("the battery save file, or empty")},
            {"warning", s::string("a battery save set aside, or another instance holds the game")},
            {"autoState", s::string("the automatic state the game has (emulation.resumeOnOpen)")},
            {"resumed", s::boolean("the automatic state was loaded")}}),
        true, false, [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                m_window.showOpenRomDialog();
                return Result::ok({{"dialog", true}});
            }
            return openRom(ctx, QString::fromStdString(args["path"].get<std::string>()),
                args.contains("resume") ? std::optional<bool>(args["resume"].get<bool>()) : std::nullopt);
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

    add(registry, "gui.save_state", "Save the game's state into a slot (replacing what it holds), with a thumbnail",
        s::object({{"slot", slotSchema()}, {"name", nameSchema()}}, {"slot"}), slotInfoSchema(), true, true,
        [this](CallContext& ctx, const Json& args) {
            return saveState(ctx, args["slot"].get<int>(), stateName(args));
        });

    add(registry, "gui.load_state", "Load a slot (Undo Load State brings back what it replaced)",
        s::object({{"slot", slotSchema()}}, {"slot"}), s::object({{"slot", slotSchema()}}), true, true,
        [this](CallContext& ctx, const Json& args) { return loadState(ctx, args["slot"].get<int>()); });

    add(registry, "gui.quick_save", "Save into the selected slot (replacing what it holds)",
        s::object({{"name", nameSchema()}}), slotInfoSchema(), true, true,
        [this](CallContext& ctx, const Json& args) { return saveState(ctx, m_selectedSlot.load(), stateName(args)); });

    add(registry, "gui.quick_load", "Load the selected slot", s::object({}), s::object({{"slot", slotSchema()}}),
        true, true, [this](CallContext& ctx, const Json&) { return loadState(ctx, m_selectedSlot.load()); });

    add(registry, "gui.select_slot", "Choose the slot Quick Save and Quick Load use (per game)",
        s::object({{"slot", slotSchema()}}, {"slot"}), s::object({{"slot", slotSchema()}}), true, false,
        [this](CallContext&, const Json& args) { return selectSlot(args["slot"].get<int>()); });

    add(registry, "gui.undo_load_state", "Go back to the state the last load replaced (one level)",
        s::object({}), s::object({}), true, true, [this](CallContext& ctx, const Json&) {
            if (!m_undoState) return Result::fail("NotFound", "no state load to undo");
            const QByteArray state = *m_undoState;
            Result r = loadStateBytes(ctx, state, false);
            if (r.isError()) return r;
            m_undoState.reset();
            m_window.showMessage(QObject::tr("Load undone"));
            return Result::ok();
        });

    add(registry, "gui.save_state_file", "Save the state to a file; without path, show the file dialog",
        s::object({{"path", s::string("a file path")}}), s::object({{"dialog", s::boolean()}, {"path", s::string()}}),
        true, true, [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                m_window.showFileDialog(Window::FileDialog::SaveState);
                return Result::ok({{"dialog", true}});
            }
            const QString path = QString::fromStdString(args["path"].get<std::string>());
            if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
            const SaveStateBlob blob = ctx.session.machine().captureState();
            QString error;
            if (!StateSlots::writeFile(path,
                    QByteArray(reinterpret_cast<const char*>(blob.data), static_cast<qsizetype>(blob.size)), &error)) {
                return Result::fail("WriteError", str(error), {{"path", str(path)}});
            }
            m_window.showMessage(QObject::tr("State saved to %1").arg(QFileInfo(path).fileName()));
            return Result::ok({{"path", str(path)}});
        });

    add(registry, "gui.load_state_file", "Load a state file; without path, show the file dialog",
        s::object({{"path", s::string("a file path")}}), s::object({{"dialog", s::boolean()}, {"path", s::string()}}),
        true, true, [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                m_window.showFileDialog(Window::FileDialog::LoadState);
                return Result::ok({{"dialog", true}});
            }
            const QString path = QString::fromStdString(args["path"].get<std::string>());
            QString error;
            const std::optional<QByteArray> state = StateSlots::readFile(path, &error);
            if (!state) return invalid("/path", str(error));
            Result r = loadStateBytes(ctx, *state);
            if (r.isError()) return r;
            m_window.showMessage(QObject::tr("State loaded from %1").arg(QFileInfo(path).fileName()));
            return Result::ok({{"path", str(path)}});
        });

    add(registry, "gui.export_battery_save", "Write the battery save to a raw .sav file; without path, show the file dialog",
        s::object({{"path", s::string("a file path")}}), s::object({{"dialog", s::boolean()}, {"path", s::string()}}),
        true, true, [this](CallContext& ctx, const Json& args) {
            const SramRange sram = ctx.session.machine().sram();
            if (sram.size == 0) return Result::fail("NoBattery", "the cartridge has no battery-backed RAM");
            if (!args.contains("path")) {
                m_window.showFileDialog(Window::FileDialog::ExportBattery);
                return Result::ok({{"dialog", true}});
            }
            const QString path = QString::fromStdString(args["path"].get<std::string>());
            if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
            QString error;
            const QByteArray bytes(reinterpret_cast<const char*>(sram.data), static_cast<qsizetype>(sram.size));
            if (!StateSlots::writeFile(path, bytes, &error)) {
                return Result::fail("WriteError", str(error), {{"path", str(path)}});
            }
            m_window.showMessage(QObject::tr("Battery save exported to %1").arg(QFileInfo(path).fileName()));
            return Result::ok({{"path", str(path)}});
        });

    add(registry, "gui.import_battery_save",
        "Replace the battery save with a raw .sav file (padded or trimmed) and reset the game; without path, show the file dialog",
        s::object({{"path", s::string("a file path")}}), s::object({{"dialog", s::boolean()}, {"path", s::string()}}),
        true, true, [this](CallContext& ctx, const Json& args) {
            Machine& machine = ctx.session.machine();
            if (machine.sram().size == 0) return Result::fail("NoBattery", "the cartridge has no battery-backed RAM");
            if (!args.contains("path")) {
                m_window.showFileDialog(Window::FileDialog::ImportBattery);
                return Result::ok({{"dialog", true}});
            }
            const QString path = QString::fromStdString(args["path"].get<std::string>());
            if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
            if (!QFileInfo(path).isFile()) return Result::fail("NotFound", "no such file", {{"path", str(path)}});
            QString error;
            const std::optional<QByteArray> bytes = StateSlots::readFile(path, &error);
            if (!bytes) return Result::fail("NotFound", str(error), {{"path", str(path)}});
            // The whole save is replaced: a short file is padded with zeros
            // (loadSram alone would keep the old bytes past its end).
            QByteArray ram = bytes->left(static_cast<qsizetype>(machine.sram().size));
            ram.append(static_cast<qsizetype>(machine.sram().size) - ram.size(), '\0');
            const Result loaded = call(ctx, "core.load_sram", {{"bytes", ram.toBase64().toStdString()}});
            if (loaded.isError()) return loaded;
            // Games read their save when they start, so the game starts
            // again; the library's save follows now, even while paused.
            const Result reset = call(ctx, "core.reset");
            if (reset.isError()) return reset;
            m_battery.poll(machine);
            m_window.showMessage(QObject::tr("Battery save imported from %1").arg(QFileInfo(path).fileName()));
            return Result::ok({{"path", str(path)}});
        });

    add(registry, "gui.take_screenshot", "Save the picture as a PNG in the library's screenshots folder (256x240, unfiltered)",
        s::object({}), s::object({{"path", s::string()}}, {"path"}), false, true,
        [this](CallContext& ctx, const Json&) {
            const QImage image = currentFrame(ctx);
            if (image.isNull()) return Result::fail("NotFound", "there is no picture yet");
            const Library library = m_window.library();
            if (m_identity.sha1.isEmpty() || !library.isUsable()) {
                return Result::fail("WriteError", "the library cannot be written; see Settings > Library");
            }
            const QDir folder(library.gameFile(Library::Folder::Screenshots, m_identity, m_gameName, {}));
            if (!folder.mkpath(QStringLiteral("."))) {
                return Result::fail("WriteError", "cannot create " + str(folder.path()));
            }
            const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            QString path = folder.filePath(stamp + QStringLiteral(".png"));
            for (int n = 2; QFile::exists(path); ++n) {
                path = folder.filePath(QStringLiteral("%1-%2.png").arg(stamp).arg(n));
            }
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
                return Result::fail("WriteError", "cannot write " + str(path), {{"path", str(path)}});
            }
            m_window.showMessage(QObject::tr("Screenshot saved"));
            return Result::ok({{"path", str(path)}});
        });

    add(registry, "gui.toggle_fps", "Show or hide the frame rate over the game", s::object({}),
        s::object({{"showFps", s::boolean()}}, {"showFps"}), false, false, [this](CallContext&, const Json&) {
            m_settings.setShowFps(!m_settings.showFps());
            m_window.displayChanged();
            return Result::ok({{"showFps", m_settings.showFps()}});
        });

    add(registry, "gui.toggle_osd", "Show or hide short on-screen messages", s::object({}),
        s::object({{"showOsd", s::boolean()}}, {"showOsd"}), false, false, [this](CallContext&, const Json&) {
            m_settings.setShowOsd(!m_settings.showOsd());
            m_window.displayChanged();
            return Result::ok({{"showOsd", m_settings.showOsd()}});
        });

    add(registry, "gui.list_slots", "The game's save-state slots 1-10 and the selected slot",
        s::object({}), s::object({{"slots", s::array(slotInfoSchema(), StateSlots::kSlots)},
            {"selectedSlot", slotSchema()}}, {"slots", "selectedSlot"}),
        false, false, [this](CallContext&, const Json&) {
            const StateSlots gameSlots = stateSlots();
            Json list = Json::array();
            for (int slot = 1; slot <= StateSlots::kSlots; ++slot) list.push_back(slotInfoJson(gameSlots.info(slot)));
            return Result::ok({{"slots", list}, {"selectedSlot", m_selectedSlot.load()}});
        });

    add(registry, "gui.set_rewind", "Hold rewind: play runs backwards while on (the history is the last 30 s or more)",
        s::object({{"on", s::boolean()}}, {"on"}), s::object({{"rewinding", s::boolean()}}, {"rewinding"}), true, true,
        [this](CallContext& ctx, const Json& args) { return setRewind(ctx, args["on"].get<bool>()); });

    add(registry, "gui.set_fast_forward", "Hold fast-forward: the game runs at the fast-forward speed while on",
        s::object({{"on", s::boolean()}}, {"on"}), s::object({{"fastForward", s::boolean()}}, {"fastForward"}), true,
        true, [this](CallContext&, const Json& args) {
            m_fastForward = args["on"].get<bool>();
            updatePlayback();
            return Result::ok({{"fastForward", m_fastForward}});
        });

    add(registry, "gui.toggle_fast_forward", "Turn fast-forward on or off", s::object({}),
        s::object({{"fastForward", s::boolean()}}, {"fastForward"}), true, true, [this](CallContext&, const Json&) {
            m_fastForward = !m_fastForward;
            updatePlayback();
            return Result::ok({{"fastForward", m_fastForward}});
        });

    add(registry, "gui.set_speed", "Set the game's speed: a percent, unlimited, or a step to the next listed speed",
        s::object({{"percent", Json{{"type", Json::array({"integer", "string"})},
                       {"description", "25, 50, 75, 100, 150, 200, 300, 400, or \"unlimited\""}}},
            {"step", s::stringEnum({"slower", "faster"})}}),
        s::object({{"speed", Json{{"type", Json::array({"integer", "string"})}, {"description", "percent, or \"unlimited\""}}}},
            {"speed"}), true, false,
        [this](CallContext&, const Json& args) {
            const std::vector<int>& list = speeds();
            if (args.contains("step")) {
                const bool faster = args["step"] == "faster";
                if (m_speed == 0) return setSpeed(faster ? 0 : list.back());
                const auto it = std::find(list.begin(), list.end(), m_speed);
                const auto index = it == list.end() ? 3 : static_cast<int>(it - list.begin());
                if (faster) return setSpeed(index + 1 < static_cast<int>(list.size()) ? list[static_cast<std::size_t>(index + 1)] : 0);
                return setSpeed(list[static_cast<std::size_t>(std::max(0, index - 1))]);
            }
            if (!args.contains("percent")) return Result::fail("InvalidArguments", "give percent or step");
            if (args["percent"].is_string()) {
                if (args["percent"] != "unlimited") {
                    return Result::fail("InvalidArguments", "percent is a number or \"unlimited\"", {{"path", "/percent"}});
                }
                return setSpeed(0);
            }
            const int percent = args["percent"].get<int>();
            if (std::find(list.begin(), list.end(), percent) == list.end()) {
                return Result::fail("InvalidArguments", "the speeds are 25, 50, 75, 100, 150, 200, 300 and 400 %",
                    {{"path", "/percent"}});
            }
            return setSpeed(percent);
        });

    add(registry, "gui.frame_advance", "Pause and run exactly one frame", s::object({}), s::object({}), true, true,
        [this](CallContext& ctx, const Json&) {
            if (isRunning(ctx)) {
                Result r = call(ctx, "core.pause");
                if (r.isError()) return r;
            }
            Machine& machine = ctx.session.machine();
            const SaveStateBlob state = machine.captureState();
            m_rewind.push(state.data, state.size, machine.controller(Port::One), machine.controller(Port::Two));
            machine.frameAdvance();
            return Result::ok();
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

    // G034: the contract's commands whose features land in later phases.
    // Their menu items are present but disabled; the commands are in the
    // catalog with their contract arguments and return NotImplemented.
    const auto later = [&registry](const char* name, std::string description, Json input, bool needsRom,
                           const char* phase) {
        add(registry, name, std::move(description) + " (coming in " + phase + ")", std::move(input), s::object({}),
            true, needsRom, [name, phase](CallContext&, const Json&) {
                return Result::fail("NotImplemented", std::string(name) + " is coming in a later update",
                    {{"phase", phase}});
            });
    };
    const Json none = s::object({});
    const Json fileArgs = s::object({{"path", s::string("a file path; without it, a file dialog")}});
    later("gui.set_region", "Force NTSC or PAL, or follow the header",
        s::object({{"region", s::stringEnum({"auto", "ntsc", "pal"})}}, {"region"}), false, "G6");
    later("gui.set_palette", "A built-in palette by name, or a .pal file",
        s::object({{"name", s::string()}, {"path", s::string()}}), false, "G6");
    later("gui.show_cheats", "Open the cheats window", none, true, "G7");
    later("gui.show_cheat_search", "Open the cheat search", none, true, "G7");
    later("gui.record_movie", "Record a movie from power-on or the current state",
        s::object({{"path", s::string()}, {"from", s::stringEnum({"power_on", "state"})}}), true, "G7");
    later("gui.play_movie", "Play a movie", fileArgs, true, "G7");
    later("gui.stop_movie", "Stop recording or playing the movie", none, true, "G7");
    later("gui.toggle_movie_read_only", "Play the movie without recording over it", none, true, "G7");
    add(registry, "gui.toggle_audio_recording",
        "Start or stop recording the game's sound to a WAV file (16-bit mono, 48 kHz); without path, into the "
        "library's recordings folder",
        s::object({{"path", s::string("an absolute .wav path")}}),
        s::object({{"recording", s::boolean()}, {"path", s::string()}, {"seconds", Json{{"type", "number"}}}},
            {"recording", "path"}),
        true, true, [this](CallContext&, const Json& args) {
            if (m_tap.wav.isOpen()) return stopRecording();
            QString path;
            if (args.contains("path")) {
                path = QString::fromStdString(args["path"].get<std::string>());
                if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
            } else {
                const Library library = m_window.library();
                if (m_identity.sha1.isEmpty() || !library.isUsable()) {
                    return Result::fail("WriteError", "the library cannot be written; see Settings > Library");
                }
                const QDir folder(library.gameFile(Library::Folder::Recordings, m_identity, m_gameName, {}));
                if (!folder.mkpath(QStringLiteral("."))) return Result::fail("WriteError", "cannot create " + str(folder.path()));
                const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
                path = folder.filePath(stamp + QStringLiteral(".wav"));
                for (int n = 2; QFile::exists(path); ++n) path = folder.filePath(QStringLiteral("%1-%2.wav").arg(stamp).arg(n));
            }
            QString error;
            if (!m_tap.wav.open(path, &error)) return Result::fail("WriteError", "cannot write " + str(path) + ": " + str(error));
            m_window.recordingChanged(path);
            m_window.showMessage(QObject::tr("Recording audio"));
            return Result::ok({{"recording", true}, {"path", str(path)}});
        });
    later("gui.show_debugger", "Open the debugger", none, true, "G8");
    later("gui.show_memory_viewer", "Open the memory viewer", none, true, "G8");
    later("gui.show_ppu_viewer", "Open the PPU viewer",
        s::object({{"view", s::stringEnum({"patterns", "nametables", "sprites", "palette"})}}), true, "G8");
    later("gui.show_trace_logger", "Open the trace logger", none, true, "G8");
    later("gui.write_memory_dump", "Write a memory dump to the library's debug folder", none, true, "G8");
    later("gui.show_user_guide", "Open the user guide", none, false, "G9");
    later("gui.open_log_folder", "Show the log folder in the file manager", none, false, "G9");

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

    // View (contract "View"): each changes a video.* setting.
    const auto valueAs = [](const char* name, Result r) {
        if (r.isError()) return r;
        return Result::ok({{name, r.value["value"]}});
    };
    add(registry, "gui.set_scale", "Size the game area to a whole multiple of the picture (after aspect and crop)",
        s::object({{"scale", s::integer(1, 6)}}, {"scale"}), s::object({{"scale", s::integer(1, 6)}}, {"scale"}), true,
        false, [this, valueAs](CallContext&, const Json& args) { return valueAs("scale", setVideo("video.scale", args["scale"])); });
    add(registry, "gui.toggle_fullscreen", "Enter or leave fullscreen (borderless, on the window's screen)",
        s::object({}), s::object({{"fullscreen", s::boolean()}}, {"fullscreen"}), true, false,
        [this, valueAs](CallContext&, const Json&) {
            return valueAs("fullscreen", setVideo("video.fullscreen", !m_settings.video().fullscreen));
        });
    add(registry, "gui.toggle_integer_scaling", "Scale the picture by whole multiples only", s::object({}),
        s::object({{"integerScaling", s::boolean()}}, {"integerScaling"}), true, false,
        [this, valueAs](CallContext&, const Json&) {
            return valueAs("integerScaling", setVideo("video.integerScaling", !m_settings.video().integerScaling));
        });
    add(registry, "gui.set_aspect", "The pixel aspect ratio: square, 8:7 (NTSC hardware) or a 4:3 stretch",
        s::object({{"aspect", s::stringEnum({"square", "8:7", "4:3"})}}, {"aspect"}),
        s::object({{"aspect", s::string()}}, {"aspect"}), true, false,
        [this, valueAs](CallContext&, const Json& args) { return valueAs("aspect", setVideo("video.aspect", args["aspect"])); });
    const Json edge = s::integer(0, 16, "pixels hidden at this edge");
    add(registry, "gui.set_overscan",
        "Hide the picture's edges: a preset (none, or tv: 8 lines top and bottom), or any of the edges; custom "
        "without edges opens Settings > Video",
        s::object({{"preset", s::stringEnum({"none", "tv", "custom"})}, {"top", edge}, {"bottom", edge},
            {"left", edge}, {"right", edge}}),
        s::object({{"overscan", s::object({{"top", edge}, {"bottom", edge}, {"left", edge}, {"right", edge}})}},
            {"overscan"}),
        true, false, [this, valueAs](CallContext&, const Json& args) {
            Json edges = Json::object();
            const std::string preset = args.value("preset", "custom");
            if (preset == "none") edges = {{"top", 0}, {"bottom", 0}, {"left", 0}, {"right", 0}};
            if (preset == "tv") edges = {{"top", 8}, {"bottom", 8}, {"left", 0}, {"right", 0}};
            for (const char* e : {"top", "bottom", "left", "right"}) {
                if (args.contains(e)) edges[e] = args[e];
            }
            const bool ask = preset == "custom" && edges.empty();
            return valueAs("overscan", setVideo("video.overscan", edges, ask));
        });
    add(registry, "gui.set_filter", "The picture filter: nearest, smooth or scanlines",
        s::object({{"filter", s::stringEnum({"nearest", "smooth", "scanlines"})}}, {"filter"}),
        s::object({{"filter", s::string()}}, {"filter"}), true, false,
        [this, valueAs](CallContext&, const Json& args) { return valueAs("filter", setVideo("video.filter", args["filter"])); });
    add(registry, "gui.toggle_always_on_top", "Keep the window above others", s::object({}),
        s::object({{"alwaysOnTop", s::boolean()}}, {"alwaysOnTop"}), true, false,
        [this, valueAs](CallContext&, const Json&) {
            return valueAs("alwaysOnTop", setVideo("video.alwaysOnTop", !m_settings.video().alwaysOnTop));
        });

    // --- Key Bindings (#108, contract "Key Bindings page") ----------------------

    add(registry, "gui.show_shortcuts", "Show the Key Bindings page (pauses a running game; Back resumes it)",
        s::object({{"section", s::stringEnum({"hotkeys", "controls"})}}),
        s::object({{"section", s::string()}}, {"section"}), false, false, [this](CallContext& ctx, const Json& args) {
            const std::string section = args.value("section", "hotkeys");
            const bool paused = isRunning(ctx) && !call(ctx, "core.pause").isError();
            m_window.showKeyBindings(QString::fromStdString(section), paused);
            return Result::ok({{"section", section}});
        });

    add(registry, "gui.list_audio_devices", "The playback devices, by name (for audio.device)", s::object({}),
        s::object({{"devices", s::array(s::string(), 100)}}, {"devices"}), false, false, [](CallContext&, const Json&) {
            Json list = Json::array();
            for (const std::string& name : qt::AudioOutput::devices()) list.push_back(name);
            return Result::ok({{"devices", list}});
        });

    add(registry, "gui.list_gamepads", "The gamepads plugged in, with the player each drives",
        s::object({}),
        s::object({{"gamepads", s::array(s::object({{"port", s::integer(1, 2)}, {"name", s::string()},
                                                       {"guid", s::string("the same for the same model")}},
                                             {"port", "name", "guid"}),
                                    2)}},
            {"gamepads"}),
        false, false, [this](CallContext&, const Json&) {
            Json list = Json::array();
            for (const auto& pad : m_window.gamepads()) {
                list.push_back({{"port", pad.port}, {"name", str(pad.name)}, {"guid", str(pad.guid)}});
            }
            return Result::ok({{"gamepads", list}});
        });

    add(registry, "gui.list_bindings", "Every menu action and game button with its bindings and defaults",
        s::object({}), s::object({{"bindings", s::array(bindingSchema(), 1000)}}, {"bindings"}), false, false,
        [this](CallContext&, const Json&) {
            Json list = Json::array();
            for (const auto& e : m_window.keyBindings().entries()) list.push_back(bindingJson(e));
            return Result::ok({{"bindings", list}});
        });

    const Json index = s::integer(0, 1, "which of the two bindings (default 0)");
    add(registry, "gui.set_binding",
        "Bind a key (portable text, such as Ctrl+Shift+S) to an entry; a conflict fails unless force",
        s::object({{"id", s::string("an id from gui.list_bindings")}, {"binding", s::string()}, {"index", index},
                      {"force", s::boolean("take the binding from the entries that have it")}},
            {"id", "binding"}),
        bindingSchema(), true, false, [this](CallContext&, const Json& args) {
            const auto change = m_window.keyBindings().set(QString::fromStdString(args["id"].get<std::string>()),
                args.value("index", 0), QString::fromStdString(args["binding"].get<std::string>()),
                args.value("force", false));
            if (change.status == KeyBindings::Change::Status::Done) m_window.bindingsChanged();
            return bindingResult(change);
        });

    add(registry, "gui.clear_binding", "Clear one of an entry's bindings, or both without index",
        s::object({{"id", s::string()}, {"index", index}}, {"id"}), bindingSchema(), true, false,
        [this](CallContext&, const Json& args) {
            const auto change = m_window.keyBindings().clear(QString::fromStdString(args["id"].get<std::string>()),
                args.value("index", -1));
            if (change.status == KeyBindings::Change::Status::Done) m_window.bindingsChanged();
            return bindingResult(change);
        });

    add(registry, "gui.reset_bindings", "Reset an entry, or every entry without id, to the defaults",
        s::object({{"id", s::string()}}), s::object({{"reset", s::array(bindingSchema(), 1000)}}, {"reset"}), true,
        false, [this](CallContext&, const Json& args) {
            const auto reset = m_window.keyBindings().reset(QString::fromStdString(args.value("id", "")));
            if (!reset) return invalid("/id", "no binding has this id");
            Json list = Json::array();
            for (const auto& e : *reset) list.push_back(bindingJson(e));
            m_window.bindingsChanged();
            return Result::ok({{"reset", list}});
        });

    add(registry, "gui.show_settings", "Show Settings, at a page or the last one shown",
        s::object({{"page", s::stringEnum(kSettingsPages)}}), s::object({{"page", s::string()}}, {"page"}),
        false, false, [this](CallContext& ctx, const Json& args) {
            const QString page = args.contains("page") ? QString::fromStdString(args["page"].get<std::string>())
                                                       : m_settings.lastSettingsPage();
            // Hotkeys is the Key Bindings page (#108).
            if (page == QLatin1String("hotkeys")) {
                const bool paused = isRunning(ctx) && !call(ctx, "core.pause").isError();
                m_window.showKeyBindings(page, paused);
                return Result::ok({{"page", str(page)}});
            }
            // Every other page is in the Settings dialog.
            m_window.showSettings(page);
            return Result::ok({{"page", str(page)}});
        });

    add(registry, "gui.get_settings", "Every setting's value and schema, or one group's",
        s::object({{"group", s::stringEnum({"video", "audio", "emulation", "library", "servers", "hotkeys", "input"})}}),
        s::object({{"values", Json{{"type", "object"}}}, {"schema", Json{{"type", "object"}}}}, {"values", "schema"}),
        false, false, [this, edge](CallContext&, const Json&) {
            const Library library = m_window.library();
            const AppSettings::ServerSettings servers = m_settings.servers();
            const AppSettings::InputSettings input = m_settings.input();
            const AppSettings::AudioSettings audio = m_settings.audio();
            const AppSettings::EmulationSettings emulation = m_settings.emulation();
            const Json values = {{"video", videoJson(m_settings.video(), m_settings)},
                {"audio", {{"device", str(audio.device)}, {"volume", audio.volume}, {"muted", audio.muted},
                              {"latencyMs", audio.latencyMs}, {"fastForwardAudio", str(audio.fastForwardAudio)}}},
                {"emulation", {{"pauseWhenInactive", emulation.pauseWhenInactive},
                                  {"resumeOnOpen", emulation.resumeOnOpen},
                                  {"fastForwardSpeed", emulation.fastForwardSpeed}}},
                {"library", {{"root", str(library.root())},
                                     {"roms", str(library.folder(Library::Folder::Roms))},
                                     {"romFolders", romFoldersValue(m_settings)}}},
                {"servers", {{"apiEnabled", servers.apiEnabled}, {"apiPort", servers.apiPort},
                                {"mcpEnabled", servers.mcpEnabled}, {"mcpPort", servers.mcpPort}}},
                {"hotkeys", storedBindings(m_window.keyBindings(), false)},
                {"input", {{"turboRate", input.turboRate}, {"allowOppositeDirections", input.allowOppositeDirections},
                              {"pauseOnGamepadDisconnect", input.pauseOnGamepadDisconnect},
                              {"bindings", storedBindings(m_window.keyBindings(), true)}}}};
            const Json schema = {{"video.scale", s::integer(1, 6, "the window scale (3)")},
                {"video.fullscreen", s::boolean("fullscreen, borderless on the window's screen")},
                {"video.integerScaling", s::boolean("whole multiples only (on)")},
                {"video.aspect", s::stringEnum({"square", "8:7", "4:3"})},
                {"video.overscan", s::object({{"top", edge}, {"bottom", edge}, {"left", edge}, {"right", edge}})},
                {"video.filter", s::stringEnum({"nearest", "smooth", "scanlines"})},
                {"video.alwaysOnTop", s::boolean("keep the window above others")},
                {"video.showFps", s::boolean("the frame rate over the game (off)")},
                {"video.showOsd", s::boolean("short on-screen messages (on)")},
                {"audio.device", s::string("a name from gui.list_audio_devices, or default (the system's)")},
                {"audio.volume", s::integer(0, 100, "percent (100)")},
                {"audio.muted", s::boolean()},
                {"audio.latencyMs", s::integer(32, 200, "the target audio latency (64)")},
                {"audio.fastForwardAudio", s::stringEnum({"stretch", "mute"})},
                {"emulation.pauseWhenInactive", s::boolean("pause while the window is in the background (on)")},
                {"emulation.resumeOnOpen", s::boolean("keep an automatic state on close and offer it on open (off)")},
                {"emulation.fastForwardSpeed", s::integer(150, 400, "percent (300)")},
                {"library.root", s::string("absolute path of the library folder")},
                {"library.roms", s::string("the library's own roms folder (always scanned)")},
                {"library.romFolders", s::array(s::string("added with gui.add_rom_folder"), 1000)},
                {"servers.apiEnabled", s::boolean("the API listener (off by default; off turns MCP off)")},
                {"servers.apiPort", s::integer(0, 65535, "the API port; 0 picks a free one")},
                {"servers.mcpEnabled", s::boolean("the MCP server (off by default; on turns the API on)")},
                {"servers.mcpPort", s::integer(0, 65535, "the MCP port; 0 picks a free one")},
                {"hotkeys", s::string("the menu bindings that differ from the defaults, by id (gui.set_binding)")},
                {"input.turboRate", s::integer(2, 30, "turbo presses a second (15 by default)")},
                {"input.allowOppositeDirections", s::boolean("Left with Right and Up with Down (off: the last pressed wins)")},
                {"input.pauseOnGamepadDisconnect", s::boolean("pause when a gamepad in use is unplugged (on by default)")},
                {"input.bindings", s::string("the game-button bindings that differ from the defaults, by id (gui.set_binding)")}};
            return Result::ok({{"values", values}, {"schema", schema}});
        });

    add(registry, "gui.set_setting", "Validate and apply a setting (gui.get_settings lists them)",
        s::object({{"key", s::stringEnum(kSettingKeys)}, {"value", Json::object()}}, {"key", "value"}),
        s::object({{"key", s::string()}, {"value", Json::object()}}, {"key", "value"}), true, false,
        [this](CallContext& ctx, const Json& args) {
            const std::string key = args["key"];
            if (key.rfind("video.", 0) == 0) return setVideo(key, args["value"]);
            const Json& value = args["value"];
            if (key.rfind("audio.", 0) == 0) {
                AppSettings::AudioSettings audio = m_settings.audio();
                if (key == "audio.device") {
                    if (!value.is_string()) return invalid("/value", "must be a device name, or default");
                    const std::string name = value.get<std::string>();
                    const auto devices = qt::AudioOutput::devices();
                    if (name != "default" && std::find(devices.begin(), devices.end(), name) == devices.end()) {
                        return invalid("/value", "no playback device is called " + name + " (gui.list_audio_devices)");
                    }
                    audio.device = QString::fromStdString(name);
                } else if (key == "audio.volume" || key == "audio.latencyMs") {
                    const bool volume = key == "audio.volume";
                    const long long lo = volume ? 0 : 32, hi = volume ? 100 : 200;
                    if (!value.is_number_integer() || value.get<long long>() < lo || value.get<long long>() > hi) {
                        return invalid("/value", "must be " + std::to_string(lo) + " to " + std::to_string(hi));
                    }
                    (volume ? audio.volume : audio.latencyMs) = value.get<int>();
                } else if (key == "audio.muted") {
                    if (!value.is_boolean()) return invalid("/value", "must be true or false");
                    audio.muted = value.get<bool>();
                } else {
                    if (value != "stretch" && value != "mute") return invalid("/value", "must be stretch or mute");
                    audio.fastForwardAudio = QString::fromStdString(value.get<std::string>());
                }
                m_settings.setAudio(audio);
                updatePlayback();
                m_window.audioSettingsChanged();
                return Result::ok({{"key", key}, {"value", value}});
            }
            if (key.rfind("emulation.", 0) == 0) {
                AppSettings::EmulationSettings emulation = m_settings.emulation();
                if (key == "emulation.fastForwardSpeed") {
                    if (!value.is_number_integer() || value.get<long long>() < 150 || value.get<long long>() > 400) {
                        return invalid("/value", "must be 150 to 400 percent");
                    }
                    emulation.fastForwardSpeed = m_fastForwardSpeed = value.get<int>();
                } else {
                    if (!value.is_boolean()) return invalid("/value", "must be true or false");
                    (key == "emulation.pauseWhenInactive" ? emulation.pauseWhenInactive : emulation.resumeOnOpen) =
                        value.get<bool>();
                }
                m_settings.setEmulation(emulation);
                updatePlayback();
                m_window.audioSettingsChanged();
                return Result::ok({{"key", key}, {"value", value}});
            }
            if (key == "library.root") {
                if (!args["value"].is_string()) return invalid("/value", "must be a string");
                return setLibraryRoot(ctx, QString::fromStdString(args["value"].get<std::string>()));
            }
            if (key.rfind("servers.", 0) == 0) {
                AppSettings::ServerSettings servers = m_settings.servers();
                if (key == "servers.apiEnabled" || key == "servers.mcpEnabled") {
                    if (!value.is_boolean()) return invalid("/value", "must be true or false");
                    const bool on = value.get<bool>();
                    // MCP needs the API: on turns the API on, API off turns MCP off.
                    if (key == "servers.apiEnabled") {
                        servers.apiEnabled = on;
                        if (!on) servers.mcpEnabled = false;
                    } else {
                        servers.mcpEnabled = on;
                        if (on) servers.apiEnabled = true;
                    }
                } else {
                    if (!value.is_number_integer() || value.get<long long>() < 0 || value.get<long long>() > 65535) {
                        return invalid("/value", "must be a port from 0 to 65535 (0 picks a free one)");
                    }
                    (key == "servers.apiPort" ? servers.apiPort : servers.mcpPort) = value.get<int>();
                }
                m_settings.setServers(servers);
                m_window.serversChanged();
                return Result::ok({{"key", key}, {"value", value}});
            }
            if (key.rfind("input.", 0) == 0) {
                AppSettings::InputSettings input = m_settings.input();
                if (key == "input.turboRate") {
                    if (!value.is_number_integer() || value.get<long long>() < 2 || value.get<long long>() > 30) {
                        return invalid("/value", "must be a rate from 2 to 30 presses a second");
                    }
                    input.turboRate = value.get<int>();
                } else {
                    if (!value.is_boolean()) return invalid("/value", "must be true or false");
                    (key == "input.allowOppositeDirections" ? input.allowOppositeDirections
                                                            : input.pauseOnGamepadDisconnect) = value.get<bool>();
                }
                m_settings.setInput(input);
                m_window.inputSettingsChanged();
                return Result::ok({{"key", key}, {"value", value}});
            }
            return invalid("/key", "unknown setting");
        });
}

} // namespace cutenes::app
