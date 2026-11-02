#include "host/gui_host.hpp"

#include "cheats/cheat_list.hpp"
#include "media/palettes.hpp"
#include "settings/app_settings.hpp"
#include "settings/key_bindings.hpp"
#include "update/update_feed.hpp"

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>
#include <cutenes/qt/audio_output.hpp>
#include <cutenes/version.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>

#include <algorithm>
#include <climits>

namespace cutenes::app {

namespace s = api::schema;
using api::CallContext;
using api::Json;
using api::Result;

namespace {

// The largest blob_chunk (002 command catalog).
constexpr qsizetype kChunk = 65536;

const std::vector<std::string> kSettingsPages = {
    "video", "audio", "input", "hotkeys", "emulation", "library", "servers", "updates"};
// The settings groups and keys implemented so far (data-model "Settings
// keys"); later phases add theirs.
const std::vector<std::string> kSettingKeys = {"video.scale", "video.fullscreen", "video.integerScaling",
    "video.aspect", "video.overscan", "video.filter", "video.alwaysOnTop", "video.pacing", "video.palette",
    "video.showFps", "video.showOsd",
    "audio.device", "audio.volume", "audio.muted", "audio.latencyMs", "audio.fastForwardAudio",
    "emulation.pauseWhenInactive", "emulation.resumeOnOpen", "emulation.fastForwardSpeed", "emulation.region",
    "library.root", "servers.apiEnabled", "servers.apiPort",
    "servers.mcpEnabled", "servers.mcpPort", "input.turboRate", "input.allowOppositeDirections",
    "input.pauseOnGamepadDisconnect", "updates.check", "updates.asked", "updates.skippedVersion"};

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
        m_midFrame = false;
        return;
    }
    if (m_rewound) {
        // Play goes on from the last frame shown, which is history again.
        m_rewind.push(m_rewound->state.data(), m_rewound->state.size(), m_rewound->input[0], m_rewound->input[1]);
        m_rewound.reset();
    }
    keepTracing(machine);
    // A frame a breakpoint or a step stopped part way goes on where it
    // was, its bookkeeping done already.
    if (!m_midFrame) {
        if (!beginFrame(machine)) return;
        m_midFrame = true;
    }
    if (!runToFrameEnd(machine)) return;
    m_midFrame = false;
    endFrame();
}

bool GuiHost::beginFrame(Machine& machine)
{
    if (m_movieMode == MovieStatus::Mode::Playing && !playMovieFrame(machine)) return false;
    const SaveStateBlob state = machine.captureState();
    m_rewind.push(state.data, state.size, machine.controller(Port::One), machine.controller(Port::Two));
    // Freeze cheats write before every frame (data-model "Cheat").
    for (const Cheat& cheat : activeCheats()) {
        if (cheat.enabled && cheat.kind == Cheat::Kind::Freeze) machine.writeMemory(cheat.address, &cheat.value, 1);
    }
    if (m_movieMode == MovieStatus::Mode::Recording) {
        m_movie.input.append(static_cast<char>(machine.controller(Port::One).to_ulong()));
        m_movie.input.append(static_cast<char>(machine.controller(Port::Two).to_ulong()));
    }
    return true;
}

void GuiHost::endFrame()
{
    if (m_movieMode != MovieStatus::Mode::None) {
        ++m_movieFrame;
        publishMovie(false);
    }
}

bool GuiHost::runToFrameEnd(Machine& machine)
{
    if (m_breakpoints.none()) {
        machine.runFrames(1); // also ends a frame begun part way
        return true;
    }
    // Breakpoints by stepping (G081): the same instructions runFrames(1)
    // runs, with PC checked before each.
    const std::uint64_t frame = machine.frameCounter();
    while (machine.frameCounter() == frame) {
        const std::uint16_t pc = machine.cpu().pc;
        if (m_breakpoints.test(pc) && m_skipBreakpoint != pc) {
            stopAtBreakpoint(pc);
            return false;
        }
        m_skipBreakpoint.reset();
        machine.stepInstruction();
    }
    return true;
}

void GuiHost::stopAtBreakpoint(std::uint16_t pc)
{
    if (api::Session* session = m_session.load()) session->call("core.pause");
    m_skipBreakpoint = pc; // Run goes on from here
    m_window.showMessage(QObject::tr("Breakpoint at $%1").arg(pc, 4, 16, QLatin1Char('0')).toUpper());
    m_window.debuggerStopped();
}

Result GuiHost::stepInstructions(CallContext& ctx, int count)
{
    if (isRunning(ctx)) {
        Result r = call(ctx, "core.pause");
        if (r.isError()) return r;
    }
    Machine& machine = ctx.session.machine();
    keepTracing(machine);
    for (int i = 0; i < count; ++i) {
        // An instruction that starts a frame starts its bookkeeping too, so
        // stepping keeps movies, rewind and cheats as play does.
        if (!m_midFrame) {
            if (!beginFrame(machine)) break;
            m_midFrame = true;
        }
        const std::uint64_t frame = machine.frameCounter();
        machine.stepInstruction();
        m_skipBreakpoint.reset();
        if (machine.frameCounter() != frame) {
            m_midFrame = false;
            endFrame();
        }
    }
    return Result::ok({{"cpu", call(ctx, "core.get_cpu").value["cpu"]}});
}

Result GuiHost::startTrace(Machine& machine, QString path)
{
    if (m_trace) {
        return Result::fail("Tracing", "the trace logger is logging already; stop it first", {{"path", str(m_tracePath)}});
    }
    if (path.isEmpty()) {
        // contracts/library-layout.md: debug/trace-<YYYYMMDD-HHMMSS>.log.
        const Library library = m_window.library();
        if (!library.isUsable()) return Result::fail("WriteError", "the library cannot be written; see Settings > Library");
        const QDir folder(library.folder(Library::Folder::Debug));
        if (!folder.mkpath(QStringLiteral("."))) return Result::fail("WriteError", "cannot create " + str(folder.path()));
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        path = folder.filePath(QStringLiteral("trace-%1.log").arg(stamp));
        for (int n = 2; QFile::exists(path); ++n) path = folder.filePath(QStringLiteral("trace-%1-%2.log").arg(stamp).arg(n));
    }
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Result::fail("WriteError", "cannot write " + str(path) + ": " + str(file->errorString()), {{"path", str(path)}});
    }
    m_trace = std::move(file);
    m_tracePath = path;
    m_traceBuffer.clear();
    m_traceLines = 0;
    m_traceFailed = false;
    keepTracing(machine);
    m_window.showMessage(QObject::tr("Trace logging to %1").arg(QFileInfo(path).fileName()));
    return Result::ok({{"path", str(path)}});
}

void GuiHost::keepTracing(Machine& machine)
{
    if (!m_trace) return;
    // The line is the one core.disassemble gives: nestest.log's format, with
    // the registers, PPU position and cycle count before the instruction.
    machine.setInstructionTrace([this, &machine](const TracePoint& tp) {
        m_traceBuffer += machine.disassemble(tp.cpu.pc);
        m_traceBuffer += '\n';
        ++m_traceLines;
        if (m_traceBuffer.size() >= (1u << 20)) flushTrace();
    });
}

void GuiHost::flushTrace()
{
    if (!m_trace || m_traceBuffer.empty()) return;
    const auto size = static_cast<qint64>(m_traceBuffer.size());
    if (m_trace->write(m_traceBuffer.data(), size) != size) m_traceFailed = true;
    m_traceBuffer.clear();
}

