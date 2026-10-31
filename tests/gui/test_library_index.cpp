// G021: the library index. Expectations come from spec FR-002, FR-005 and
// FR-006 and contracts/library-layout.md "Index cache": every .nes under
// roms/ (nested) is listed with its header info and support status from a
// scratch load; changed files are re-read, unchanged ones come from the
// cache; a rejected ROM carries the core's reason.

#include "gui_support.hpp"
#include "library/library.hpp"
#include "library/library_index.hpp"
#include "settings/app_settings.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

using namespace cutenes::app;
using cutenes::test::waitUntil;

namespace {

// A NES 2.0 image with the given mapper, battery flag and region (byte 12:
// 0 NTSC, 1 PAL). iNES 1.0 has no reliable region field, so the core
// reports it as Unspecified.
QByteArray image(int mapper, bool battery = false, char seed = 1, bool pal = false)
{
    QByteArray rom("NES\x1A", 4);
    rom.append(char(1)).append(char(1));
    rom.append(char(((mapper & 0x0F) << 4) | (battery ? 0x02 : 0)));
    rom.append(char((mapper & 0xF0) | 0x08)); // NES 2.0
    rom.append(QByteArray(4, 0));               // bytes 8-11
    rom.append(char(pal ? 1 : 0));              // byte 12: timing
    rom.append(QByteArray(3, 0));
    for (int i = 0; i < 16384 + 8192; ++i) rom.append(char(seed + i));
    return rom;
}

void put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

const LibraryEntry* byName(const QVector<LibraryEntry>& entries, const QString& name)
{
    for (const auto& e : entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

TEST(GuiLibraryIndex, ListsEveryNesFileUnderRomsWithItsHeader)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    const QString roms = library.folder(Library::Folder::Roms);
    put(roms + QStringLiteral("/Alpha.nes"), image(0, false, 1));
    put(roms + QStringLiteral("/Sub/Deeper/Beta.NES"), image(1, true, 2, true));
    put(roms + QStringLiteral("/notes.txt"), "not a ROM");

    const auto entries = scanRoms(roms, {}, {});
    ASSERT_EQ(entries.size(), 2) << "README.txt and notes.txt are not ROMs";
    const auto* alpha = byName(entries, QStringLiteral("Alpha"));
    const auto* beta = byName(entries, QStringLiteral("Beta"));
    ASSERT_TRUE(alpha && beta);
    EXPECT_EQ(alpha->relativePath, QStringLiteral("Alpha.nes"));
    EXPECT_EQ(beta->relativePath, QStringLiteral("Sub/Deeper/Beta.NES"));
    EXPECT_EQ(alpha->mapper, 0);
    EXPECT_EQ(beta->mapper, 1);
    EXPECT_FALSE(alpha->battery);
    EXPECT_TRUE(beta->battery);
    EXPECT_EQ(alpha->region, QStringLiteral("NTSC"));
    EXPECT_EQ(beta->region, QStringLiteral("PAL"));
    EXPECT_TRUE(alpha->supported);
    EXPECT_EQ(alpha->identity, identifyRom(image(0, false, 1))->sha1);
}

TEST(GuiLibraryIndex, ARejectedRomCarriesTheCoresReason)
{
    QTemporaryDir tmp;
    put(tmp.filePath(QStringLiteral("Odd.nes")), image(255));
    put(tmp.filePath(QStringLiteral("Broken.nes")), "NES\x1A truncated");
    const auto entries = scanRoms(tmp.path(), {}, {});
    const auto* odd = byName(entries, QStringLiteral("Odd"));
    const auto* broken = byName(entries, QStringLiteral("Broken"));
    ASSERT_TRUE(odd && broken);
    EXPECT_FALSE(odd->supported);
    EXPECT_TRUE(odd->reason.contains(QStringLiteral("255"))) << odd->reason.toStdString();
    EXPECT_FALSE(broken->supported);
    EXPECT_FALSE(broken->reason.isEmpty());
}

// Recent games outside the library are listed too (FR-002), by absolute path.
TEST(GuiLibraryIndex, ExtraPathsAreListedAbsolutely)
{
    QTemporaryDir roms;
    QTemporaryDir elsewhere;
    const QString outside = elsewhere.filePath(QStringLiteral("Outside.nes"));
    put(outside, image(0, false, 7));
    const auto entries = scanRoms(roms.path(), {outside, elsewhere.filePath(QStringLiteral("gone.nes"))}, {});
    ASSERT_EQ(entries.size(), 1) << "a missing recent file is not listed";
    EXPECT_TRUE(entries[0].relativePath.isEmpty());
    EXPECT_EQ(entries[0].path, QFileInfo(outside).absoluteFilePath());
}

// Unchanged files (same size and mtime) are not read again.
TEST(GuiLibraryIndex, UnchangedEntriesComeFromThePreviousScan)
{
    QTemporaryDir tmp;
    put(tmp.filePath(QStringLiteral("A.nes")), image(0));
    auto first = scanRoms(tmp.path(), {}, {});
    ASSERT_EQ(first.size(), 1);
    first[0].name = QStringLiteral("from cache");
    QHash<QString, LibraryEntry> previous{{first[0].path, first[0]}};
    EXPECT_EQ(scanRoms(tmp.path(), {}, previous)[0].name, QStringLiteral("from cache"));

    QThread::msleep(1100); // mtime has 1 s resolution on some file systems
    put(tmp.filePath(QStringLiteral("A.nes")), image(1));
    const auto rescanned = scanRoms(tmp.path(), {}, previous);
    EXPECT_EQ(rescanned[0].name, QStringLiteral("A"));
    EXPECT_EQ(rescanned[0].mapper, 1);
}

TEST(GuiLibraryIndex, TheCacheRoundTrips)
{
    QTemporaryDir tmp;
    put(tmp.filePath(QStringLiteral("roms/A.nes")), image(4, true));
    const auto entries = scanRoms(tmp.filePath(QStringLiteral("roms")), {}, {});
    const QString cache = tmp.filePath(QStringLiteral("cache/library-index.json"));
    ASSERT_TRUE(writeIndexCache(cache, tmp.path(), entries));
    QString root;
    const auto loaded = readIndexCache(cache, &root);
    EXPECT_EQ(root, tmp.path());
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_EQ(loaded[0].path, entries[0].path);
    EXPECT_EQ(loaded[0].identity, entries[0].identity);
    EXPECT_EQ(loaded[0].mapper, 4);
    EXPECT_TRUE(loaded[0].battery);
    EXPECT_EQ(loaded[0].mtime, entries[0].mtime);
}

// FR-005: the index shows the cache at once, then a background scan and the
// file watcher pick up added and removed ROMs without a restart.
TEST(GuiLibraryIndex, AddedAndRemovedRomsAppearWithoutARestart)
{
    QTemporaryDir tmp;
    const Library library(tmp.filePath(QStringLiteral("lib")));
    ASSERT_TRUE(library.ensureLayout());
    AppSettings settings(tmp.filePath(QStringLiteral("settings.ini")));
    const QString roms = library.folder(Library::Folder::Roms);
    put(roms + QStringLiteral("/First.nes"), image(0, false, 1));

    LibraryIndex index(tmp.filePath(QStringLiteral("cache.json")), settings);
    index.setLibrary(library);
    ASSERT_TRUE(waitUntil([&] { return index.entries().size() == 1; }));

    put(roms + QStringLiteral("/New/Second.nes"), image(0, false, 2));
    ASSERT_TRUE(waitUntil([&] { return index.entries().size() == 2; }, 10000)) << "an added ROM";
    QFile::remove(roms + QStringLiteral("/First.nes"));
    ASSERT_TRUE(waitUntil([&] { return index.entries().size() == 1; }, 10000)) << "a removed ROM";
    EXPECT_EQ(index.entries()[0].name, QStringLiteral("Second"));

    // A new index starts from the cache before any scan finishes.
    LibraryIndex again(tmp.filePath(QStringLiteral("cache.json")), settings);
    again.setLibrary(library, /*scan=*/false);
    ASSERT_EQ(again.entries().size(), 1);
    EXPECT_EQ(again.entries()[0].name, QStringLiteral("Second"));
}

// Save and state flags and last-played come from the library and settings.
TEST(GuiLibraryIndex, EntriesShowSavesStatesAndLastPlayed)
{
    QTemporaryDir tmp;
    const Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    AppSettings settings(tmp.filePath(QStringLiteral("settings.ini")));
    put(library.folder(Library::Folder::Roms) + QStringLiteral("/G.nes"), image(0, true, 3));
    auto entries = scanRoms(library.folder(Library::Folder::Roms), {}, {});
    ASSERT_EQ(entries.size(), 1);
    const GameIdentity id{entries[0].identity};

    annotate(entries, library, settings);
    EXPECT_FALSE(entries[0].hasSave);
    EXPECT_EQ(entries[0].stateCount, 0);
    EXPECT_FALSE(entries[0].lastPlayed.isValid());

    put(library.gameFile(Library::Folder::Saves, id, QStringLiteral("G"), QStringLiteral(".sav")), QByteArray(8192, 0));
    const QString states = library.gameFile(Library::Folder::States, id, QStringLiteral("G"), {});
    put(states + QStringLiteral("/slot-01.state"), "s");
    put(states + QStringLiteral("/slot-02.state"), "s");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::UTC);
    settings.setLastPlayed(id.sha1, when);
    annotate(entries, library, settings);
    EXPECT_TRUE(entries[0].hasSave);
    EXPECT_EQ(entries[0].stateCount, 2);
    EXPECT_EQ(entries[0].lastPlayed, when);
}
