// G070 movies, G072 (spec US8-6/7, FR-052, data-model "Movie",
// library-layout "Movie file (.cnm)").
// The test ROM reads controller 1 every frame and adds the byte it read to a
// running sum at $20, which it writes (AND $3F) to the backdrop colour. The
// picture, and so the frame hash, depends on every frame's input since the
// start: a replay reaches the recording's last hash only if each frame got
// the same buttons. Expectations follow from that program and the file
// format, not from the emulator.
// - A movie from power-on, and one from a state, replay to the recording's
//   final hash, whatever the player presses meanwhile (read-only).
// - A movie for a different game is refused before playback starts.
// - Taking over a read-only playback truncates the movie at that frame and
//   records on from there, counting a rerecord.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QAction>
#include <QFile>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <cstring>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

// NROM-128: the program above at $C000 (reset vector), NMI and rendering
// off, so the backdrop ($3F00) fills the picture. `variant` changes an
// unused byte, which makes a different game (another SHA-1).
QByteArray inputSumRom(char variant = 0)
{
    QByteArray rom(16 + 16384 + 8192, '\0');
    const char header[] = {'N', 'E', 'S', 0x1A, 1, 1};
    std::memcpy(rom.data(), header, sizeof header);
    const unsigned char program[] = {
        0xA9, 0x00, 0x85, 0x20,       // LDA #0; STA $20 (the sum)
        0x2C, 0x02, 0x20,             // $C004 wait: BIT $2002 (vblank)
        0x10, 0xFB,                   // BPL wait
        0xA9, 0x01, 0x8D, 0x16, 0x40, // strobe controller 1
        0xA9, 0x00, 0x8D, 0x16, 0x40,
        0xA2, 0x08,                   // LDX #8
        0xA9, 0x00, 0x85, 0x21,       // $21 = 0
        0xAD, 0x16, 0x40,             // read: LDA $4016
        0x4A, 0x26, 0x21,             // LSR A; ROL $21
        0xCA, 0xD0, 0xF7,             // DEX; BNE read
        0xA5, 0x21, 0x18, 0x65, 0x20, // $20 += $21
        0x85, 0x20,
        0xA9, 0x3F, 0x8D, 0x06, 0x20, // PPUADDR = $3F00
        0xA9, 0x00, 0x8D, 0x06, 0x20,
        0xA5, 0x20, 0x29, 0x3F,       // PPUDATA = $20 & $3F
        0x8D, 0x07, 0x20,
        0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR = $0000
        0x8D, 0x06, 0x20,
        0x4C, 0x04, 0xC0,             // JMP wait
    };
    std::memcpy(rom.data() + 16, program, sizeof program);
    rom[16 + 0x3000] = variant;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = static_cast<char>(0xC0);
    return rom;
}

// #121: like inputSumRom, with battery RAM, and two more terms. The sum
// starts from the battery RAM's first byte ($6000), and every frame adds
// $22, which nothing but a freeze cheat sets. So the final sum and picture
// depend on the battery RAM at power-on and on the cheats, as well as on the
// input.
QByteArray batterySumRom()
{
    QByteArray rom(16 + 16384 + 8192, '\0');
    const char header[] = {'N', 'E', 'S', 0x1A, 1, 1, 0x02}; // flags 6: battery
    std::memcpy(rom.data(), header, sizeof header);
    const unsigned char program[] = {
        0xAD, 0x00, 0x60, 0x85, 0x20, // LDA $6000; STA $20 (the sum)
        0x2C, 0x02, 0x20,             // $C005 wait: BIT $2002 (vblank)
        0x10, 0xFB,                   // BPL wait
        0xA9, 0x01, 0x8D, 0x16, 0x40, // strobe controller 1
        0xA9, 0x00, 0x8D, 0x16, 0x40,
        0xA2, 0x08,                   // LDX #8
        0xA9, 0x00, 0x85, 0x21,       // $21 = 0
        0xAD, 0x16, 0x40,             // read: LDA $4016
        0x4A, 0x26, 0x21,             // LSR A; ROL $21
        0xCA, 0xD0, 0xF7,             // DEX; BNE read
        0xA5, 0x21, 0x18, 0x65, 0x20, // $20 += $21
        0x85, 0x20,
        0xA5, 0x20, 0x18, 0x65, 0x22, // $20 += $22 (a freeze cheat's)
        0x85, 0x20,
        0xA9, 0x3F, 0x8D, 0x06, 0x20, // PPUADDR = $3F00
        0xA9, 0x00, 0x8D, 0x06, 0x20,
        0xA5, 0x20, 0x29, 0x3F,       // PPUDATA = $20 & $3F
        0x8D, 0x07, 0x20,
        0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR = $0000
        0x8D, 0x06, 0x20,
        0x4C, 0x05, 0xC0,             // JMP wait
    };
    std::memcpy(rom.data() + 16, program, sizeof program);
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = static_cast<char>(0xC0);
    return rom;
}

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

