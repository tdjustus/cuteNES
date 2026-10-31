#include "library/library.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace cutenes::app {

namespace {

constexpr auto kIdentities = "identities.json";
constexpr auto kIdentityFile = "identity.txt";

const char* subfolder(Library::Folder folder)
{
    switch (folder) {
    case Library::Folder::Roms: return "roms";
    case Library::Folder::Saves: return "saves";
    case Library::Folder::States: return "states";
    case Library::Folder::Screenshots: return "screenshots";
    case Library::Folder::Movies: return "movies";
    case Library::Folder::Recordings: return "recordings";
    case Library::Folder::Cheats: return "cheats";
    case Library::Folder::Debug: return "debug";
    }
    return "";
}

constexpr Library::Folder kAllFolders[] = {
    Library::Folder::Roms, Library::Folder::Saves, Library::Folder::States,
    Library::Folder::Screenshots, Library::Folder::Movies, Library::Folder::Recordings,
    Library::Folder::Cheats, Library::Folder::Debug,
};

const char* kRomsReadme =
    "Put your .nes ROM files here, in any subfolders you like.\n"
    "cuteNES lists them in its library browser (File > Game Library) and\n"
    "picks up new files without a restart.\n"
    "\n"
    "ROMs can also be opened from anywhere with File > Open ROM; their\n"
    "saves and states are still kept in this library, matched to the game\n"
    "by its contents, so renaming or moving a ROM keeps its saves.\n";

QJsonObject readIdentities(const QString& dir)
{
    QFile file(QDir(dir).filePath(QLatin1String(kIdentities)));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// The recorded identity of an entry, or empty when none is recorded.
QString recordedIdentity(const QFileInfo& entry, const QJsonObject& identities)
{
    if (entry.isDir()) {
        QFile file(QDir(entry.filePath()).filePath(QLatin1String(kIdentityFile)));
        return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed() : QString();
    }
    return identities.value(entry.fileName()).toString();
}

struct Candidate {
    QFileInfo entry;
    int collision; // 1 for "[id8]", n for "[id8-n]"
};

// Entries in `dir` whose name (without `suffix`) ends with the game's tag.
QList<Candidate> candidates(const QString& dir, const GameIdentity& id, const QString& suffix)
{
    const QRegularExpression tag(QStringLiteral("\\[%1(?:-(\\d+))?\\]%2$")
            .arg(id.id8(), QRegularExpression::escape(suffix)));
    QList<Candidate> found;
    const auto filter = suffix.isEmpty() ? QDir::Dirs | QDir::NoDotAndDotDot : QDir::Files;
    for (const QFileInfo& entry : QDir(dir).entryInfoList(filter, QDir::Name)) {
        const auto match = tag.match(entry.fileName());
        if (match.hasMatch()) {
            found.append({entry, match.captured(1).isEmpty() ? 1 : match.captured(1).toInt()});
        }
    }
    return found;
}

// Copies `from` into `to` recursively, skipping files `to` already has;
// identities.json files are merged.
bool copyTree(const QString& from, const QString& to)
{
    if (!QDir().mkpath(to)) return false;
    bool ok = true;
    for (const QFileInfo& entry : QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            ok = copyTree(entry.filePath(), target) && ok;
        } else if (entry.fileName() == QLatin1String(kIdentities) && QFile::exists(target)) {
            QJsonObject merged = readIdentities(to);
            const QJsonObject theirs = readIdentities(from);
            for (auto it = theirs.begin(); it != theirs.end(); ++it) {
                if (!merged.contains(it.key())) merged.insert(it.key(), it.value());
            }
            QSaveFile file(target);
            ok = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(merged).toJson()) >= 0
                && file.commit() && ok;
        } else if (!QFile::exists(target)) {
            ok = QFile::copy(entry.filePath(), target) && ok;
        }
    }
    return ok;
}

} // namespace

QString Library::defaultRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("cuteNES"));
}

Library::Library(QString root) : m_root(QDir::cleanPath(std::move(root))) {}

