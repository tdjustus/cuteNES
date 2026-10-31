// G020: game identity, per-game file names and the library layout.
// Expectations come from contracts/library-layout.md and research R7: the
// identity is the SHA-1 of the ROM after its 16-byte header and any 512-byte
// trainer (iNES flags 6 bit 2); per-game names are "<name> [<id8>]", with
// the name sanitised and trimmed to 80 characters; lookups match the
// bracketed id8 and tell a collision apart by the full hash.

#include "library/game_identity.hpp"
#include "library/library.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cutenes::app::GameIdentity;
using cutenes::app::Library;

namespace {

// An iNES image: header, optional trainer, 16 KiB PRG and 8 KiB CHR filled
// with a pattern derived from `seed`.
QByteArray inesImage(char seed, bool trainer = false, char headerJunk = 0)
{
    QByteArray header("NES\x1A", 4);
    header.append(char(1)).append(char(1));      // 1 x 16 KiB PRG, 1 x 8 KiB CHR
    header.append(char(trainer ? 0x04 : 0x00));  // flags 6
    header.append(char(0));                      // flags 7
    header.append(QByteArray(8, headerJunk));    // bytes 8-15 (dumper junk)
    QByteArray body;
    for (int i = 0; i < 16384 + 8192; ++i) body.append(char(seed + i * 7));
    return header + (trainer ? QByteArray(512, char(0x55)) : QByteArray()) + body;
}

QString sha1Of(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

void writeFile(const QString& path, const QByteArray& bytes = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << path.toStdString();
    f.write(bytes);
}

} // namespace

TEST(GuiIdentity, IsTheSha1OfTheRomAfterTheHeader)
{
    const QByteArray rom = inesImage(3);
    const auto id = cutenes::app::identifyRom(rom);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->sha1, sha1Of(rom.mid(16)));
    EXPECT_EQ(id->id8(), id->sha1.left(8));
    EXPECT_EQ(id->sha1.size(), 40);
}

TEST(GuiIdentity, ReheaderedDumpsShareTheIdentity)
{
    EXPECT_EQ(cutenes::app::identifyRom(inesImage(3))->sha1,
        cutenes::app::identifyRom(inesImage(3, false, 'D'))->sha1);
    EXPECT_NE(cutenes::app::identifyRom(inesImage(3))->sha1, cutenes::app::identifyRom(inesImage(4))->sha1);
}

TEST(GuiIdentity, ATrainerIsNotPartOfTheIdentity)
{
    const QByteArray withTrainer = inesImage(3, true);
    const auto id = cutenes::app::identifyRom(withTrainer);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->sha1, sha1Of(withTrainer.mid(16 + 512)));
    EXPECT_EQ(id->sha1, cutenes::app::identifyRom(inesImage(3))->sha1);
}

TEST(GuiIdentity, NonInesFilesHaveNone)
{
    EXPECT_FALSE(cutenes::app::identifyRom("not a ROM").has_value());
    EXPECT_FALSE(cutenes::app::identifyRom(QByteArray("NES\x1A", 4)).has_value()) << "header only";
}

TEST(GuiNames, DisplayNamesAreSanitisedAndTrimmed)
{
    using cutenes::app::sanitizeDisplayName;
    EXPECT_EQ(sanitizeDisplayName(QStringLiteral("Zelda: \"Link\" <1/2>\\|?*")),
        QStringLiteral("Zelda_ _Link_ _1_2_____"));
    EXPECT_EQ(sanitizeDisplayName(QStringLiteral("a\tb\x01" "c")), QStringLiteral("a_b_c"));
    EXPECT_EQ(sanitizeDisplayName(QString(100, QLatin1Char('n'))).size(), 80);
    EXPECT_EQ(sanitizeDisplayName(QStringLiteral("  Tetris  ")), QStringLiteral("Tetris"));
}

TEST(GuiNames, GameNamesCarryTheShortIdentity)
{
    const GameIdentity id{QStringLiteral("0123abcd") + QString(32, QLatin1Char('e'))};
    EXPECT_EQ(cutenes::app::gameBaseName(QStringLiteral("Tetris"), id), QStringLiteral("Tetris [0123abcd]"));
}

// First launch: the folder, its subfolders and roms/README.txt (FR-010, A2).
TEST(GuiLibrary, FirstLaunchCreatesTheLayout)
{
    QTemporaryDir tmp;
    Library library(tmp.filePath(QStringLiteral("cuteNES")));
    QString error;
    ASSERT_TRUE(library.ensureLayout(&error)) << error.toStdString();
    for (const char* sub : {"roms", "saves", "states", "screenshots", "movies", "recordings", "cheats", "debug"}) {
        EXPECT_TRUE(QDir(library.root()).exists(QString::fromLatin1(sub))) << sub;
    }
    EXPECT_TRUE(QFile::exists(library.folder(Library::Folder::Roms) + QStringLiteral("/README.txt")));
    EXPECT_TRUE(library.isUsable());
}

