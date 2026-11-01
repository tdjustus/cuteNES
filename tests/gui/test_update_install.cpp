// G099 (research R16 "Replacement"; spec FR-094, FR-095): replacing the
// installed version, on files in a temporary folder. Expectations come from
// R16:
// - every swap keeps the old version whole until the new one is in place,
//   and puts it back if a step fails;
// - an application folder (the Windows zip) is swapped file by file: each
//   old file is renamed aside to <name>.cutenes-old, so Qt never loads it,
//   and the new one renamed in; Windows allows renaming a running .exe or a
//   loaded DLL, though not overwriting it;
// - an AppImage is one file, renamed over the old one and executable;
// - a macOS bundle is unpacked beside the old one and renamed in, the old one
//   kept aside as .cutenes-old.app;
// - the next start removes what was kept aside.

#include "update/update_install.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace update = cutenes::app::update;

namespace {

void write(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
    file.write(bytes);
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

// Every file under `root`, relative, sorted.
QStringList files(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

} // namespace

TEST(UpdateInstall, AnApplicationFolderIsSwappedFileByFile)
{
    QTemporaryDir dir;
    const QString app = dir.filePath(QStringLiteral("cuteNES"));
    const QString fresh = dir.filePath(QStringLiteral("staged"));
    write(app + QStringLiteral("/cutenes.exe"), "old exe");
    write(app + QStringLiteral("/Qt6Core.dll"), "old core");
    write(app + QStringLiteral("/platforms/qwindows.dll"), "old plugin");
    write(app + QStringLiteral("/gone.dll"), "only in the old version");
    write(fresh + QStringLiteral("/cutenes.exe"), "new exe");
    write(fresh + QStringLiteral("/Qt6Core.dll"), "new core");
    write(fresh + QStringLiteral("/platforms/qwindows.dll"), "new plugin");
    write(fresh + QStringLiteral("/added.dll"), "only in the new version");

    QString why;
    ASSERT_TRUE(update::swapFolder(fresh, app, &why)) << why.toStdString();
    EXPECT_EQ(read(app + QStringLiteral("/cutenes.exe")), "new exe");
    EXPECT_EQ(read(app + QStringLiteral("/Qt6Core.dll")), "new core");
    EXPECT_EQ(read(app + QStringLiteral("/platforms/qwindows.dll")), "new plugin");
    EXPECT_EQ(read(app + QStringLiteral("/added.dll")), "only in the new version");
    // The old files, all of them, kept aside under a name nothing loads.
    EXPECT_EQ(read(app + QStringLiteral("/gone.dll.cutenes-old")), "only in the old version");
    EXPECT_EQ(read(app + QStringLiteral("/cutenes.exe.cutenes-old")), "old exe");
    EXPECT_FALSE(QFile::exists(app + QStringLiteral("/gone.dll")));
    EXPECT_FALSE(QDir(fresh).exists()); // the staged folder is used up

    // The next start removes them.
    update::removeOldVersion(app);
    EXPECT_EQ(files(app), (QStringList{QStringLiteral("Qt6Core.dll"), QStringLiteral("added.dll"),
                              QStringLiteral("cutenes.exe"), QStringLiteral("platforms/qwindows.dll")}));
}

TEST(UpdateInstall, AFailedSwapPutsTheOldVersionBack)
{
    QTemporaryDir dir;
    const QString app = dir.filePath(QStringLiteral("cuteNES"));
    const QString fresh = dir.filePath(QStringLiteral("staged"));
    write(app + QStringLiteral("/cutenes.exe"), "old exe");
    write(app + QStringLiteral("/Qt6Core.dll"), "old core");
    write(fresh + QStringLiteral("/cutenes.exe"), "new exe");
    write(fresh + QStringLiteral("/Qt6Core.dll"), "new core");
    // A folder where a new file must go: the rename in cannot succeed.
    QDir().mkpath(app + QStringLiteral("/blocker"));
    write(app + QStringLiteral("/blocker/inside"), "x");
    write(fresh + QStringLiteral("/blocker"), "a file where a folder is");

    QString why;
    EXPECT_FALSE(update::swapFolder(fresh, app, &why));
    EXPECT_FALSE(why.isEmpty());
    EXPECT_EQ(read(app + QStringLiteral("/cutenes.exe")), "old exe");
    EXPECT_EQ(read(app + QStringLiteral("/Qt6Core.dll")), "old core");
    EXPECT_EQ(files(app), (QStringList{QStringLiteral("Qt6Core.dll"), QStringLiteral("blocker/inside"),
                              QStringLiteral("cutenes.exe")}));
}

TEST(UpdateInstall, AnAppImageIsReplacedByRenameAndIsExecutable)
{
    QTemporaryDir dir;
    const QString target = dir.filePath(QStringLiteral("cuteNES.AppImage"));
    const QString download = dir.filePath(QStringLiteral(".cuteNES.AppImage.download"));
    write(target, "old image");
    write(download, "new image");
    QString why;
    ASSERT_TRUE(update::replaceFile(download, target, &why)) << why.toStdString();
    EXPECT_EQ(read(target), "new image");
    EXPECT_FALSE(QFile::exists(download));
#if !defined(_WIN32)
    EXPECT_TRUE(QFileInfo(target).isExecutable());
#endif
}

#if defined(Q_OS_MACOS)
TEST(UpdateInstall, AMacBundleIsUnpackedBesideAndRenamedIn)
{
    QTemporaryDir dir;
    const QString bundle = dir.filePath(QStringLiteral("cutenes.app"));
    write(bundle + QStringLiteral("/Contents/MacOS/cutenes"), "old binary");
    // The new release's zip, made as the package job makes it.
    QTemporaryDir build;
    const QString fresh = build.filePath(QStringLiteral("cutenes.app"));
    write(fresh + QStringLiteral("/Contents/MacOS/cutenes"), "new binary");
    write(fresh + QStringLiteral("/Contents/Info.plist"), "<plist/>");
    const QString zip = dir.filePath(QStringLiteral(".cuteNES-1.2.0-macos-arm64.zip.download"));
    ASSERT_EQ(QProcess::execute(QStringLiteral("ditto"), {QStringLiteral("-c"), QStringLiteral("-k"),
                                    QStringLiteral("--keepParent"), fresh, zip}),
        0);

    QString why;
    ASSERT_TRUE(update::installBundle(zip, bundle, &why)) << why.toStdString();
    EXPECT_EQ(read(bundle + QStringLiteral("/Contents/MacOS/cutenes")), "new binary");
    EXPECT_EQ(read(dir.filePath(QStringLiteral(".cutenes-old.app/Contents/MacOS/cutenes"))), "old binary");
    EXPECT_FALSE(QFile::exists(zip));
    update::removeOldVersion(bundle);
    EXPECT_FALSE(QFileInfo::exists(dir.filePath(QStringLiteral(".cutenes-old.app"))));
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
        QStringList{QStringLiteral("cutenes.app")});
}

TEST(UpdateInstall, AZipWithoutTheBundleChangesNothing)
{
    QTemporaryDir dir;
    const QString bundle = dir.filePath(QStringLiteral("cutenes.app"));
    write(bundle + QStringLiteral("/Contents/MacOS/cutenes"), "old binary");
    QTemporaryDir build;
    write(build.filePath(QStringLiteral("other/readme.txt")), "not a bundle");
    const QString zip = dir.filePath(QStringLiteral(".bad.zip.download"));
    ASSERT_EQ(QProcess::execute(QStringLiteral("ditto"), {QStringLiteral("-c"), QStringLiteral("-k"),
                                    QStringLiteral("--keepParent"), build.filePath(QStringLiteral("other")), zip}),
        0);
    QString why;
    EXPECT_FALSE(update::installBundle(zip, bundle, &why));
    EXPECT_TRUE(why.contains(QStringLiteral("cutenes.app"))) << why.toStdString();
    EXPECT_EQ(read(bundle + QStringLiteral("/Contents/MacOS/cutenes")), "old binary");
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
        QStringList{QStringLiteral("cutenes.app")});
}
#endif