std::string error(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_TRUE(r.isError()) << name << " should be refused";
    return r.isError() ? r.error->name : "";
}

std::uint64_t frame(MainWindow& w)
{
    return call(w, "core.run_state")["frame"].get<std::uint64_t>();
}

void runFor(MainWindow& w, std::uint64_t frames)
{
    const std::uint64_t until = frame(w) + frames;
    EXPECT_TRUE(waitUntil([&] { return frame(w) >= until; }));
}

void hold(MainWindow& w, Json buttons)
{
    call(w, "core.set_controller", {{"port", 1}, {"buttons", std::move(buttons)}});
}

// The program's running sum at $20 (the picture shows only its colour, and
// several colours are the same black, so pictures can match when sums do
// not).
int sum(MainWindow& w)
{
    const Json r = call(w, "core.peek", {{"addr", 0x20}, {"length", 1}});
    return static_cast<unsigned char>(QByteArray::fromBase64(QByteArray::fromStdString(r["bytes"].get<std::string>()))[0]);
}

std::string hash(MainWindow& w)
{
    return call(w, "core.frame_hash")["frameHash"].get<std::string>();
}

// Plays until the movie ends, which pauses on its last frame.
void playToTheEnd(MainWindow& w)
{
    EXPECT_TRUE(waitUntil([&] { return call(w, "gui.movie_status")["mode"] == "none"; }, 20000));
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused") << "the last frame stays on screen";
}

struct CnmFile {
    QJsonObject header;
    QByteArray body;
};

// library-layout "Movie file (.cnm)": a magic line, a JSON line, the input.
CnmFile readCnm(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    const QByteArray bytes = file.readAll();
    EXPECT_TRUE(bytes.startsWith("CNM1\n"));
    const qsizetype end = bytes.indexOf('\n', 5);
    EXPECT_GT(end, 5);
    CnmFile cnm;
    cnm.header = QJsonDocument::fromJson(bytes.mid(5, end - 5)).object();
    cnm.body = bytes.mid(end + 1);
    return cnm;
}

struct Game {
    QString folder;
    MainWindow::Options options = windowOptions(&folder);
    QString rom;
    QString other;
    std::string identity;

    Game()
    {
        rom = folder + QStringLiteral("/sum.nes");
        other = folder + QStringLiteral("/other.nes");
        for (const auto& [path, variant] : {std::pair{rom, char(0)}, std::pair{other, char(1)}}) {
            QFile file(path);
            EXPECT_TRUE(file.open(QIODevice::WriteOnly));
            file.write(inputSumRom(variant));
        }
    }

    QString movie(const char* name) const { return folder + QLatin1Char('/') + QLatin1String(name); }

    void open(MainWindow& w, const QString& path)
    {
        w.show();
        call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
        identity = call(w, "gui.open_rom", {{"path", path.toStdString()}})["identity"].get<std::string>();
        runFor(w, 2);
    }
    void open(MainWindow& w) { open(w, rom); }
};

// Presses a few patterns, a frame count apart, while the game runs.
void play(MainWindow& w)
{
    for (const Json& buttons : {Json{"A"}, Json{"Right", "B"}, Json::array(), Json{"Start", "Up"}, Json{"A", "B"}}) {
        hold(w, buttons);
        runFor(w, 12);
    }
    hold(w, Json::array());
}

} // namespace