Result GuiHost::stopTrace(Machine& machine)
{
    if (!m_trace) return Result::fail("NotTracing", "the trace logger is not logging");
    machine.setInstructionTrace(nullptr);
    flushTrace();
    m_trace->close();
    const bool failed = m_traceFailed || m_trace->error() != QFileDevice::NoError;
    const QString path = m_tracePath;
    const std::uint64_t lines = m_traceLines;
    m_trace.reset();
    m_tracePath.clear();
    m_traceBuffer.clear();
    m_traceBuffer.shrink_to_fit();
    if (failed) {
        m_window.showMessage(QObject::tr("The trace could not be written in full"));
        return Result::fail("WriteError", "the trace could not be written in full (is the disk full?)",
            {{"path", str(path)}, {"lines", lines}});
    }
    m_window.showMessage(QObject::tr("Trace saved: %1 (%n instructions)", nullptr, static_cast<int>(std::min<std::uint64_t>(lines, INT_MAX)))
            .arg(QFileInfo(path).fileName()));
    return Result::ok({{"path", str(path)}, {"lines", lines}});
}

bool GuiHost::playMovieFrame(Machine& machine)
{
    // The console's buttons pressed before this frame, as recorded.
    api::Session* session = m_session.load();
    for (const Movie::Event& e : m_movie.events) {
        if (e.frame != m_movieFrame || !session) continue;
        if (e.kind == Movie::Event::Kind::Reset) {
            session->call("core.reset");
        } else if (e.kind == Movie::Event::Kind::Power) {
            session->call("core.power_on");
            session->call("core.resume");
        } else if (m_movieCheats) {
            // As recorded: a set cheat switched on writes once, then.
            for (const Cheat& c : e.cheats) {
                if (c.kind == Cheat::Kind::Set && !m_movieCheats->contains(c)) machine.writeMemory(c.address, &c.value, 1);
            }
            m_movieCheats = e.cheats;
            applyGameGenie(machine);
        }
    }
    if (m_movieFrame >= m_movie.frames()) {
        // The end: the last frame stays on screen, paused, so it can be
        // looked at (and compared).
        if (session) session->call("core.pause");
        stopMovie(true);
        return false;
    }
    const auto byte = [&](qint64 i) { return ButtonSet(static_cast<unsigned char>(m_movie.input[i])); };
    machine.setController(Port::One, byte(m_movieFrame * 2));
    machine.setController(Port::Two, byte(m_movieFrame * 2 + 1));
    return true;
}

GuiHost::MovieStatus GuiHost::movieStatus() const
{
    std::lock_guard lock(m_movieMutex);
    return m_movieStatus;
}

void GuiHost::publishMovie(bool changed)
{
    {
        std::lock_guard lock(m_movieMutex);
        m_movieStatus.mode = m_movieMode;
        m_movieStatus.frame = m_movieFrame;
        m_movieStatus.frames = m_movie.frames();
        m_movieStatus.rerecords = m_movie.rerecords;
        m_movieStatus.path = m_moviePath;
    }
    if (changed) m_window.movieChanged();
}

Json GuiHost::movieJson() const
{
    const char* mode = m_movieMode == MovieStatus::Mode::Recording ? "recording"
        : m_movieMode == MovieStatus::Mode::Playing                ? "playing"
                                                                   : "none";
    return {{"mode", mode}, {"readOnly", m_movieMode == MovieStatus::Mode::Playing}, {"movieFrame", m_movieFrame},
        {"frames", m_movie.frames()}, {"rerecords", m_movie.rerecords}, {"path", str(m_moviePath)}};
}

Result GuiHost::movieBusy(bool recording) const
{
    if (m_movieMode == MovieStatus::Mode::Playing) {
        return Result::fail("MoviePlaying", "a movie is playing; stop it, or take over with Read-Only, first");
    }
    if (recording && m_movieMode == MovieStatus::Mode::Recording) {
        return Result::fail("MovieRecording", "a movie is recording; stop it first");
    }
    return Result::ok();
}

Result GuiHost::recordMovie(CallContext& ctx, const QString& path, bool fromPowerOn)
{
    if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
    if (m_movieMode != MovieStatus::Mode::None) stopMovie();
    Machine& machine = ctx.session.machine();
    Movie movie;
    movie.identity = m_identity.sha1;
    movie.core = QString::fromLatin1(coreVersionString());
    movie.region = machine.region() == Region::Pal ? QStringLiteral("pal") : QStringLiteral("ntsc");
    if (fromPowerOn) {
        movie.start = Movie::Start::PowerOn;
    } else {
        movie.start = Movie::Start::State;
        const SaveStateBlob blob = machine.captureState();
        movie.state = QByteArray(reinterpret_cast<const char*>(blob.data), static_cast<qsizetype>(blob.size));
    }
    // Written at once, so a folder that cannot take it is found now.
    QString error;
    if (!movie.write(path, &error)) return Result::fail("WriteError", str(error), {{"path", str(path)}});
    movie.cheats = enabledCheats();
    if (fromPowerOn) {
        Result r = call(ctx, "core.power_on");
        if (!r.isError()) r = call(ctx, "core.resume");
        if (r.isError()) return r;
        // The battery RAM the game starts from (a state carries its own).
        const SramRange sram = machine.sram();
        if (sram.size > 0) movie.battery = QByteArray(reinterpret_cast<const char*>(sram.data), static_cast<qsizetype>(sram.size));
    }
    m_movie = std::move(movie);
    m_moviePath = path;
    m_movieFrame = 0;
    m_movieMode = MovieStatus::Mode::Recording;
    m_rewind.clear(); // the history before the movie is another timeline
    m_rewound.reset();
    publishMovie(true);
    m_window.showMessage(QObject::tr("Recording movie"));
    return Result::ok(movieJson());
}

Result GuiHost::playMovie(CallContext& ctx, const QString& path)
{
    QString error;
    std::optional<Movie> movie = Movie::read(path, &error);
    if (!movie) return invalid("/path", str(error));
    // Refused before anything changes (spec edge case "A movie that does
    // not match the loaded game").
    if (movie->identity != m_identity.sha1) {
        return Result::fail("MovieMismatch", "the movie was recorded on another game",
            {{"movie", str(movie->identity)}, {"game", str(m_identity.sha1)}});
    }
    const QString region = ctx.session.machine().region() == Region::Pal ? QStringLiteral("pal") : QStringLiteral("ntsc");
    if (!movie->region.isEmpty() && movie->region != region) {
        return Result::fail("MovieMismatch",
            "the movie was recorded with " + str(movie->region.toUpper()) + " timing; set Emulation > Region to match",
            {{"movie", str(movie->region)}, {"game", str(region)}});
    }
    if (m_movieMode != MovieStatus::Mode::None) stopMovie();
    // The player's battery RAM stays theirs (#121): kept here, not saved
    // while the movie plays, and put back when it stops.
    Machine& machine = ctx.session.machine();
    const SramRange sram = machine.sram();
    const QByteArray playerBattery(reinterpret_cast<const char*>(sram.data), static_cast<qsizetype>(sram.size));
    Result r = movie->start == Movie::Start::State ? loadStateBytes(ctx, movie->state, false) : call(ctx, "core.power_on");
    if (!r.isError() && movie->battery && machine.sram().size > 0) {
        machine.loadSram(reinterpret_cast<const std::uint8_t*>(movie->battery->constData()),
            static_cast<std::size_t>(movie->battery->size()));
    }
    if (!r.isError()) r = call(ctx, "core.resume");
    if (r.isError()) return r;
    if (!playerBattery.isEmpty()) {
        m_playerBattery = playerBattery;
        m_battery.hold(true);
    }
    m_movieCheats = movie->cheats;
    applyGameGenie(machine);
    m_movie = std::move(*movie);
    m_moviePath = path;
    m_movieFrame = 0;
    m_movieMode = MovieStatus::Mode::Playing;
    m_rewind.clear();
    m_rewound.reset();
    m_rewinding = false;
    updatePlayback();
    publishMovie(true);
    Json v = movieJson();
    if (m_movie.core != QLatin1String(coreVersionString())) {
        // Another core may run the same input differently.
        v["warning"] = "recorded with core " + str(m_movie.core) + "; it may not play back the same";
        m_window.showMessage(QObject::tr("Movie from core %1: it may not play back the same").arg(m_movie.core));
    } else {
        m_window.showMessage(QObject::tr("Playing movie"));
    }
    return Result::ok(v);
}