TEST(GuiLibrary, AnUnwritableLocationIsReported)
{
    QTemporaryDir tmp;
    const QString blocker = tmp.filePath(QStringLiteral("file"));
    writeFile(blocker);
    Library library(blocker + QStringLiteral("/cuteNES")); // under a file: cannot exist
    QString error;
    EXPECT_FALSE(library.ensureLayout(&error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(library.isUsable());
}

// A renamed or re-headered ROM finds its save; a new game gets a new name
// (FR-011, A3).
TEST(GuiLibrary, SavesAreFoundByIdentityWhateverTheRomIsCalled)
{
    QTemporaryDir tmp;
    Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    const GameIdentity id = *cutenes::app::identifyRom(inesImage(9));

    EXPECT_TRUE(library.findGameFile(Library::Folder::Saves, id, QStringLiteral(".sav")).isEmpty());
    const QString created = library.gameFile(Library::Folder::Saves, id, QStringLiteral("My Game"), QStringLiteral(".sav"));
    EXPECT_EQ(QFileInfo(created).fileName(), QStringLiteral("My Game [%1].sav").arg(id.id8()));
    writeFile(created);
    library.recordIdentity(Library::Folder::Saves, created, id);

    // Later the ROM is called something else: the old file still matches.
    EXPECT_EQ(library.gameFile(Library::Folder::Saves, id, QStringLiteral("Renamed"), QStringLiteral(".sav")), created);
    EXPECT_EQ(library.findGameFile(Library::Folder::Saves, id, QStringLiteral(".sav")), created);
}

// Two different games whose identities start with the same 8 digits.
TEST(GuiLibrary, AShortIdentityCollisionIsToldApartByTheFullHash)
{
    QTemporaryDir tmp;
    Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    const GameIdentity first{QStringLiteral("abcd1234") + QString(32, QLatin1Char('1'))};
    const GameIdentity second{QStringLiteral("abcd1234") + QString(32, QLatin1Char('2'))};

    const QString a = library.gameFile(Library::Folder::Saves, first, QStringLiteral("Same"), QStringLiteral(".sav"));
    writeFile(a);
    library.recordIdentity(Library::Folder::Saves, a, first);

    EXPECT_TRUE(library.findGameFile(Library::Folder::Saves, second, QStringLiteral(".sav")).isEmpty());
    const QString b = library.gameFile(Library::Folder::Saves, second, QStringLiteral("Same"), QStringLiteral(".sav"));
    EXPECT_NE(a, b);
    EXPECT_EQ(QFileInfo(b).fileName(), QStringLiteral("Same [abcd1234-2].sav"));
    writeFile(b);
    library.recordIdentity(Library::Folder::Saves, b, second);
    EXPECT_EQ(library.findGameFile(Library::Folder::Saves, first, QStringLiteral(".sav")), a);
    EXPECT_EQ(library.findGameFile(Library::Folder::Saves, second, QStringLiteral(".sav")), b);
}

// A save copied in by hand (no recorded identity) still matches its id8.
TEST(GuiLibrary, AnUnrecordedFileMatchesByShortIdentity)
{
    QTemporaryDir tmp;
    Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    const GameIdentity id = *cutenes::app::identifyRom(inesImage(5));
    const QString manual = library.folder(Library::Folder::Saves) + QStringLiteral("/Copied [%1].sav").arg(id.id8());
    writeFile(manual);
    EXPECT_EQ(library.findGameFile(Library::Folder::Saves, id, QStringLiteral(".sav")), manual);
}

// Per-game folders (states/, screenshots/) record the identity inside.
TEST(GuiLibrary, PerGameFoldersRecordTheirIdentity)
{
    QTemporaryDir tmp;
    Library library(tmp.path());
    ASSERT_TRUE(library.ensureLayout());
    const GameIdentity id = *cutenes::app::identifyRom(inesImage(6));
    const QString dir = library.gameFile(Library::Folder::States, id, QStringLiteral("Game"), {});
    ASSERT_TRUE(QDir().mkpath(dir));
    library.recordIdentity(Library::Folder::States, dir, id);
    QFile identity(dir + QStringLiteral("/identity.txt"));
    ASSERT_TRUE(identity.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromLatin1(identity.readAll()).trimmed(), id.sha1);
    EXPECT_EQ(library.findGameFile(Library::Folder::States, id, {}), dir);
}

// US2-4: moving the library keeps everything and deletes nothing that was
// copied rather than renamed.
TEST(GuiLibrary, MovingTheContentsKeepsEveryFile)
{
    QTemporaryDir tmp;
    const Library from(tmp.filePath(QStringLiteral("old")));
    const Library to(tmp.filePath(QStringLiteral("new")));
    ASSERT_TRUE(from.ensureLayout());
    writeFile(from.folder(Library::Folder::Roms) + QStringLiteral("/Sub/Game.nes"), "rom");
    const GameIdentity id{QString(40, QLatin1Char('a'))};
    const QString save = from.gameFile(Library::Folder::Saves, id, QStringLiteral("Game"), QStringLiteral(".sav"));
    writeFile(save, "save");
    from.recordIdentity(Library::Folder::Saves, save, id);
    // The new location already has a saves folder of its own.
    ASSERT_TRUE(QDir().mkpath(to.folder(Library::Folder::Saves)));
    writeFile(to.folder(Library::Folder::Saves) + QStringLiteral("/Other [bbbbbbbb].sav"), "other");

    QString error;
    ASSERT_TRUE(from.moveContentsTo(to, &error)) << error.toStdString();
    EXPECT_TRUE(QFile::exists(to.folder(Library::Folder::Roms) + QStringLiteral("/Sub/Game.nes")));
    EXPECT_TRUE(QFile::exists(to.folder(Library::Folder::Saves) + QStringLiteral("/Other [bbbbbbbb].sav")));
    EXPECT_EQ(to.findGameFile(Library::Folder::Saves, id, QStringLiteral(".sav")),
        to.folder(Library::Folder::Saves) + QStringLiteral("/Game [aaaaaaaa].sav"));
    EXPECT_FALSE(QFileInfo::exists(from.folder(Library::Folder::Roms))) << "renamed into place";
    EXPECT_TRUE(QFile::exists(save)) << "copied, so the original stays";
}