TEST(GuiMovies, AMovieFromPowerOnReplaysToTheSameFinalFrame)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    hold(w, {"Select"}); // what the player holds before recording plays no part

    const QString path = game.movie("power-on.cnm");
    const Json started = call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "power_on"}});
    EXPECT_EQ(started["mode"], "recording");
    EXPECT_EQ(started["frames"], 0);
    play(w);
    call(w, "gui.reset"); // a recorded event
    play(w);
    call(w, "gui.toggle_pause");
    const Json stopped = call(w, "gui.stop_movie");
    const std::string recorded = hash(w);
    const int frames = stopped["frames"].get<int>();
    EXPECT_GE(frames, 120);

    const CnmFile cnm = readCnm(path);
    EXPECT_EQ(cnm.header.value(QStringLiteral("identity")).toString().toStdString(), game.identity);
    EXPECT_EQ(cnm.header.value(QStringLiteral("start")).toString(), QStringLiteral("power_on"));
    EXPECT_EQ(cnm.header.value(QStringLiteral("frames")).toInt(), frames);
    EXPECT_EQ(cnm.header.value(QStringLiteral("rerecords")).toInt(), 0);
    EXPECT_EQ(cnm.body.size(), frames * 2) << "two bytes a frame";
    EXPECT_TRUE(cnm.body.contains(char(0x80))) << "A alone: bit 7 (core serial order)";
    const QJsonArray events = cnm.header.value(QStringLiteral("events")).toArray();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].toObject().value(QStringLiteral("event")).toString(), QStringLiteral("reset"));
    const int resetAt = events[0].toObject().value(QStringLiteral("frame")).toInt();
    EXPECT_GT(resetAt, 0);
    EXPECT_LT(resetAt, frames);

    // The player's buttons go elsewhere: read-only play ignores them.
    hold(w, {"B", "Down"});
    const Json playing = call(w, "gui.play_movie", {{"path", path.toStdString()}});
    EXPECT_EQ(playing["mode"], "playing");
    EXPECT_EQ(playing["readOnly"], true);
    EXPECT_EQ(playing["frames"], frames);
    playToTheEnd(w);
    EXPECT_EQ(hash(w), recorded);
}

TEST(GuiMovies, AMovieFromAStateReplaysToTheSameFinalFrame)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    play(w); // the state holds a sum already

    const QString path = game.movie("state.cnm");
    const Json started = call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "state"}});
    EXPECT_EQ(started["movieFrame"], 0) << "the movie's own count, not the core's";
    EXPECT_GT(started["frame"].get<int>(), 0);
    play(w);
    call(w, "gui.toggle_pause");
    const int frames = call(w, "gui.stop_movie")["frames"].get<int>();
    const std::string recorded = hash(w);
    const int recordedSum = sum(w);
    const CnmFile cnm = readCnm(path);
    EXPECT_EQ(cnm.header.value(QStringLiteral("start")).toString(), QStringLiteral("state"));
    EXPECT_FALSE(QByteArray::fromBase64(cnm.header.value(QStringLiteral("state")).toString().toLatin1()).isEmpty());

    // The game goes on somewhere else first: 3 frames of Up+A, read as $88
    // once per vblank the program sees (2 or 3 of them, as the pause fell),
    // so the sum moves on: k x $88 is 0 mod 256 only for k a multiple of 32.
    // The picture cannot show it reliably: $20 & $3F may land on another of
    // the palette's blacks, the same frame hash.
    hold(w, {"Up", "A"});
    call(w, "core.run_frames", {{"frames", 3}});
    EXPECT_NE(sum(w), recordedSum);
    hold(w, Json::array());
    call(w, "gui.play_movie", {{"path", path.toStdString()}});
    playToTheEnd(w);
    EXPECT_EQ(sum(w), recordedSum);
    EXPECT_EQ(hash(w), recorded);
    EXPECT_EQ(call(w, "gui.movie_status")["frames"], 0) << "no movie once it ends";
    (void)frames;
}