QString Library::folder(Folder f) const
{
    return QDir(m_root).filePath(QLatin1String(subfolder(f)));
}

bool Library::ensureLayout(QString* error) const
{
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    for (Folder f : kAllFolders) {
        if (!QDir().mkpath(folder(f))) {
            return fail(QObject::tr("Cannot create the folder %1.").arg(QDir::toNativeSeparators(folder(f))));
        }
    }
    const QString readme = QDir(folder(Folder::Roms)).filePath(QStringLiteral("README.txt"));
    if (!QFile::exists(readme)) {
        QSaveFile file(readme);
        if (!file.open(QIODevice::WriteOnly) || file.write(kRomsReadme) < 0 || !file.commit()) {
            return fail(QObject::tr("Cannot write to %1.").arg(QDir::toNativeSeparators(m_root)));
        }
    }
    if (!isUsable()) {
        return fail(QObject::tr("%1 is not writable.").arg(QDir::toNativeSeparators(m_root)));
    }
    return true;
}

bool Library::isUsable() const
{
    const QFileInfo info(m_root);
    return info.isDir() && info.isWritable();
}

QString Library::findGameFile(Folder f, const GameIdentity& id, const QString& suffix) const
{
    const QString dir = folder(f);
    const QJsonObject identities = readIdentities(dir);
    QString unrecorded;
    for (const Candidate& c : candidates(dir, id, suffix)) {
        const QString recorded = recordedIdentity(c.entry, identities);
        if (recorded == id.sha1) return c.entry.filePath();
        // A file copied in by hand has no record; trust its tag, but only
        // after every recorded match has had its chance.
        if (recorded.isEmpty() && unrecorded.isEmpty()) unrecorded = c.entry.filePath();
    }
    return unrecorded;
}

QString Library::gameFile(Folder f, const GameIdentity& id, const QString& displayName,
    const QString& suffix) const
{
    if (QString existing = findGameFile(f, id, suffix); !existing.isEmpty()) return existing;
    // Every tag in use belongs to another game: take the next free number.
    int collision = 1;
    for (const Candidate& c : candidates(folder(f), id, suffix)) collision = std::max(collision, c.collision + 1);
    QString name = sanitizeDisplayName(displayName);
    if (name.isEmpty()) name = QStringLiteral("Game");
    return QDir(folder(f)).filePath(gameBaseName(name, id, collision) + suffix);
}

bool Library::moveContentsTo(const Library& target, QString* error) const
{
    if (target.root() == m_root) return true;
    if (!QDir().mkpath(target.root())) {
        if (error) *error = QObject::tr("Cannot create %1.").arg(QDir::toNativeSeparators(target.root()));
        return false;
    }
    QStringList failed;
    for (Folder f : kAllFolders) {
        const QString from = folder(f);
        const QString to = target.folder(f);
        if (!QFileInfo(from).isDir()) continue;
        if (!QFileInfo::exists(to) && QDir().rename(from, to)) continue;
        if (!copyTree(from, to)) failed << QDir::toNativeSeparators(from);
    }
    if (!failed.isEmpty()) {
        if (error) *error = QObject::tr("Some files could not be copied from %1.").arg(failed.join(QStringLiteral(", ")));
        return false;
    }
    return true;
}

void Library::recordIdentity(Folder f, const QString& path, const GameIdentity& id) const
{
    const QFileInfo entry(path);
    if (entry.isDir()) {
        QSaveFile file(QDir(path).filePath(QLatin1String(kIdentityFile)));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(id.sha1.toLatin1() + '\n');
            file.commit();
        }
        return;
    }
    const QString dir = folder(f);
    QJsonObject identities = readIdentities(dir);
    if (identities.value(entry.fileName()).toString() == id.sha1) return;
    identities.insert(entry.fileName(), id.sha1);
    QSaveFile file(QDir(dir).filePath(QLatin1String(kIdentities)));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(identities).toJson());
        file.commit();
    }
}

} // namespace cutenes::app