Result GuiHost::stopMovie(bool finished)
{
    if (m_movieMode == MovieStatus::Mode::None) return Result::fail("NoMovie", "no movie is recording or playing");
    Json v = {{"path", str(m_moviePath)}, {"frames", m_movie.frames()}};
    QString error;
    const bool written = m_movieMode != MovieStatus::Mode::Recording || m_movie.write(m_moviePath, &error);
    if (m_movieMode == MovieStatus::Mode::Recording) {
        m_window.showMessage(written ? QObject::tr("Movie saved (%1 frames)").arg(m_movie.frames())
                                     : QObject::tr("The movie could not be saved"));
    } else {
        m_window.showMessage(finished ? QObject::tr("Movie finished") : QObject::tr("Movie stopped"));
    }
    m_movieMode = MovieStatus::Mode::None;
    m_movie = Movie();
    m_moviePath.clear();
    m_movieFrame = 0;
    endMovieOverrides();
    publishMovie(true);
    if (!written) return Result::fail("WriteError", str(error), {{"path", v["path"]}});
    return Result::ok(v);
}

Result GuiHost::takeOverMovie()
{
    // Read-write from here (spec US8-7): the rest of the movie goes, and
    // the player's input records on from this frame.
    m_movie.truncate(m_movieFrame);
    ++m_movie.rerecords;
    m_movieMode = MovieStatus::Mode::Recording;
    // The player's own cheats from here on, recorded as such. The battery RAM
    // stays the movie's until the movie stops.
    if (m_movieCheats) {
        m_movieCheats.reset();
        if (api::Session* session = m_session.load()) applyGameGenie(session->machine());
        recordCheats();
    }
    QString error;
    if (!m_movie.write(m_moviePath, &error)) return Result::fail("WriteError", str(error), {{"path", str(m_moviePath)}});
    publishMovie(true);
    m_window.showMessage(QObject::tr("Recording from frame %1").arg(m_movieFrame));
    return Result::ok(movieJson());
}

void GuiHost::applyGameGenie(Machine& machine)
{
    std::vector<GameGenieCode> codes;
    for (const Cheat& cheat : activeCheats()) {
        if (!cheat.enabled || cheat.kind != Cheat::Kind::GameGenie) continue;
        if (const auto code = decodeGameGenie(cheat.code.toStdString())) codes.push_back(*code);
    }
    machine.setGameGenieCodes(codes);
}

QVector<Cheat> GuiHost::enabledCheats() const
{
    QVector<Cheat> on;
    for (const Cheat& c : m_cheats.cheats) {
        if (c.enabled) on.push_back(c);
    }
    return on;
}

void GuiHost::recordCheats()
{
    if (m_movieMode != MovieStatus::Mode::Recording) return;
    Movie::Event e;
    e.frame = m_movieFrame;
    e.kind = Movie::Event::Kind::Cheats;
    e.cheats = enabledCheats();
    // Two changes before one frame: the last one stands.
    if (!m_movie.events.isEmpty() && m_movie.events.back().frame == e.frame
        && m_movie.events.back().kind == Movie::Event::Kind::Cheats) {
        m_movie.events.back() = e;
    } else {
        m_movie.events.push_back(e);
    }
}

void GuiHost::endMovieOverrides()
{
    m_movieCheats.reset();
    api::Session* session = m_session.load();
    if (session) applyGameGenie(session->machine());
    if (m_playerBattery) {
        if (session && session->machine().sram().size > 0) {
            session->machine().loadSram(reinterpret_cast<const std::uint8_t*>(m_playerBattery->constData()),
                static_cast<std::size_t>(m_playerBattery->size()));
        }
        m_playerBattery.reset();
        m_battery.hold(false);
    }
}

CheatSearch::Snapshot GuiHost::snapshot(Machine& machine)
{
    // CPU RAM, and PRG-RAM when the cartridge has it (data-model
    // "CheatSearch"). Both are plain memory: reading them has no effect.
    CheatSearch::Snapshot s;
    s.ram.resize(0x800);
    machine.readMemory(0x0000, s.ram.size(), s.ram.data());
    const std::size_t prgRam = std::min<std::size_t>(machine.cartridgeInfo().prgRamSize, 0x2000);
    s.prgRam.resize(prgRam);
    if (prgRam > 0) machine.readMemory(0x6000, prgRam, s.prgRam.data());
    return s;
}

Result GuiHost::saveCheats()
{
    QString error;
    if (!m_cheatsPath.isEmpty() && !m_cheats.write(m_cheatsPath, m_identity, &error)) {
        return Result::fail("WriteError", str(error), {{"path", str(m_cheatsPath)}});
    }
    m_window.cheatsChanged();
    return Result::ok();
}

