#include "update/update_install.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>

#include <filesystem>
#include <utility>
#include <vector>

#ifndef CUTENES_PACKAGE
#define CUTENES_PACKAGE "none"
#endif

namespace cutenes::app::update {

namespace {

const QString kOld = QStringLiteral(".cutenes-old");
const QString kOldBundle = QStringLiteral(".cutenes-old.app");
const QString kStaging = QStringLiteral(".cutenes-update");

QString native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

// Every file under `root`, relative to it.
QStringList filesUnder(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    return out;
}

// Can files be made in `dir`? Asked by making one: on Windows the
// permission bits Qt reports leave out the ACLs.
bool writable(const QString& dir)
{
    QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".cutenes-probe-XXXXXX")));
    return probe.open();
}

bool run(const QString& program, const QStringList& arguments, QString* why)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (why) {
            *why = QObject::tr("%1 could not unpack the download: %2")
                       .arg(program, QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        }
        return false;
    }
    return true;
}

} // namespace

Target currentTarget()
{
    Target t;
    t.package = QStringLiteral(CUTENES_PACKAGE);
    const QString dir = QCoreApplication::applicationDirPath();
    if (t.package == QLatin1String("macos-zip")) {
        t.path = QDir(dir + QStringLiteral("/../..")).absolutePath(); // .../cutenes.app/Contents/MacOS
    } else if (t.package == QLatin1String("windows-zip")) {
        t.path = dir;
    } else if (t.package == QLatin1String("appimage")) {
        t.path = qEnvironmentVariable("APPIMAGE"); // set by the AppImage runtime
    }
    return t;
}

QString suffixFor(const QString& package)
{
    if (package == QLatin1String("windows-zip")) return QStringLiteral("windows-x64.zip");
    if (package == QLatin1String("appimage")) return QStringLiteral("linux-x86_64.AppImage");
    return platformSuffix(); // macos-zip is this Mac's architecture; none, this system's
}

InstallSupport supportFor(const Target& target)
{
    if (target.package == QLatin1String("none") || target.package.isEmpty()) {
        return {false, QObject::tr("This copy of cuteNES was not installed from a cuteNES release package, so it "
                                   "cannot replace itself. See Details opens the release page.")};
    }
    if (target.path.isEmpty() || !QFileInfo::exists(target.path)) {
        return {false, QObject::tr("cuteNES cannot find where it is installed, so it cannot replace itself. See "
                                   "Details opens the release page.")};
    }
    const QString folder = downloadFolder(target);
    const bool ok = writable(folder) && (target.package != QLatin1String("windows-zip") || writable(target.path));
    if (!ok) {
        return {false, QObject::tr("cuteNES cannot write to %1, so it cannot replace itself. See Details opens the "
                                   "release page.")
                           .arg(native(folder))};
    }
    return {true, {}};
}

QString downloadFolder(const Target& target)
{
    return QFileInfo(target.path).absolutePath();
}

bool swapFolder(const QString& fresh, const QString& app, QString* why)
{
    const QDir appDir(app);
    // Leftovers of an earlier update that could not be removed then.
    removeOldVersion(app);
    std::vector<QString> asideDone;                 // relative paths renamed to <path>.cutenes-old
    std::vector<QString> movedIn;                   // relative paths moved from fresh into app
    const auto undo = [&] {
        for (auto it = movedIn.rbegin(); it != movedIn.rend(); ++it) {
            QDir().mkpath(QFileInfo(QDir(fresh).filePath(*it)).absolutePath());
            QFile::rename(appDir.filePath(*it), QDir(fresh).filePath(*it));
        }
        for (auto it = asideDone.rbegin(); it != asideDone.rend(); ++it) {
            QFile::rename(appDir.filePath(*it) + kOld, appDir.filePath(*it));
        }
    };
    for (const QString& file : filesUnder(app)) {
        if (!QFile::rename(appDir.filePath(file), appDir.filePath(file) + kOld)) {
            if (why) *why = QObject::tr("cannot move %1 aside").arg(native(appDir.filePath(file)));
            undo();
            return false;
        }
        asideDone.push_back(file);
    }
    for (const QString& file : filesUnder(fresh)) {
        const QString to = appDir.filePath(file);
        if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::rename(QDir(fresh).filePath(file), to)) {
            if (why) *why = QObject::tr("cannot put the new %1 in place").arg(native(to));
            undo();
            return false;
        }
        movedIn.push_back(file);
    }
    QDir(fresh).removeRecursively(); // only empty folders are left
    return true;
}