TEST(GuiMovies, AMovieForADifferentGameIsRefused)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    const QString path = game.movie("first.cnm");
    call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "power_on"}});
    play(w);
    call(w, "gui.stop_movie");

    game.open(w, game.other);
    const std::uint64_t before = frame(w);
    EXPECT_EQ(error(w, "gui.play_movie", {{"path", path.toStdString()}}), "MovieMismatch");
    EXPECT_EQ(call(w, "gui.movie_status")["mode"], "none");
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Running") << "the game plays on";
    runFor(w, 2);
    EXPECT_GT(frame(w), before) << "no power cycle";
    EXPECT_EQ(error(w, "gui.play_movie", {{"path", game.movie("missing.cnm").toStdString()}}), "InvalidArguments");
}

TEST(GuiMovies, TakingOverAReadOnlyPlaybackTruncatesAndRecordsOn)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    const QString path = game.movie("takeover.cnm");
    call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "power_on"}});
    play(w);
    play(w);
    const int original = call(w, "gui.stop_movie")["frames"].get<int>();
    const QByteArray originalBody = readCnm(path).body;

    QAction* stop = w.findChild<QAction*>(QStringLiteral("gui.stop_movie"));
    QAction* readOnly = w.findChild<QAction*>(QStringLiteral("gui.toggle_movie_read_only"));
    ASSERT_TRUE(stop && readOnly);
    EXPECT_TRUE(waitUntil([&] { return !stop->isEnabled(); })) << "no movie";
    call(w, "gui.play_movie", {{"path", path.toStdString()}});
    EXPECT_TRUE(waitUntil([&] { return stop->isEnabled() && readOnly->isChecked(); })) << "the menu follows the movie";
    EXPECT_TRUE(waitUntil([&] { return call(w, "gui.movie_status")["movieFrame"].get<int>() >= 30; }));
    QLabel* overlay = w.findChild<QLabel*>(QStringLiteral("movieOverlay"));
    ASSERT_TRUE(overlay);
    EXPECT_TRUE(waitUntil([&] { return overlay->isVisible() && overlay->text().startsWith(QStringLiteral("▶")); }));
    // Read-only play refuses what would leave the movie's timeline.
    EXPECT_EQ(error(w, "gui.set_rewind", {{"on", true}}), "MoviePlaying");
    EXPECT_EQ(error(w, "gui.reset"), "MoviePlaying");
    EXPECT_EQ(error(w, "gui.load_state_file", {{"path", path.toStdString()}}), "MoviePlaying");

    call(w, "gui.toggle_pause");
    const Json taken = call(w, "gui.toggle_movie_read_only");
    EXPECT_EQ(taken["mode"], "recording");
    EXPECT_EQ(taken["readOnly"], false);
    EXPECT_EQ(taken["rerecords"], 1);
    const int at = taken["movieFrame"].get<int>();
    EXPECT_EQ(taken["frames"], at) << "the rest of the movie is gone";
    EXPECT_LT(at, original);
    EXPECT_TRUE(waitUntil([&] { return !readOnly->isChecked() && overlay->text().startsWith(QStringLiteral("● REC")); }));

    hold(w, {"Select", "Left"}); // before play goes on, so the first frame has them
    call(w, "gui.toggle_pause");
    runFor(w, 20);
    call(w, "gui.toggle_pause");
    const int frames = call(w, "gui.stop_movie")["frames"].get<int>();
    EXPECT_GE(frames, at + 20);
    const CnmFile cnm = readCnm(path);
    EXPECT_EQ(cnm.header.value(QStringLiteral("rerecords")).toInt(), 1);
    EXPECT_EQ(cnm.header.value(QStringLiteral("frames")).toInt(), frames);
    EXPECT_EQ(cnm.body.left(at * 2), originalBody.left(at * 2)) << "the frames before the takeover stay";
    EXPECT_EQ(cnm.body.at(at * 2), char(0x22)) << "Select and Left from the takeover on";
}

TEST(GuiMovies, StopWithoutAMovieAndClosingTheGameWhileRecording)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    EXPECT_EQ(error(w, "gui.stop_movie"), "NoMovie");
    EXPECT_EQ(error(w, "gui.toggle_movie_read_only"), "NoMovie");
    EXPECT_EQ(error(w, "gui.record_movie", {{"path", "relative.cnm"}, {"from", "power_on"}}), "InvalidArguments");

    // Closing the game ends the recording and writes the file.
    const QString path = game.movie("closed.cnm");
    call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "power_on"}});
    play(w);
    call(w, "gui.close_game");
    EXPECT_GE(readCnm(path).header.value(QStringLiteral("frames")).toInt(), 60);
    EXPECT_EQ(call(w, "gui.movie_status")["mode"], "none");
}

