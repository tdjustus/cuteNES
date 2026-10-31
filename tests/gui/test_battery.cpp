// G023: battery saves (spec FR-012, US2-2; research R8; A4). Expectations:
// a battery game's RAM reaches disk within the write interval (5 s in the
// app, so within FR-012's 10 s), at most once per interval, and at once on
// close; writes are atomic; a save of the wrong size is set aside as
// .bad-<date>, never loaded or deleted; a save is found by game identity;
// a second instance on the same game does not write.
//
// The game is blargg's 01-basics with the battery flag set: per its readme
// it writes its status to $6000 and the signature DE B0 61 to $6001-$6003.

#include "library/library.hpp"
#include "persistence/battery_saver.hpp"

#include <cutenes/machine.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

#include <chrono>

using namespace cutenes::app;
using namespace std::chrono_literals;

namespace {

QByteArray batteryRom()
{
    QFile f(QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray rom = f.readAll();
    rom[6] = char(rom[6] | 0x02); // battery-backed PRG-RAM
    return rom;
}

struct Game {
    cutenes::Machine machine;
    GameIdentity id;
    Game()
    {
        const QByteArray rom = batteryRom();
        id = *identifyRom(rom);
        const auto r = machine.loadCartridge(reinterpret_cast<const std::uint8_t*>(rom.constData()),
            static_cast<std::size_t>(rom.size()));
        EXPECT_EQ(r.error, cutenes::LoadError::Ok);
        machine.powerOn();
    }
    void run(int frames) { machine.runFrames(static_cast<std::uint32_t>(frames)); }
};

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool waitForFile(const QString& path, std::chrono::milliseconds limit)
{
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
        if (QFile::exists(path)) return true;
        QThread::msleep(5);
    }
    return QFile::exists(path);
}

bool hasSignature(const QByteArray& save)
{
    return save.size() > 3 && uchar(save[1]) == 0xDE && uchar(save[2]) == 0xB0 && uchar(save[3]) == 0x61;
}

} // namespace

TEST(GuiBattery, ChangedRamReachesDiskWithinTheInterval)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    BatterySaver saver(200ms);
    const auto opened = saver.open(library, game.id, QStringLiteral("Basics"), game.machine);
    ASSERT_TRUE(opened.active) << opened.warning.toStdString();
    EXPECT_FALSE(opened.loaded);

    game.run(60);
    const auto changedAt = std::chrono::steady_clock::now();
    saver.poll(game.machine);
    ASSERT_TRUE(waitForFile(opened.path, 2s));
    EXPECT_LT(std::chrono::steady_clock::now() - changedAt, 1s);
    const QByteArray save = readAll(opened.path);
    EXPECT_EQ(save.size(), static_cast<qsizetype>(game.machine.sram().size));
    EXPECT_TRUE(hasSignature(save));
    EXPECT_EQ(QFileInfo(opened.path).fileName(), QStringLiteral("Basics [%1].sav").arg(game.id.id8()));
}

TEST(GuiBattery, WritesAtMostOncePerInterval)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    BatterySaver saver(400ms);
    ASSERT_TRUE(saver.open(library, game.id, QStringLiteral("G"), game.machine).active);
    const std::uint8_t a = 1;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        const std::uint8_t v = static_cast<std::uint8_t>(a + i);
        game.machine.writeMemory(0x7000, &v, 1);
        saver.poll(game.machine);
        QThread::msleep(20);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    QThread::msleep(500);
    const int allowed = 1 + static_cast<int>(elapsed / 400ms) + 1;
    EXPECT_GE(saver.writes(), 1);
    EXPECT_LE(saver.writes(), allowed);
    EXPECT_EQ(uchar(readAll(saver.path())[0x1000]), a + 19) << "the last value wins";
}