Json GuiHost::cheatsJson() const
{
    Json list = Json::array();
    for (qsizetype i = 0; i < m_cheats.cheats.size(); ++i) {
        const Cheat& c = m_cheats.cheats[i];
        Json item = {{"index", i}, {"label", str(c.label)}, {"kind", str(kindName(c.kind))}, {"enabled", c.enabled}};
        if (c.kind == Cheat::Kind::GameGenie) {
            item["code"] = str(c.code);
        } else {
            item["address"] = c.address;
            item["value"] = c.value;
        }
        list.push_back(item);
    }
    return list;
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
        m_midFrame = false;
        m_skipBreakpoint.reset();
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

void GuiHost::applyCoreSettings(CallContext& ctx)
{
    call(ctx, "core.set_region_override", {{"region", str(m_settings.emulation().region)}});
    const QString palette = m_settings.video().palette;
    std::optional<std::vector<std::uint32_t>> entries = builtinPalette(palette);
    if (!entries) entries = readPalFile(palette, nullptr); // a file since moved: the default
    call(ctx, "core.set_palette", {{"entries", entries.value_or(std::vector<std::uint32_t>{})}});
}

Result GuiHost::setRegion(CallContext& ctx, const Json& value)
{
    if (value != "auto" && value != "ntsc" && value != "pal") return invalid("/value", "must be auto, ntsc or pal");
    AppSettings::EmulationSettings emulation = m_settings.emulation();
    emulation.region = QString::fromStdString(value.get<std::string>());
    m_settings.setEmulation(emulation);
    call(ctx, "core.set_region_override", {{"region", value}});
    m_window.videoChanged(false); // the menu's check
    // What the next power-on chooses: the override, else the header.
    Machine& machine = ctx.session.machine();
    const Region header = machine.cartridgeInfo().region == Region::Pal ? Region::Pal : Region::Ntsc;
    const Region next = value == "pal" ? Region::Pal : value == "ntsc" ? Region::Ntsc : header;
    const bool pending = m_gameOpen && next != machine.region();
    if (pending) {
        m_window.showMessage(next == Region::Pal ? QObject::tr("PAL timing from the next power cycle")
                                                 : QObject::tr("NTSC timing from the next power cycle"));
    }
    return Result::ok({{"region", machine.region() == Region::Pal ? "PAL" : "NTSC"}, {"pending", pending}});
}

Result GuiHost::setPalette(CallContext& ctx, const QString& name, const QString& path)
{
    std::optional<std::vector<std::uint32_t>> entries;
    if (!path.isEmpty()) {
        if (QDir::isRelativePath(path)) return invalid("/path", "the path must be absolute");
        QString error;
        entries = readPalFile(path, &error);
        if (!entries) return invalid("/path", str(error));
    } else {
        entries = builtinPalette(name);
        if (!entries) return invalid("/name", "must be one of " + str(builtinPalettes().join(QStringLiteral(", "))));
    }
    Result r = call(ctx, "core.set_palette", {{"entries", *entries}});
    if (r.isError()) return r;
    AppSettings::VideoSettings video = m_settings.video();
    video.palette = path.isEmpty() ? name : path;
    m_settings.setVideo(video);
    m_window.videoChanged(false);
    return Result::ok({{"palette", str(video.palette)}});
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

    // Emulation > Region and View > Palette apply to every game opened.
    applyCoreSettings(ctx);
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
    // The game's cheats (FR-051): freezes start with the first frame, and
    // Game Genie codes go to the core. Set cheats write only when switched on.
    m_search = CheatSearch();
    m_cheatsPath = id.sha1.isEmpty() || !library.isUsable()
        ? QString()
        : library.gameFile(Library::Folder::Cheats, id, m_gameName, QStringLiteral(".json"));
    m_cheats = m_cheatsPath.isEmpty() ? CheatList() : CheatList::read(m_cheatsPath);
    applyGameGenie(ctx.session.machine());
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
    if (m_movieMode != MovieStatus::Mode::None) stopMovie();
    if (m_trace) stopTrace(ctx.session.machine());
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
    m_cheats = CheatList();
    m_cheatsPath.clear();
    m_search = CheatSearch();
    ctx.session.machine().setGameGenieCodes({});
    m_rewind.clear();
    m_rewound.reset();
    m_rewinding = false;
    m_pauseAfterRewind = false;
    m_breakpoints.reset();
    m_skipBreakpoint.reset();
    m_midFrame = false;
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
        {"alwaysOnTop", v.alwaysOnTop}, {"pacing", str(v.pacing)}, {"palette", str(v.palette)}, {"showFps", settings.showFps()}, {"showOsd", settings.showOsd()}};
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
    } else if (key == "video.pacing") {
        if (!oneOf({"auto", "display", "audio"})) return invalid("/value", "must be auto, display or audio");
        v.pacing = QString::fromStdString(value.get<std::string>());
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
        true, true, [this](CallContext& ctx, const Json&) {
            if (Result busy = movieBusy(false); busy.isError()) return busy;
            Result r = plain(call(ctx, "core.reset"));
            if (!r.isError() && m_movieMode == MovieStatus::Mode::Recording) {
                m_movie.events.push_back({m_movieFrame, Movie::Event::Kind::Reset, {}});
            }
            return r;
        });

    add(registry, "gui.power_cycle", "Turn the console off and on again", s::object({}),
        s::object({}), true, true, [this](CallContext& ctx, const Json&) {
            if (Result busy = movieBusy(false); busy.isError()) return busy;
            const bool running = isRunning(ctx);
            Result r = call(ctx, "core.power_on");
            // Power-on pauses the machine; a game being played keeps playing.
            if (!r.isError() && running) r = call(ctx, "core.resume");
            if (!r.isError() && m_movieMode == MovieStatus::Mode::Recording) {
                m_movie.events.push_back({m_movieFrame, Movie::Event::Kind::Power, {}});
            }
            return plain(r);
        });

    add(registry, "gui.save_state", "Save the game's state into a slot (replacing what it holds), with a thumbnail",
        s::object({{"slot", slotSchema()}, {"name", nameSchema()}}, {"slot"}), slotInfoSchema(), true, true,
        [this](CallContext& ctx, const Json& args) {
            return saveState(ctx, args["slot"].get<int>(), stateName(args));
        });

    add(registry, "gui.load_state", "Load a slot (Undo Load State brings back what it replaced)",
        s::object({{"slot", slotSchema()}}, {"slot"}), s::object({{"slot", slotSchema()}}), true, true,
        [this](CallContext& ctx, const Json& args) {
            if (Result busy = movieBusy(true); busy.isError()) return busy;
            return loadState(ctx, args["slot"].get<int>());
        });

    add(registry, "gui.quick_save", "Save into the selected slot (replacing what it holds)",
        s::object({{"name", nameSchema()}}), slotInfoSchema(), true, true,
        [this](CallContext& ctx, const Json& args) { return saveState(ctx, m_selectedSlot.load(), stateName(args)); });

    add(registry, "gui.quick_load", "Load the selected slot", s::object({}), s::object({{"slot", slotSchema()}}),
        true, true, [this](CallContext& ctx, const Json&) {
            if (Result busy = movieBusy(true); busy.isError()) return busy;
            return loadState(ctx, m_selectedSlot.load());
        });

    add(registry, "gui.select_slot", "Choose the slot Quick Save and Quick Load use (per game)",
        s::object({{"slot", slotSchema()}}, {"slot"}), s::object({{"slot", slotSchema()}}), true, false,
        [this](CallContext&, const Json& args) { return selectSlot(args["slot"].get<int>()); });

    add(registry, "gui.undo_load_state", "Go back to the state the last load replaced (one level)",
        s::object({}), s::object({}), true, true, [this](CallContext& ctx, const Json&) {
            if (Result busy = movieBusy(true); busy.isError()) return busy;
            if (!m_undoState) return Result::fail("NotFound", "no state load to undo");
            const QByteArray state = *m_undoState;
            Result r = loadStateBytes(ctx, state, false);
            if (r.isError()) return r;
            m_undoState.reset();
            m_window.showMessage(QObject::tr("Load undone"));
            return Result::ok();
        });

    add(registry, "gui.set_region",
        "Force NTSC or PAL timing, or follow the header (auto); a running game changes at its next power cycle",
        s::object({{"region", s::stringEnum({"auto", "ntsc", "pal"})}}, {"region"}),
        s::object({{"region", s::string("the region in effect")}, {"pending", s::boolean("changes at the power cycle")}},
            {"region", "pending"}),
        true, false, [this](CallContext& ctx, const Json& args) { return setRegion(ctx, args["region"]); });

    add(registry, "gui.set_palette",
        "The picture's colours: a built-in palette (default, rgb), or a .pal file; with neither, a file dialog",
        s::object({{"name", s::stringEnum({"default", "rgb"})}, {"path", s::string("a .pal file (192 or 1536 bytes)")}}),
        s::object({{"palette", s::string()}, {"dialog", s::boolean()}}), true, false,
        [this](CallContext& ctx, const Json& args) {
            if (args.contains("name") && args.contains("path")) return invalid("", "give name or path, not both");
            if (!args.contains("name") && !args.contains("path")) {
                m_window.showFileDialog(Window::FileDialog::Palette);
                return Result::ok({{"dialog", true}});
            }
            return setPalette(ctx, QString::fromStdString(args.value("name", "")),
                QString::fromStdString(args.value("path", "")));
        });

    // --- Cheats (FR-050/051/053, data-model "Cheat", "CheatSearch") ---------
    add(registry, "gui.show_cheats", "Open the cheats window", s::object({}), s::object({}), false, true,
        [this](CallContext&, const Json&) {
            m_window.showCheats(false);
            return Result::ok();
        });
    add(registry, "gui.show_cheat_search", "Open the cheat search (the game keeps running)", s::object({}),
        s::object({}), false, true, [this](CallContext&, const Json&) {
            m_window.showCheats(true);
            return Result::ok();
        });

    const Json cheatOut = s::object({{"index", s::integer(0, 9999)}, {"label", s::string()},
        {"kind", s::stringEnum({"freeze", "set", "game_genie"})}, {"address", s::u16()},
        {"value", s::integer(0, 255)}, {"code", s::string()}, {"enabled", s::boolean()}},
        {"index", "label", "kind", "enabled"});
    add(registry, "gui.list_cheats", "The game's cheats", s::object({}),
        s::object({{"cheats", s::array(cheatOut, 10000, "in order")}}, {"cheats"}), false, true,
        [this](CallContext&, const Json&) { return Result::ok({{"cheats", cheatsJson()}}); });

    add(registry, "gui.add_cheat",
        "Add a cheat: freeze (written before every frame) or set (written once) an address to a value, or a "
        "6- or 8-letter Game Genie code; saved with the game",
        s::object({{"kind", s::stringEnum({"freeze", "set", "game_genie"})}, {"address", s::u16()},
                      {"value", s::integer(0, 255)}, {"code", s::string("Game Genie letters")},
                      {"label", s::string()}, {"enabled", s::boolean("default true")}},
            {"kind"}),
        s::object({{"index", s::integer(0, 9999)}}, {"index"}), true, true,
        [this](CallContext& ctx, const Json& args) {
            Cheat cheat;
            cheat.kind = *kindFromName(QString::fromStdString(args["kind"].get<std::string>()));
            cheat.label = QString::fromStdString(args.value("label", ""));
            cheat.enabled = args.value("enabled", true);
            if (cheat.kind == Cheat::Kind::GameGenie) {
                const std::string code = args.value("code", "");
                if (!decodeGameGenie(code)) {
                    return invalid("/code", "a Game Genie code is 6 or 8 of the letters APZLGITYEOXUKSVN");
                }
                cheat.code = QString::fromStdString(code).toUpper();
            } else {
                if (!args.contains("address") || !args.contains("value")) {
                    return invalid("", "freeze and set need an address and a value");
                }
                cheat.address = args["address"].get<std::uint16_t>();
                cheat.value = static_cast<std::uint8_t>(args["value"].get<int>());
            }
            m_cheats.cheats.push_back(cheat);
            Machine& machine = ctx.session.machine();
            // Switched on, freeze and set write at once; freeze then before every frame.
            // Not while a movie's own cheats are in effect: they decide (#121).
            if (cheat.enabled && cheat.kind != Cheat::Kind::GameGenie && !m_movieCheats) {
                machine.writeMemory(cheat.address, &cheat.value, 1);
            }
            applyGameGenie(machine);
            if (cheat.enabled) recordCheats();
            Result saved = saveCheats();
            if (saved.isError()) return saved;
            return Result::ok({{"index", m_cheats.cheats.size() - 1}});
        });

    const Json byIndex = s::object({{"index", s::integer(0, 9999)}}, {"index"});
    add(registry, "gui.remove_cheat", "Remove a cheat", byIndex, s::object({}), true, true,
        [this](CallContext& ctx, const Json& args) {
            const int i = args["index"].get<int>();
            if (i >= m_cheats.cheats.size()) return invalid("/index", "there is no such cheat");
            const bool wasOn = m_cheats.cheats[i].enabled;
            m_cheats.cheats.removeAt(i);
            applyGameGenie(ctx.session.machine());
            if (wasOn) recordCheats();
            return saveCheats();
        });

    add(registry, "gui.set_cheat_enabled", "Switch a cheat on (set cheats write their value then) or off",
        s::object({{"index", s::integer(0, 9999)}, {"enabled", s::boolean()}}, {"index", "enabled"}), s::object({}),
        true, true, [this](CallContext& ctx, const Json& args) {
            const int i = args["index"].get<int>();
            if (i >= m_cheats.cheats.size()) return invalid("/index", "there is no such cheat");
            Cheat& cheat = m_cheats.cheats[i];
            const bool on = args["enabled"].get<bool>();
            Machine& machine = ctx.session.machine();
            const bool changed = on != cheat.enabled;
            if (on && !cheat.enabled && cheat.kind != Cheat::Kind::GameGenie && !m_movieCheats) {
                machine.writeMemory(cheat.address, &cheat.value, 1);
            }
            cheat.enabled = on;
            applyGameGenie(machine);
            if (changed) recordCheats();
            return saveCheats();
        });

    add(registry, "gui.cheat_search_reset",
        "Start a cheat search over CPU RAM and PRG-RAM, with 1- or 2-byte (little-endian) values",
        s::object({{"size", s::integer(1, 2, "bytes (1)")}}), s::object({{"candidates", s::u64()}}, {"candidates"}),
        false, true, [this](CallContext& ctx, const Json& args) {
            m_search.reset(snapshot(ctx.session.machine()), args.value("size", 1));
            return Result::ok({{"candidates", m_search.count()}});
        });

    add(registry, "gui.cheat_search_filter",
        "Keep the addresses whose value now is equal/not_equal/greater/less than `value` (or the previous "
        "snapshot without one), or changed/unchanged since it; snapshots are taken at frame boundaries",
        s::object({{"filter", s::stringEnum({"equal", "not_equal", "greater", "less", "changed", "unchanged"})},
                      {"value", s::integer(0, 65535)}},
            {"filter"}),
        s::object({{"candidates", s::u64()}}, {"candidates"}), false, true,
        [this](CallContext& ctx, const Json& args) {
            if (!m_search.active()) m_search.reset(snapshot(ctx.session.machine()), 1);
            const std::string name = args["filter"];
            const CheatSearch::Filter filter = name == "equal" ? CheatSearch::Filter::Equal
                : name == "not_equal"                          ? CheatSearch::Filter::NotEqual
                : name == "greater"                            ? CheatSearch::Filter::Greater
                : name == "less"                               ? CheatSearch::Filter::Less
                : name == "changed"                            ? CheatSearch::Filter::Changed
                                                               : CheatSearch::Filter::Unchanged;
            std::optional<std::uint32_t> value;
            if (args.contains("value")) value = args["value"].get<std::uint32_t>();
            m_search.filter(filter, value, snapshot(ctx.session.machine()));
            return Result::ok({{"candidates", m_search.count()}});
        });

    add(registry, "gui.cheat_search_results", "The addresses left, with their values now and at the last filter",
        s::object({{"max", s::integer(1, 1000, "default 100")}}),
        s::object({{"candidates", s::u64()}, {"size", s::integer(0, 2)},
                      {"results", s::array(s::object({{"address", s::u16()}, {"value", s::u64()},
                                                          {"previous", s::u64()}}),
                                      1000, "in address order")}},
            {"candidates", "size", "results"}),
        false, true, [this](CallContext& ctx, const Json& args) {
            Json results = Json::array();
            const auto now = snapshot(ctx.session.machine());
            const auto max = static_cast<std::size_t>(args.value("max", 100));
            for (const CheatSearch::Candidate& c : m_search.results(now, max)) {
                results.push_back({{"address", c.address}, {"value", c.value}, {"previous", c.previous}});
            }
            return Result::ok({{"candidates", m_search.count()}, {"size", m_search.size()}, {"results", results}});
        });

    add(registry, "gui.add_watch", "Keep an address in the game's watch list, with a label",
        s::object({{"address", s::u16()}, {"size", s::integer(1, 2, "bytes (1)")}, {"label", s::string()}},
            {"address"}),
        s::object({{"index", s::integer(0, 9999)}}, {"index"}), true, true,
        [this](CallContext&, const Json& args) {
            m_cheats.watches.push_back({QString::fromStdString(args.value("label", "")),
                args["address"].get<std::uint16_t>(), args.value("size", 1)});
            Result saved = saveCheats();
            if (saved.isError()) return saved;
            return Result::ok({{"index", m_cheats.watches.size() - 1}});
        });

    add(registry, "gui.remove_watch", "Remove an address from the watch list", byIndex, s::object({}), true, true,
        [this](CallContext&, const Json& args) {
            const int i = args["index"].get<int>();
            if (i >= m_cheats.watches.size()) return invalid("/index", "there is no such watch");
            m_cheats.watches.removeAt(i);
            return saveCheats();
        });

    add(registry, "gui.list_watches", "The watch list with live values", s::object({}),
        s::object({{"watches", s::array(s::object({{"index", s::integer(0, 9999)}, {"label", s::string()},
                                                       {"address", s::u16()}, {"size", s::integer(1, 2)},
                                                       {"value", s::u64()}}),
                                   10000, "in order")}},
            {"watches"}),
        false, true, [this](CallContext& ctx, const Json&) {
            Json list = Json::array();
            Machine& machine = ctx.session.machine();
            for (qsizetype i = 0; i < m_cheats.watches.size(); ++i) {
                const Watch& w = m_cheats.watches[i];
                std::uint8_t bytes[2] = {};
                for (int b = 0; b < w.size; ++b) bytes[b] = machine.peek(static_cast<std::uint16_t>(w.address + b));
                list.push_back({{"index", i}, {"label", str(w.label)}, {"address", w.address}, {"size", w.size},
                    {"value", bytes[0] | (w.size == 2 ? bytes[1] << 8 : 0)}});
            }
            return Result::ok({{"watches", list}});
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
            if (Result busy = movieBusy(true); busy.isError()) return busy;
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
        [this](CallContext& ctx, const Json& args) {
            const bool on = args["on"].get<bool>();
            if (Result busy = movieBusy(true); on && busy.isError()) return busy;
            return setRewind(ctx, on);
        });

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
            if (!m_rewinding) {
                runFrame(machine); // as in play: rewind history, cheats and the movie
                return Result::ok();
            }
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

    const Json none = s::object({});
    const Json fileArgs = s::object({{"path", s::string("a file path; without it, a file dialog")}});
    const Json movieStatusSchema = s::object(
        {{"mode", s::stringEnum({"none", "recording", "playing"})}, {"readOnly", s::boolean("playing: input is the movie's")},
            {"movieFrame", s::u64("frames recorded or played so far (the envelope's frame is the core's)")}, {"frames", s::u64("in the movie")},
            {"rerecords", s::u64()}, {"path", s::string()}, {"dialog", s::boolean("the file dialog was shown instead")},
            {"warning", s::string("recorded by another core version")}});
    add(registry, "gui.record_movie",
        "Record a movie (.cnm): each frame's input from power-on (a power cycle first) or from the current state; "
        "without path, show the file dialog",
        s::object({{"path", s::string("an absolute .cnm path")}, {"from", s::stringEnum({"power_on", "state"})}},
            {"from"}),
        movieStatusSchema, true, true, [this](CallContext& ctx, const Json& args) {
            const bool fromPowerOn = args["from"] == "power_on";
            if (!args.contains("path")) {
                const Library library = m_window.library();
                const QString folder = m_identity.sha1.isEmpty() || !library.isUsable()
                    ? QString()
                    : library.gameFile(Library::Folder::Movies, m_identity, m_gameName, {});
                m_window.showMovieDialog(true, fromPowerOn ? QStringLiteral("power_on") : QStringLiteral("state"), folder);
                return Result::ok({{"dialog", true}});
            }
            return recordMovie(ctx, QString::fromStdString(args["path"].get<std::string>()), fromPowerOn);
        });
    add(registry, "gui.play_movie",
        "Play a movie read-only (refused for another game); it pauses on its last frame. Without path, show the "
        "file dialog",
        fileArgs, movieStatusSchema, true, true, [this](CallContext& ctx, const Json& args) {
            if (!args.contains("path")) {
                const Library library = m_window.library();
                const QString folder = m_identity.sha1.isEmpty() || !library.isUsable()
                    ? QString()
                    : library.gameFile(Library::Folder::Movies, m_identity, m_gameName, {});
                m_window.showMovieDialog(false, {}, folder);
                return Result::ok({{"dialog", true}});
            }
            return playMovie(ctx, QString::fromStdString(args["path"].get<std::string>()));
        });
    add(registry, "gui.stop_movie", "Stop recording (writing the file) or playing the movie", none,
        s::object({{"path", s::string()}, {"frames", s::u64()}}, {"path", "frames"}), true, true,
        [this](CallContext&, const Json&) { return stopMovie(); });
    add(registry, "gui.toggle_movie_read_only",
        "Take over a playing movie: the rest of it is dropped and the player's input records on from this frame",
        none, movieStatusSchema, true, true, [this](CallContext&, const Json&) {
            if (m_movieMode == MovieStatus::Mode::None) return Result::fail("NoMovie", "no movie is recording or playing");
            if (m_movieMode == MovieStatus::Mode::Recording) {
                return Result::fail("MovieRecording", "the movie is recording already; stop it and play it to watch it");
            }
            return takeOverMovie();
        });
    add(registry, "gui.movie_status", "The movie recording or playing, if any", none, movieStatusSchema, false,
        false, [this](CallContext&, const Json&) { return Result::ok(movieJson()); });
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
    add(registry, "gui.show_debugger", "Open the debugger: registers, flags, disassembly, stepping and breakpoints",
        none, s::object({}), false, true, [this](CallContext&, const Json&) {
            m_window.showDebugger();
            return Result::ok();
        });
    add(registry, "gui.debug_step_instruction", "Pause and run CPU instructions (default 1)",
        s::object({{"count", s::integer(1, 100000, "default 1")}}), s::object({{"cpu", Json{{"type", "object"}}}}, {"cpu"}),
        true, true, [this](CallContext& ctx, const Json& args) { return stepInstructions(ctx, args.value("count", 1)); });
    add(registry, "gui.debug_step_frame",
        "Pause and run to the end of the frame; a breakpoint on the way stops it (breakpoint: its address)",
        none, s::object({{"cpu", Json{{"type", "object"}}}, {"breakpoint", s::u16()}}, {"cpu"}), true, true,
        [this](CallContext& ctx, const Json&) {
            if (isRunning(ctx)) {
                Result r = call(ctx, "core.pause");
                if (r.isError()) return r;
            }
            runFrame(ctx.session.machine());
            Json v = {{"cpu", call(ctx, "core.get_cpu").value["cpu"]}};
            if (m_midFrame && m_skipBreakpoint) v["breakpoint"] = *m_skipBreakpoint;
            return Result::ok(v);
        });
    add(registry, "gui.debug_run", "Run from here; a breakpoint at PC does not stop it again", none, runStateOutput(),
        true, true, [](CallContext& ctx, const Json&) { return runStateResult(call(ctx, "core.resume")); });
    const auto breakpointList = [this] {
        Json list = Json::array();
        for (std::size_t a = 0; a < m_breakpoints.size(); ++a) {
            if (m_breakpoints.test(a)) list.push_back(a);
        }
        return list;
    };
    const Json breakpointsSchema = s::object({{"breakpoints", s::array(s::u16(), 65536, "in address order")}}, {"breakpoints"});
    add(registry, "gui.add_breakpoint", "Stop before the instruction at an address (execution breakpoint)",
        s::object({{"address", s::u16()}}, {"address"}), breakpointsSchema, true, true,
        [this, breakpointList](CallContext&, const Json& args) {
            m_breakpoints.set(args["address"].get<std::size_t>());
            return Result::ok({{"breakpoints", breakpointList()}});
        });
    add(registry, "gui.remove_breakpoint", "Remove the breakpoint at an address",
        s::object({{"address", s::u16()}}, {"address"}), breakpointsSchema, true, true,
        [this, breakpointList](CallContext&, const Json& args) {
            const auto address = args["address"].get<std::size_t>();
            if (!m_breakpoints.test(address)) return invalid("/address", "no breakpoint there");
            m_breakpoints.reset(address);
            return Result::ok({{"breakpoints", breakpointList()}});
        });
    add(registry, "gui.list_breakpoints", "The execution breakpoints", none, breakpointsSchema, false, false,
        [breakpointList](CallContext&, const Json&) { return Result::ok({{"breakpoints", breakpointList()}}); });
    add(registry, "gui.show_memory_viewer",
        "Open the memory viewer: CPU memory, PPU memory, OAM and palette RAM in hex, editable; viewing has no side "
        "effects",
        none, s::object({}), false, true, [this](CallContext&, const Json&) {
            m_window.showMemoryViewer();
            return Result::ok();
        });
    add(registry, "gui.show_ppu_viewer",
        "Open the PPU viewer at a view: pattern tables, nametables with the scroll, sprites, or palette RAM",
        s::object({{"view", s::stringEnum({"patterns", "nametables", "sprites", "palette"})}}), s::object({}), false,
        true, [this](CallContext&, const Json& args) {
            m_window.showPpuViewer(QString::fromStdString(args.value("view", std::string("patterns"))));
            return Result::ok();
        });
    add(registry, "gui.show_trace_logger", "Open the trace logger: start and stop logging every instruction to a file",
        none, s::object({}), false, true, [this](CallContext&, const Json&) {
            m_window.showTraceLogger();
            return Result::ok();
        });
    const Json traceSchema = s::object({{"path", s::string()}, {"lines", s::u64("instructions logged")}}, {"path", "lines"});
    add(registry, "gui.trace_start",
        "Log every instruction the CPU runs, in nestest.log's format, to a file (without path, "
        "trace-<YYYYMMDD-HHMMSS>.log in the library's debug folder) until gui.trace_stop or the game closes",
        s::object({{"path", s::string("an absolute file path")}}), s::object({{"path", s::string()}}, {"path"}), true,
        true, [this](CallContext& ctx, const Json& args) {
            return startTrace(ctx.session.machine(), QString::fromStdString(args.value("path", std::string())));
        });
    add(registry, "gui.trace_stop", "Stop the trace logger and finish its file", none, traceSchema, true, false,
        [this](CallContext& ctx, const Json&) { return stopTrace(ctx.session.machine()); });
    add(registry, "gui.trace_status", "Whether the trace logger is logging, to which file, and how many instructions so far",
        none, s::object({{"active", s::boolean()}, {"path", s::string()}, {"lines", s::u64()}}, {"active"}), false,
        false, [this](CallContext&, const Json&) {
            if (!m_trace) return Result::ok({{"active", false}});
            return Result::ok({{"active", true}, {"path", str(m_tracePath)}, {"lines", m_traceLines}});
        });
    add(registry, "gui.write_memory_dump",
        "Write the core's memory dump (<YYYYMMDDHHMMSS>.dmp, 001 core-api.md \"Memory dump\") into the library's debug "
        "folder",
        none, s::object({{"path", s::string()}}, {"path"}), false, true, [this](CallContext& ctx, const Json&) {
            const Library library = m_window.library();
            if (!library.isUsable()) return Result::fail("WriteError", "the library cannot be written; see Settings > Library");
            const QDir folder(library.folder(Library::Folder::Debug));
            if (!folder.mkpath(QStringLiteral("."))) return Result::fail("WriteError", "cannot create " + str(folder.path()));
            // The core's dumpMemory names and writes the file itself, but
            // through a std::string path, which Windows reads in the ANSI
            // code page; Qt writes any path. Same name, same bytes.
            const QDateTime now = QDateTime::currentDateTime();
            const QString stamp = now.toString(QStringLiteral("yyyyMMddHHmmss"));
            QString path = folder.filePath(stamp + QStringLiteral(".dmp"));
            for (int n = 2; QFile::exists(path); ++n) path = folder.filePath(QStringLiteral("%1-%2.dmp").arg(stamp).arg(n));
            const std::vector<std::uint8_t> bytes = ctx.session.machine().memoryDump(static_cast<std::uint64_t>(now.toSecsSinceEpoch()));
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)
                || file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()))
                    != static_cast<qint64>(bytes.size())
                || !file.commit()) {
                return Result::fail("WriteError", "cannot write " + str(path), {{"path", str(path)}});
            }
            m_window.showMessage(QObject::tr("Memory dump written: %1").arg(QFileInfo(path).fileName()));
            return Result::ok({{"path", str(path)}});
        });
    const Json windowNames = s::stringEnum({"about", "settings", "cheats", "cheat_search", "debugger", "memory_viewer",
        "ppu_viewer", "trace_logger", "user_guide"});
    const Json windowList = s::array(windowNames, 16, "in name order");
    add(registry, "gui.list_windows", "The windows a gui.show_* opened that are open now", none,
        s::object({{"windows", windowList}}, {"windows"}), false, false, [this](CallContext&, const Json&) {
            Json list = Json::array();
            for (const QString& name : m_window.openWindows()) list.push_back(str(name));
            return Result::ok({{"windows", list}});
        });
    Json closable = windowNames;
    closable["enum"].push_back("all");
    add(registry, "gui.close_window",
        "Close a window a gui.show_* opened (all: every one); returns the ones it closes, which gui.list_windows "
        "drops once they have closed",
        s::object({{"window", closable}}, {"window"}), s::object({{"closed", windowList}}, {"closed"}), false, false,
        [this](CallContext&, const Json& args) {
            const QString window = QString::fromStdString(args["window"].get<std::string>());
            const QStringList open = m_window.openWindows();
            if (window != QLatin1String("all") && !open.contains(window)) return invalid("/window", "that window is not open");
            const QStringList closing = window == QLatin1String("all") ? open : QStringList{window};
            m_window.closeWindows(closing);
            Json list = Json::array();
            for (const QString& name : closing) list.push_back(str(name));
            return Result::ok({{"closed", list}});
        });
    add(registry, "gui.state",
        "The window's state: the page shown, whether a game is open, fullscreen, scale, speed, fast-forward, "
        "rewind, the movie, the selected slot and the open windows",
        none,
        s::object({{"page", s::stringEnum({"library", "game", "key_bindings"})}, {"game", s::boolean("a game is open")},
                      {"fullscreen", s::boolean()}, {"scale", s::integer(1, 6)},
                      {"speed", s::integer(0, 400, "percent; 0 is Unlimited")}, {"fastForward", s::boolean()},
                      {"rewinding", s::boolean()}, {"movie", Json{{"type", "object"}}},
                      {"selectedSlot", s::integer(1, 10)}, {"windows", windowList}},
            {"page", "game", "fullscreen", "scale", "speed", "fastForward", "rewinding", "movie", "selectedSlot",
                "windows"}),
        false, false, [this](CallContext&, const Json&) {
            Json windows = Json::array();
            for (const QString& name : m_window.openWindows()) windows.push_back(str(name));
            const auto video = m_settings.video();
            Json movie = movieJson();
            return Result::ok({{"page", str(m_window.currentPage())}, {"game", m_gameOpen}, {"fullscreen", video.fullscreen},
                {"scale", video.scale}, {"speed", m_speed}, {"fastForward", m_fastForward}, {"rewinding", m_rewinding},
                {"movie", movie}, {"selectedSlot", m_selectedSlot.load()}, {"windows", windows}});
        });
    add(registry, "gui.show_user_guide", "Show the user guide (docs/user-guide.md, built in)", none,
        s::object({}), false, false, [this](CallContext&, const Json&) {
            m_window.showUserGuide();
            return Result::ok();
        });
    add(registry, "gui.open_log_folder", "Show the log folder (cutenes.log, cutenes.previous.log) in the file manager",
        s::object({}), s::object({{"path", s::string()}}, {"path"}), false, false, [this](CallContext&, const Json&) {
            return Result::ok({{"path", str(QDir::toNativeSeparators(m_window.openLogFolder()))}});
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
        s::object({{"group", s::stringEnum({"video", "audio", "emulation", "library", "servers", "hotkeys", "input", "updates"})}}),
        s::object({{"values", Json{{"type", "object"}}}, {"schema", Json{{"type", "object"}}}}, {"values", "schema"}),
        false, false, [this, edge](CallContext&, const Json& args) {
            const Library library = m_window.library();
            const AppSettings::ServerSettings servers = m_settings.servers();
            const AppSettings::UpdateSettings updates = m_settings.updates();
            const AppSettings::InputSettings input = m_settings.input();
            const AppSettings::AudioSettings audio = m_settings.audio();
            const AppSettings::EmulationSettings emulation = m_settings.emulation();
            const Window::PacingNow pacing = m_window.pacingNow();
            Json video = videoJson(m_settings.video(), m_settings);
            video["pacingNow"] = {{"mode", pacing.display ? "display" : "audio"}, {"divisor", pacing.divisor},
                {"displayHz", pacing.displayHz}, {"framesPerSecond", pacing.framesPerSecond}};
            const Json values = {{"video", video},
                {"audio", {{"device", str(audio.device)}, {"volume", audio.volume}, {"muted", audio.muted},
                              {"latencyMs", audio.latencyMs}, {"fastForwardAudio", str(audio.fastForwardAudio)}}},
                {"emulation", {{"pauseWhenInactive", emulation.pauseWhenInactive},
                                  {"resumeOnOpen", emulation.resumeOnOpen},
                                  {"fastForwardSpeed", emulation.fastForwardSpeed},
                                  {"region", str(emulation.region)}}},
                {"library", {{"root", str(library.root())},
                                     {"roms", str(library.folder(Library::Folder::Roms))},
                                     {"romFolders", romFoldersValue(m_settings)}}},
                {"servers", {{"apiEnabled", servers.apiEnabled}, {"apiPort", servers.apiPort},
                                {"mcpEnabled", servers.mcpEnabled}, {"mcpPort", servers.mcpPort}}},
                {"updates", {{"check", updates.check}, {"asked", updates.asked},
                                {"skippedVersion", str(updates.skippedVersion)}}},
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
                {"video.pacing", s::stringEnum({"auto", "display", "audio"})},
                {"video.palette", s::string("a built-in palette (default, rgb) or a .pal file's path; gui.set_palette")},
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
                {"emulation.region", s::stringEnum({"auto", "ntsc", "pal"})},
                {"library.root", s::string("absolute path of the library folder")},
                {"library.roms", s::string("the library's own roms folder (always scanned)")},
                {"library.romFolders", s::array(s::string("added with gui.add_rom_folder"), 1000)},
                {"servers.apiEnabled", s::boolean("the API listener (off by default; off turns MCP off)")},
                {"servers.apiPort", s::integer(0, 65535, "the API port; 0 picks a free one")},
                {"servers.mcpEnabled", s::boolean("the MCP server (off by default; on turns the API on)")},
                {"servers.mcpPort", s::integer(0, 65535, "the MCP port; 0 picks a free one")},
                {"updates.check", s::boolean("check GitHub Releases once per launch (off until the user says yes; R16)")},
                {"updates.asked", s::boolean("the first-launch question has been answered")},
                {"updates.skippedVersion", s::string("a version Skip This Version declined (MAJOR.MINOR.PATCH), or empty")},
                {"hotkeys", s::string("the menu bindings that differ from the defaults, by id (gui.set_binding)")},
                {"input.turboRate", s::integer(2, 30, "turbo presses a second (15 by default)")},
                {"input.allowOppositeDirections", s::boolean("Left with Right and Up with Down (off: the last pressed wins)")},
                {"input.pauseOnGamepadDisconnect", s::boolean("pause when a gamepad in use is unplugged (on by default)")},
                {"input.bindings", s::string("the game-button bindings that differ from the defaults, by id (gui.set_binding)")}};
            if (!args.contains("group")) return Result::ok({{"values", values}, {"schema", schema}});
            // One group: its values, and the schema keys under it ("video.scale"), or
            // the group's own key for a one-key group ("hotkeys").
            const std::string group = args["group"].get<std::string>();
            Json groupSchema = Json::object();
            for (const auto& [key, value] : schema.items()) {
                if (key == group || key.rfind(group + ".", 0) == 0) groupSchema[key] = value;
            }
            return Result::ok({{"values", {{group, values[group]}}}, {"schema", groupSchema}});
        });

    add(registry, "gui.set_setting", "Validate and apply a setting (gui.get_settings lists them)",
        s::object({{"key", s::stringEnum(kSettingKeys)}, {"value", Json::object()}}, {"key", "value"}),
        s::object({{"key", s::string()}, {"value", Json::object()}}, {"key", "value"}), true, false,
        [this](CallContext& ctx, const Json& args) {
            const std::string key = args["key"];
            if (key == "video.palette") {
                if (!args["value"].is_string()) return invalid("/value", "must be a palette name or a .pal path");
                const QString value = QString::fromStdString(args["value"].get<std::string>());
                return setPalette(ctx, builtinPalette(value) ? value : QString(), builtinPalette(value) ? QString() : value);
            }
            if (key == "emulation.region") return setRegion(ctx, args["value"]);
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
            if (key.rfind("updates.", 0) == 0) {
                // Only the settings: a check follows at the next launch, and
                // nothing here installs (FR-097).
                AppSettings::UpdateSettings updates = m_settings.updates();
                if (key == "updates.skippedVersion") {
                    if (!value.is_string()) return invalid("/value", "must be a version (1.2.0) or empty");
                    const QString version = QString::fromStdString(value.get<std::string>());
                    if (!version.isEmpty() && !update::Version::parse(version)) {
                        return invalid("/value", "must be a version (1.2.0) or empty");
                    }
                    updates.skippedVersion = version.isEmpty() ? QString() : update::Version::parse(version)->toString();
                } else {
                    if (!value.is_boolean()) return invalid("/value", "must be true or false");
                    (key == "updates.check" ? updates.check : updates.asked) = value.get<bool>();
                }
                m_settings.setUpdates(updates);
                m_window.updatesChanged();
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
