// G070-G071 (spec US8-1..5, FR-050/051/053, data-model "Cheat" and
// "CheatSearch", library-layout "cheats/<game>.json").
// The test ROM counts frames: every 16th frame it adds 1 to $10 (from 0)
// and to the 16-bit value at $14-$15 (from $1280). Expectations follow from
// that program, not from the emulator.
// - The search narrows to one address by value while the game runs, with
//   1- and 2-byte values.
// - A value written through the session reaches the game at the next frame
//   boundary, without pausing it.
// - freeze holds a value, set writes it once, and Game Genie codes patch
//   ROM reads; an invalid code is refused with the reason.
// - Cheats and watches are saved per game and come back when it reopens.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QDialog>
#include <QFile>
#include <QThread>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

// NROM-128: the program above at $C000 (reset vector), NMI off.
QByteArray counterRom()
{
    QByteArray rom(16 + 16384 + 8192, '\0');
    const char header[] = {'N', 'E', 'S', 0x1A, 1, 1};
    std::memcpy(rom.data(), header, sizeof header);
    const unsigned char program[] = {
        0xA9, 0x12, 0x85, 0x15, // LDA #$12; STA $15
        0xA9, 0x80, 0x85, 0x14, // LDA #$80; STA $14
        0xA9, 0x00, 0x85, 0x10, // LDA #$00; STA $10
        0x85, 0x11,             // STA $11 (the frame count)
        0x2C, 0x02, 0x20,       // $C00E wait: BIT $2002 (vblank)
        0x10, 0xFB,             // BPL wait
        0xE6, 0x11,             // INC $11
        0xA5, 0x11, 0x29, 0x0F, // LDA $11; AND #$0F
        0xD0, 0xF3,             // BNE wait (15 frames in 16)
        0xE6, 0x10,             // INC $10
        0xE6, 0x14,             // INC $14
        0xD0, 0xED,             // BNE wait
        0xE6, 0x15,             // INC $15
        0x4C, 0x0E, 0xC0,       // JMP wait
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

unsigned peek(MainWindow& w, int address, int size = 1)
{
    const Json r = call(w, "core.peek", {{"addr", address}, {"length", size}});
    const QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(r["bytes"].get<std::string>()));
    const auto byte = [&](int i) { return raw.size() > i ? static_cast<unsigned>(static_cast<unsigned char>(raw[i])) : 0u; };
    return size == 2 ? byte(0) | byte(1) << 8 : byte(0);
}

// Waits for the counter at `address` to step, so the next 16 frames
// (about 0.27 s) hold its value: the moment a player reads it off the
// screen and types it in.
unsigned justChanged(MainWindow& w, int address, int size = 1)
{
    const unsigned before = peek(w, address, size);
    unsigned now = before;
    EXPECT_TRUE(waitUntil([&] { return (now = peek(w, address, size)) != before; }));
    return now;
}

struct Game {
    QString folder;
    MainWindow::Options options = windowOptions(&folder);
    QString rom;

    Game()
    {
        rom = folder + QStringLiteral("/counter.nes");
        QFile file(rom);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(counterRom());
    }

    void open(MainWindow& w) const
    {
        w.show();
        call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
        call(w, "gui.open_rom", {{"path", rom.toStdString()}});
        EXPECT_TRUE(waitUntil([&] { return peek(w, 0x15) == 0x12; })) << "the program has started";
    }
};

} // namespace

TEST(GuiCheatSearch, NarrowsToTheCounterByValueWhileTheGameRuns)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);

    const Json reset = call(w, "gui.cheat_search_reset", {{"size", 1}});
    const int prgRam = call(w, "core.cartridge_info")["cartridge"]["prgRamSize"].get<int>();
    EXPECT_EQ(reset["candidates"], 2048 + prgRam) << "every byte of RAM and PRG-RAM";
    for (int round = 0; round < 3; ++round) {
        const unsigned seen = justChanged(w, 0x10);
        call(w, "gui.cheat_search_filter", {{"filter", "equal"}, {"value", seen}});
    }
    const Json results = call(w, "gui.cheat_search_results");
    ASSERT_EQ(results["candidates"], 1) << results.dump();
    EXPECT_EQ(results["results"][0]["address"], 0x10);
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Running") << "the search never paused the game";
}

TEST(GuiCheatSearch, FindsATwoByteValueAndFiltersAgainstThePreviousSnapshot)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);

    call(w, "gui.cheat_search_reset", {{"size", 2}});
    for (int round = 0; round < 2; ++round) {
        const unsigned seen = justChanged(w, 0x14, 2);
        ASSERT_EQ(seen >> 8, 0x12u) << "the 16-bit counter starts at $1280";
        call(w, "gui.cheat_search_filter", {{"filter", "equal"}, {"value", seen}});
    }
    Json results = call(w, "gui.cheat_search_results");
    ASSERT_EQ(results["candidates"], 1) << results.dump();
    EXPECT_EQ(results["results"][0]["address"], 0x14);
    EXPECT_EQ(results["size"], 2);

    // Without a value: against the previous snapshot.
    call(w, "gui.cheat_search_reset", {{"size", 1}});
    justChanged(w, 0x10);
    call(w, "gui.cheat_search_filter", {{"filter", "greater"}});
    justChanged(w, 0x10);
    call(w, "gui.cheat_search_filter", {{"filter", "changed"}});
    results = call(w, "gui.cheat_search_results", {{"max", 1000}});
    std::vector<int> addresses;
    for (const Json& r : results["results"]) addresses.push_back(r["address"].get<int>());
    EXPECT_NE(std::find(addresses.begin(), addresses.end(), 0x10), addresses.end()) << results.dump();
    EXPECT_NE(std::find(addresses.begin(), addresses.end(), 0x14), addresses.end());
    EXPECT_EQ(std::find(addresses.begin(), addresses.end(), 0x15), addresses.end()) << "$15 stays $12";

    EXPECT_TRUE(w.session().call("gui.cheat_search_filter", {{"filter", "bigger"}}).isError());
}