bool replaceFile(const QString& download, const QString& target, QString* why)
{
    QFile::setPermissions(download, QFile::permissions(download) | QFile::ExeOwner | QFile::ReadOwner | QFile::ExeGroup
            | QFile::ReadGroup | QFile::ExeOther | QFile::ReadOther);
    std::error_code ec;
    // POSIX rename replaces the target in one step.
    std::filesystem::rename(std::filesystem::path(download.toStdWString()), std::filesystem::path(target.toStdWString()), ec);
    if (ec) {
        if (why) *why = QObject::tr("cannot replace %1: %2").arg(native(target), QString::fromStdString(ec.message()));
        QFile::remove(download);
        return false;
    }
    return true;
}

bool installBundle(const QString& zip, const QString& bundle, QString* why)
{
    const QDir parent(QFileInfo(bundle).absolutePath());
    const QString staging = parent.filePath(kStaging);
    const auto cleanUp = [&] {
        QDir(staging).removeRecursively();
        QFile::remove(zip);
    };
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging) || !run(QStringLiteral("ditto"), {QStringLiteral("-x"), QStringLiteral("-k"), zip, staging}, why)) {
        cleanUp();
        return false;
    }
    const QString fresh = QDir(staging).filePath(QStringLiteral("cutenes.app"));
    if (!QFileInfo(fresh).isDir()) {
        if (why) *why = QObject::tr("the download has no cutenes.app");
        cleanUp();
        return false;
    }
    const QString old = parent.filePath(kOldBundle);
    QDir(old).removeRecursively();
    if (!QFile::rename(bundle, old)) {
        if (why) *why = QObject::tr("cannot move %1 aside").arg(native(bundle));
        cleanUp();
        return false;
    }
    if (!QFile::rename(fresh, bundle)) {
        QFile::rename(old, bundle);
        if (why) *why = QObject::tr("cannot put the new cutenes.app in place");
        cleanUp();
        return false;
    }
    cleanUp();
    return true;
}

bool install(const Target& target, const QString& download, QString* relaunch, QString* why)
{
    if (target.package == QLatin1String("macos-zip")) {
        if (!installBundle(download, target.path, why)) return false;
        if (relaunch) *relaunch = target.path + QStringLiteral("/Contents/MacOS/cutenes");
        return true;
    }
    if (target.package == QLatin1String("appimage")) {
        if (!replaceFile(download, target.path, why)) return false;
        if (relaunch) *relaunch = target.path;
        return true;
    }
    if (target.package == QLatin1String("windows-zip")) {
        const QString staging = QDir(downloadFolder(target)).filePath(kStaging);
        QDir(staging).removeRecursively();
        // Windows 10 and later have bsdtar, which reads zip files.
        const bool unpacked = QDir().mkpath(staging)
            && run(QStringLiteral("tar"), {QStringLiteral("-xf"), download, QStringLiteral("-C"), staging}, why);
        QFile::remove(download);
        if (!unpacked || !QFile::exists(QDir(staging).filePath(QStringLiteral("cutenes.exe")))) {
            if (unpacked && why) *why = QObject::tr("the download has no cutenes.exe");
            QDir(staging).removeRecursively();
            return false;
        }
        if (!swapFolder(staging, target.path, why)) {
            QDir(staging).removeRecursively();
            return false;
        }
        if (relaunch) *relaunch = QDir(target.path).filePath(QStringLiteral("cutenes.exe"));
        return true;
    }
    if (why) *why = QObject::tr("this build cannot replace itself");
    QFile::remove(download);
    return false;
}

void removeOldVersion(const QString& path)
{
    if (path.endsWith(QLatin1String(".app"))) {
        QDir(QFileInfo(path).absoluteDir().filePath(kOldBundle)).removeRecursively();
        return;
    }
    if (!QFileInfo(path).isDir()) return;
    for (const QString& file : filesUnder(path)) {
        // A file still loaded (the old version yet to quit) stays for next time.
        if (file.endsWith(kOld)) QFile::remove(QDir(path).filePath(file));
    }
}

} // namespace cutenes::app::update