#if defined(_WIN32)
// The claim the Windows swap rests on: a loaded DLL can be renamed (not
// overwritten), so the running version's files can be moved aside.
TEST(UpdateInstall, ALoadedDllCanBeSwappedOnWindows)
{
    QTemporaryDir dir;
    const QString app = dir.filePath(QStringLiteral("cuteNES"));
    const QString fresh = dir.filePath(QStringLiteral("staged"));
    const QString dll = app + QStringLiteral("/loaded.dll");
    QDir().mkpath(app);
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    ASSERT_TRUE(QFile::copy(QString::fromWCharArray(system) + QStringLiteral("\\version.dll"), dll));
    HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(dll).utf16()));
    ASSERT_NE(module, nullptr);
    // Overwriting it in place fails, as R16 says.
    {
        QFile overwrite(dll);
        EXPECT_FALSE(overwrite.open(QIODevice::WriteOnly));
    }
    write(fresh + QStringLiteral("/loaded.dll"), "new dll");
    QString why;
    EXPECT_TRUE(update::swapFolder(fresh, app, &why)) << why.toStdString();
    EXPECT_EQ(read(dll), "new dll");
    EXPECT_TRUE(QFile::exists(dll + QStringLiteral(".cutenes-old")));
    FreeLibrary(module);
    update::removeOldVersion(app);
    EXPECT_FALSE(QFile::exists(dll + QStringLiteral(".cutenes-old")));
}
#endif