TEST(GuiBattery, ClosingWritesAtOnce)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    BatterySaver saver(1h); // never on its own
    const auto opened = saver.open(library, game.id, QStringLiteral("G"), game.machine);
    game.run(60);
    saver.poll(game.machine);
    saver.close(game.machine);
    EXPECT_TRUE(hasSignature(readAll(opened.path)));
    EXPECT_FALSE(saver.active());
}

TEST(GuiBattery, ASaveIsLoadedByIdentity)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game first;
    const std::size_t size = first.machine.sram().size;
    ASSERT_GT(size, 0x1F00u);
    QByteArray bytes(static_cast<qsizetype>(size), 0);
    bytes[0x1F00] = 0x42;
    // Saved under another name: the ROM has been renamed since.
    QFile f(library.folder(Library::Folder::Saves) + QStringLiteral("/Old Name [%1].sav").arg(first.id.id8()));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    f.close();

    BatterySaver saver;
    const auto opened = saver.open(library, first.id, QStringLiteral("New Name"), first.machine);
    EXPECT_TRUE(opened.loaded);
    EXPECT_EQ(opened.path, f.fileName());
    EXPECT_EQ(first.machine.sram().data[0x1F00], 0x42);
}

TEST(GuiBattery, AWrongSizedSaveIsSetAsideNotLoaded)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    const QString path = library.gameFile(Library::Folder::Saves, game.id, QStringLiteral("G"), QStringLiteral(".sav"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("bad");
    f.close();

    BatterySaver saver;
    const auto opened = saver.open(library, game.id, QStringLiteral("G"), game.machine);
    EXPECT_FALSE(opened.loaded);
    EXPECT_FALSE(opened.warning.isEmpty());
    EXPECT_FALSE(QFile::exists(path));
    const QStringList aside = QDir(library.folder(Library::Folder::Saves))
                                  .entryList({QStringLiteral("*.sav.bad-*")}); // [ ] would be a glob class
    ASSERT_EQ(aside.size(), 1);
    EXPECT_EQ(readAll(library.folder(Library::Folder::Saves) + QLatin1Char('/') + aside[0]), QByteArray("bad"));
}

#if !defined(_WIN32)
// A write that cannot complete leaves the previous save intact.
TEST(GuiBattery, AFailedWriteKeepsTheOldSave)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    BatterySaver saver(1h);
    const auto opened = saver.open(library, game.id, QStringLiteral("G"), game.machine);
    saver.close(game.machine); // an all-zero save
    const QByteArray before = readAll(opened.path);
    ASSERT_FALSE(before.isEmpty());

    BatterySaver again(1h);
    again.open(library, game.id, QStringLiteral("G"), game.machine);
    game.run(60);
    QFile::setPermissions(library.folder(Library::Folder::Saves), QFile::ReadOwner | QFile::ExeOwner);
    again.poll(game.machine);
    again.close(game.machine);
    QFile::setPermissions(library.folder(Library::Folder::Saves),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    EXPECT_EQ(readAll(opened.path), before);
}
#endif

TEST(GuiBattery, ASecondInstanceDoesNotWrite)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    Game game;
    BatterySaver first;
    ASSERT_TRUE(first.open(library, game.id, QStringLiteral("G"), game.machine).active);
    BatterySaver second;
    const auto opened = second.open(library, game.id, QStringLiteral("G"), game.machine);
    EXPECT_FALSE(opened.active);
    EXPECT_FALSE(opened.warning.isEmpty());
    first.close(game.machine);
}

TEST(GuiBattery, AGameWithoutBatteryHasNoSave)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    QFile f(QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray rom = f.readAll();
    cutenes::Machine machine;
    machine.loadCartridge(reinterpret_cast<const std::uint8_t*>(rom.constData()), static_cast<std::size_t>(rom.size()));
    BatterySaver saver;
    EXPECT_FALSE(saver.open(library, *identifyRom(rom), QStringLiteral("G"), machine).active);
    EXPECT_TRUE(QDir(library.folder(Library::Folder::Saves)).entryList({QStringLiteral("*.sav")}).isEmpty());
}