TEST(GuiCheats, AnEditedValueReachesTheGameAtTheNextFrameWithoutAPause)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    call(w, "core.write_memory", {{"addr", 0x10}, {"bytes", "dw=="}});
    const unsigned now = peek(w, 0x10);
    EXPECT_TRUE(now == 0x77 || now == 0x78) << now;
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Running");
    EXPECT_TRUE(waitUntil([&] { return peek(w, 0x10) > 0x78; })) << "the game counts on from the new value";
}

TEST(GuiCheats, FreezeHoldsSetWritesOnceAndGameGenieCodesPatchReads)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);

    const Json added = call(w, "gui.add_cheat", {{"kind", "freeze"}, {"address", 0x14}, {"value", 0x42}, {"label", "Score"}});
    const int freeze = added["index"].get<int>();
    // The game adds 1 every 16 frames; each frame starts from $42 again.
    for (int i = 0; i < 40; ++i) {
        const unsigned v = peek(w, 0x14);
        EXPECT_TRUE(v == 0x42 || v == 0x43) << v;
        QThread::msleep(25);
    }
    call(w, "gui.set_cheat_enabled", {{"index", freeze}, {"enabled", false}});
    EXPECT_TRUE(waitUntil([&] { return peek(w, 0x14) > 0x43; })) << "free again";

    call(w, "gui.add_cheat", {{"kind", "set"}, {"address", 0x10}, {"value", 0xA0}});
    EXPECT_GE(peek(w, 0x10), 0xA0u);
    EXPECT_TRUE(waitUntil([&] { return peek(w, 0x10) > 0xA1; })) << "set writes once";

    // nesdev "Game Genie": GOSSIP is $D1DD = $14; the ROM holds $00 there.
    EXPECT_EQ(peek(w, 0xD1DD), 0x00u);
    const int genie = call(w, "gui.add_cheat", {{"kind", "game_genie"}, {"code", "gossip"}})["index"].get<int>();
    EXPECT_EQ(peek(w, 0xD1DD), 0x14u);
    call(w, "gui.set_cheat_enabled", {{"index", genie}, {"enabled", false}});
    EXPECT_EQ(peek(w, 0xD1DD), 0x00u);

    const Result invalid = w.session().call("gui.add_cheat", {{"kind", "game_genie"}, {"code", "BADBAD"}});
    ASSERT_TRUE(invalid.isError());
    EXPECT_NE(invalid.error->message.find("APZLGITYEOXUKSVN"), std::string::npos) << invalid.error->message;

    const Json list = call(w, "gui.list_cheats")["cheats"];
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0]["label"], "Score");
    EXPECT_EQ(list[0]["enabled"], false);
    EXPECT_EQ(list[2]["code"], "GOSSIP");
    call(w, "gui.remove_cheat", {{"index", 1}});
    EXPECT_EQ(call(w, "gui.list_cheats")["cheats"].size(), 2u);
}

TEST(GuiCheats, CheatsAndWatchesAreSavedPerGame)
{
    Game game;
    {
        MainWindow w(game.options);
        game.open(w);
        call(w, "gui.add_cheat", {{"kind", "freeze"}, {"address", 0x10}, {"value", 9}, {"label", "Lives"}});
        call(w, "gui.add_cheat", {{"kind", "game_genie"}, {"code", "ZEXPYGLA"}, {"enabled", false}});
        call(w, "gui.add_watch", {{"address", 0x14}, {"size", 2}, {"label", "Score"}});
        const Json watches = call(w, "gui.list_watches")["watches"];
        ASSERT_EQ(watches.size(), 1u);
        EXPECT_EQ(watches[0]["value"].get<unsigned>() >> 8, 0x12u) << "a live 2-byte value";
        call(w, "gui.close_game");
    }
    MainWindow w(game.options);
    game.open(w);
    const Json cheats = call(w, "gui.list_cheats")["cheats"];
    ASSERT_EQ(cheats.size(), 2u);
    EXPECT_EQ(cheats[0]["label"], "Lives");
    EXPECT_EQ(cheats[0]["enabled"], true);
    EXPECT_EQ(cheats[1]["enabled"], false);
    EXPECT_TRUE(waitUntil([&] { return peek(w, 0x10) == 9 || peek(w, 0x10) == 10; })) << "the freeze is back";
    const Json watches = call(w, "gui.list_watches")["watches"];
    ASSERT_EQ(watches.size(), 1u);
    EXPECT_EQ(watches[0]["label"], "Score");
    call(w, "gui.remove_watch", {{"index", 0}});
    EXPECT_TRUE(call(w, "gui.list_watches")["watches"].empty());
}

TEST(GuiCheats, TheWindowsOpenAndTheGamePlaysOn)
{
    Game game;
    MainWindow w(game.options);
    game.open(w);
    call(w, "gui.add_cheat", {{"kind", "freeze"}, {"address", 0x10}, {"value", 3}, {"label", "Lives"}});
    call(w, "gui.show_cheats");
    QDialog* cheats = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (cheats = w.findChild<QDialog*>(QStringLiteral("cheatsWindow"))) && cheats->isVisible();
    }));
    auto* table = cheats->findChild<QTableWidget*>(QStringLiteral("cheatTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_TRUE(waitUntil([&] { return table->rowCount() == 1; }));

    call(w, "gui.show_cheat_search");
    QDialog* search = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (search = w.findChild<QDialog*>(QStringLiteral("cheatSearchWindow"))) && search->isVisible();
    }));
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Running");
}
