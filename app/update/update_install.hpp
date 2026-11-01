#pragma once

// Replacing the installed version (research R16 "Replacement"; spec FR-094,
// FR-095, FR-096). Each swap keeps the old version whole until the new one
// is in place, and puts it back if a step fails; what was kept aside goes at
// the next start (removeOldVersion).
//
// General, not NES-specific: the three install shapes are the ones any
// desktop Qt application packaged per docs/packaging.md has.
// - A macOS bundle: one folder, renamed aside and the new one renamed in.
//   A running bundle can be renamed; its process keeps its open files.
// - A Windows application folder: Windows refuses to overwrite or delete a
//   running .exe or a loaded DLL, but allows renaming it. So every old file
//   is renamed aside (<name>.cutenes-old, a name Qt never loads as a
//   plugin) and each new one renamed in.
// - An AppImage: one file, renamed over the old one (POSIX rename replaces;
//   the running copy keeps its mounted image).

#include "update/update_check.hpp" // InstallSupport

#include <QString>
#include <QStringList>

namespace cutenes::app::update {

// Where this build is installed, from CUTENES_PACKAGE (set by the CI
// package job) and the running program's path.
struct Target {
    QString package; // "macos-zip", "windows-zip", "appimage" or "none"
    QString path;    // the bundle, the application folder, or the AppImage
};
Target currentTarget();

// The asset suffix a package updates from (update_feed's platformSuffix()
// for "none").
QString suffixFor(const QString& package);

// Whether `target` can be replaced: a release package, there, with write
// access where the swap happens (FR-096).
InstallSupport supportFor(const Target& target);

// The folder the download goes to: beside the install, on its file system,
// so the swap is a rename.
QString downloadFolder(const Target& target);

// Unpacks or moves the verified `download` into place, and gives the program
// to start. False with `why` leaves the installed version as it was; the
// download is removed either way.
bool install(const Target& target, const QString& download, QString* relaunch, QString* why);

// The steps, each tested on its own.
bool swapFolder(const QString& fresh, const QString& app, QString* why);
bool replaceFile(const QString& download, const QString& target, QString* why);
bool installBundle(const QString& zip, const QString& bundle, QString* why);

// What a swap kept aside: <bundle's folder>/.cutenes-old.app, or the
// *.cutenes-old files under an application folder.
void removeOldVersion(const QString& path);

} // namespace cutenes::app::update