// #121: a movie carries the cheats it was recorded with (and their changes)
// and the battery RAM a power-on movie started from, and replays with them
// whatever the player has. The player's own cheats and battery RAM are back
// when it ends, and the save file never gets the movie's.
TEST(GuiMovies, AMovieKeepsItsCheatsAndBatteryAndLeavesThePlayersAlone)
{
    Game game;
    const QString rom = game.folder + QStringLiteral("/battery-sum.nes");
    {
        QFile file(rom);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(batterySumRom());
    }
    MainWindow w(game.options);
    game.open(w, rom);
    const auto sram = [&](unsigned char first) {
        call(w, "core.load_sram", {{"bytes", QByteArray(1, static_cast<char>(first)).toBase64().toStdString()}});
    };
    const auto sramFirst = [&] {
        const Json r = call(w, "core.read_sram", {{"offset", 0}, {"length", 1}});
        return static_cast<unsigned char>(QByteArray::fromBase64(QByteArray::fromStdString(r["bytes"].get<std::string>()))[0]);
    };
    const auto freeze = [&](int value) {
        call(w, "gui.add_cheat", {{"kind", "freeze"}, {"address", 0x22}, {"value", value}, {"label", "x"}});
    };
    sram(0x10);
    freeze(1); // cheat 0

    const QString path = game.movie("cheats.cnm");
    call(w, "gui.record_movie", {{"path", path.toStdString()}, {"from", "power_on"}});
    play(w);
    call(w, "gui.set_cheat_enabled", {{"index", 0}, {"enabled", false}});
    freeze(3); // cheat 1, from the same frame
    play(w);
    call(w, "gui.toggle_pause");
    call(w, "gui.stop_movie");
    const std::string recorded = hash(w);
    const int recordedSum = sum(w);

    const CnmFile cnm = readCnm(path);
    const QJsonArray startCheats = cnm.header.value(QStringLiteral("cheats")).toArray();
    ASSERT_EQ(startCheats.size(), 1) << "the cheats on at the start";
    EXPECT_EQ(startCheats[0].toObject().value(QStringLiteral("value")).toInt(), 1);
    const QByteArray battery =
        QByteArray::fromBase64(cnm.header.value(QStringLiteral("battery")).toString().toLatin1());
    ASSERT_FALSE(battery.isEmpty()) << "a power-on movie keeps the battery RAM it started from";
    EXPECT_EQ(static_cast<unsigned char>(battery[0]), 0x10);
    QJsonArray changes;
    for (const QJsonValue& e : cnm.header.value(QStringLiteral("events")).toArray()) {
        if (e.toObject().value(QStringLiteral("event")).toString() == QLatin1String("cheats")) changes.append(e);
    }
    ASSERT_EQ(changes.size(), 1) << "two changes before one frame are one event";
    const QJsonArray then = changes[0].toObject().value(QStringLiteral("cheats")).toArray();
    ASSERT_EQ(then.size(), 1);
    EXPECT_EQ(then[0].toObject().value(QStringLiteral("value")).toInt(), 3);

    // The player's own cheats and battery RAM now differ from the movie's.
    call(w, "gui.set_cheat_enabled", {{"index", 1}, {"enabled", false}});
    freeze(7); // cheat 2
    sram(0x55);
    const Json playerCheats = call(w, "gui.list_cheats");

    call(w, "gui.play_movie", {{"path", path.toStdString()}});
    playToTheEnd(w);
    EXPECT_EQ(sum(w), recordedSum) << "the movie's cheats and battery RAM, not the player's";
    EXPECT_EQ(hash(w), recorded);

    // Over: the player's cheats and battery RAM are back.
    EXPECT_EQ(call(w, "gui.list_cheats"), playerCheats);
    EXPECT_EQ(sramFirst(), 0x55);
}
