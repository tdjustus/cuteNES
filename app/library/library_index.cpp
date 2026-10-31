#include "library/library_index.hpp"

#include "settings/app_settings.hpp"

#include <cutenes/machine.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace cutenes::app {

namespace {

constexpr int kCacheVersion = 1;
// The largest ROM worth reading (the API's upload limit is 8 MiB).
constexpr qint64 kMaxRomSize = 8 * 1024 * 1024;

QString regionName(Region region)
{
    switch (region) {
    case Region::Ntsc: return QStringLiteral("NTSC");
    case Region::Pal: return QStringLiteral("PAL");
    case Region::Unspecified: break;
    }
    return QStringLiteral("Unspecified");
}

QString rejection(const LoadResult& r)
{
    switch (r.error) {
    case LoadError::Ok: return {};
    case LoadError::NotAnInesFile: return QObject::tr("Not an iNES file");
    case LoadError::TruncatedFile: return QObject::tr("The file is truncated");
    case LoadError::InvalidHeader: return QObject::tr("Invalid header");
    case LoadError::InvalidBankCounts: return QObject::tr("Invalid ROM bank counts");
    case LoadError::UnsupportedMapper: return QObject::tr("Unsupported mapper %1").arg(r.detail);
    case LoadError::Nes2FieldInvalid: return QObject::tr("Invalid NES 2.0 field");
    }
    return QObject::tr("Rejected");
}

bool isRom(const QFileInfo& info)
{
    return info.isFile() && info.suffix().compare(QLatin1String("nes"), Qt::CaseInsensitive) == 0;
}

QJsonObject toJson(const LibraryEntry& e)
{
    return {
        {QStringLiteral("path"), e.relativePath.isEmpty() ? e.path : e.relativePath},
        {QStringLiteral("absolute"), e.relativePath.isEmpty()},
        {QStringLiteral("size"), e.size},
        {QStringLiteral("mtime"), e.mtime},
        {QStringLiteral("identity"), e.identity},
        {QStringLiteral("name"), e.name},
        {QStringLiteral("mapper"), e.mapper},
        {QStringLiteral("submapper"), e.submapper},
        {QStringLiteral("region"), e.region},
        {QStringLiteral("battery"), e.battery},
        {QStringLiteral("supported"), e.supported},
        {QStringLiteral("reason"), e.reason},
    };
}

LibraryEntry fromJson(const QJsonObject& o, const QString& romsDir)
{
    LibraryEntry e;
    const QString path = o.value(QStringLiteral("path")).toString();
    if (o.value(QStringLiteral("absolute")).toBool()) {
        e.path = path;
    } else {
        e.relativePath = path;
        e.path = QDir(romsDir).filePath(path);
    }
    e.size = o.value(QStringLiteral("size")).toInteger();
    e.mtime = o.value(QStringLiteral("mtime")).toInteger();
    e.identity = o.value(QStringLiteral("identity")).toString();
    e.name = o.value(QStringLiteral("name")).toString();
    e.mapper = o.value(QStringLiteral("mapper")).toInt(-1);
    e.submapper = o.value(QStringLiteral("submapper")).toInt();
    e.region = o.value(QStringLiteral("region")).toString();
    e.battery = o.value(QStringLiteral("battery")).toBool();
    e.supported = o.value(QStringLiteral("supported")).toBool();
    e.reason = o.value(QStringLiteral("reason")).toString();
    return e;
}

// Short identity -> per-game entry in `dir` (files with `suffix`, or folders).
QHash<QString, QFileInfo> taggedEntries(const QString& dir, bool folders)
{
    static const QRegularExpression tag(QStringLiteral("\\[([0-9a-f]{8})(?:-\\d+)?\\](\\.[^.]+)?$"));
    QHash<QString, QFileInfo> found;
    const auto filter = folders ? QDir::Dirs | QDir::NoDotAndDotDot : QDir::Files;
    for (const QFileInfo& entry : QDir(dir).entryInfoList(filter)) {
        const auto match = tag.match(entry.fileName());
        if (!match.hasMatch()) continue;
        if (!folders && match.captured(2) != QLatin1String(".sav")) continue;
        found.insert(match.captured(1), entry);
    }
    return found;
}

} // namespace

LibraryEntry describeRom(const QString& path, const QString& romsDir)
{
    const QFileInfo info(path);
    LibraryEntry e;
    e.path = info.absoluteFilePath();
    if (!romsDir.isEmpty()) {
        const QString relative = QDir(romsDir).relativeFilePath(e.path);
        if (!relative.startsWith(QLatin1String(".."))) e.relativePath = relative;
    }
    e.size = info.size();
    e.mtime = info.lastModified().toMSecsSinceEpoch();
    e.name = info.completeBaseName();
    if (e.size > kMaxRomSize) {
        e.reason = QObject::tr("The file is too large");
        return e;
    }
    QFile file(e.path);
    if (!file.open(QIODevice::ReadOnly)) {
        e.reason = file.errorString();
        return e;
    }
    const QByteArray bytes = file.readAll();
    if (const auto id = identifyRom(bytes)) e.identity = id->sha1;
    // The core's verdict, from a throwaway machine (FR-006).
    Machine scratch;
    const LoadResult result = scratch.loadCartridge(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    e.supported = result.error == LoadError::Ok;
    e.reason = rejection(result);
    if (result.error == LoadError::Ok || result.error == LoadError::UnsupportedMapper) {
        e.mapper = result.error == LoadError::Ok ? result.info.mapperId : result.detail;
        e.submapper = result.info.submapper;
        e.battery = result.info.battery;
        e.region = regionName(result.info.region);
    }
    return e;
}

QVector<LibraryEntry> scanRoms(const QString& romsDir, const QStringList& extra,
    const QHash<QString, LibraryEntry>& previous)
{
    QVector<LibraryEntry> entries;
    QSet<QString> seen;
    const auto add = [&](const QFileInfo& info) {
        const QString path = info.absoluteFilePath();
        if (seen.contains(path)) return;
        seen.insert(path);
        const auto old = previous.constFind(path);
        if (old != previous.constEnd() && old->size == info.size()
            && old->mtime == info.lastModified().toMSecsSinceEpoch()) {
            entries.append(*old);
        } else {
            entries.append(describeRom(path, romsDir));
        }
    };
    if (!romsDir.isEmpty()) {
        QDirIterator it(romsDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info = it.nextFileInfo();
            if (isRom(info)) add(info);
        }
    }
    for (const QString& path : extra) {
        const QFileInfo info(path);
        if (info.isFile()) add(info);
    }
    return entries;
}

void annotate(QVector<LibraryEntry>& entries, const Library& library, const AppSettings& settings)
{
    const auto saves = taggedEntries(library.folder(Library::Folder::Saves), false);
    const auto states = taggedEntries(library.folder(Library::Folder::States), true);
    QSet<QString> recent;
    for (const QString& path : settings.recent()) recent.insert(QFileInfo(path).absoluteFilePath());
    for (LibraryEntry& e : entries) {
        const QString id8 = e.identity.left(8);
        e.hasSave = !e.identity.isEmpty() && saves.contains(id8);
        e.stateCount = 0;
        if (!e.identity.isEmpty() && states.contains(id8)) {
            e.stateCount = static_cast<int>(QDir(states.value(id8).filePath())
                    .entryList({QStringLiteral("slot-*.state")}, QDir::Files)
                    .size());
        }
        e.lastPlayed = e.identity.isEmpty() ? QDateTime() : settings.lastPlayed(e.identity);
        e.recent = recent.contains(e.path);
    }
}

bool writeIndexCache(const QString& file, const QString& root, const QVector<LibraryEntry>& entries)
{
    QJsonArray array;
    for (const auto& e : entries) array.append(toJson(e));
    const QJsonObject doc{{QStringLiteral("version"), kCacheVersion}, {QStringLiteral("root"), root},
        {QStringLiteral("entries"), array}};
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    return out.commit();
}

QVector<LibraryEntry> readIndexCache(const QString& file, QString* root)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return {};
    const QJsonObject doc = QJsonDocument::fromJson(in.readAll()).object();
    if (doc.value(QStringLiteral("version")).toInt() != kCacheVersion) return {};
    const QString cachedRoot = doc.value(QStringLiteral("root")).toString();
    if (root) *root = cachedRoot;
    const QString romsDir = Library(cachedRoot).folder(Library::Folder::Roms);
    QVector<LibraryEntry> entries;
    for (const QJsonValue& v : doc.value(QStringLiteral("entries")).toArray()) {
        entries.append(fromJson(v.toObject(), romsDir));
    }
    return entries;
}

QString LibraryIndex::defaultCacheFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("library-index.json"));
}

LibraryIndex::LibraryIndex(QString cacheFile, const AppSettings& settings, QObject* parent)
    : QObject(parent), m_cacheFile(std::move(cacheFile)), m_settings(settings)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300); // a copy of many files is one rescan
    connect(&m_debounce, &QTimer::timeout, this, &LibraryIndex::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_scan, &QFutureWatcher<QVector<LibraryEntry>>::finished, this, &LibraryIndex::onScanFinished);
}

LibraryIndex::~LibraryIndex()
{
    m_scan.waitForFinished();
}

void LibraryIndex::setLibrary(const Library& library, bool scan)
{
    m_library = library;
    QString cachedRoot;
    QVector<LibraryEntry> cached = readIndexCache(m_cacheFile, &cachedRoot);
    if (QDir::cleanPath(cachedRoot) != library.root()) cached.clear();
    publish(std::move(cached));
    watchFolders();
    if (scan) rescan();
}

QVector<LibraryEntry> LibraryIndex::entries() const
{
    std::lock_guard lock(m_mutex);
    return m_entries;
}

void LibraryIndex::rescan()
{
    if (!m_library) return;
    if (m_scan.isRunning()) {
        m_rescanPending = true;
        return;
    }
    QHash<QString, LibraryEntry> previous;
    for (const auto& e : entries()) previous.insert(e.path, e);
    m_scan.setFuture(QtConcurrent::run(scanRoms, m_library->folder(Library::Folder::Roms),
        m_settings.recent(), previous));
}

void LibraryIndex::onScanFinished()
{
    QVector<LibraryEntry> scanned = m_scan.result();
    if (m_library) writeIndexCache(m_cacheFile, m_library->root(), scanned);
    publish(std::move(scanned));
    watchFolders(); // new subfolders
    if (m_rescanPending) {
        m_rescanPending = false;
        rescan();
    }
}

void LibraryIndex::refreshAnnotations()
{
    publish(entries());
}

void LibraryIndex::publish(QVector<LibraryEntry> list)
{
    if (m_library) annotate(list, *m_library, m_settings);
    std::sort(list.begin(), list.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    {
        std::lock_guard lock(m_mutex);
        m_entries = std::move(list);
    }
    emit entriesChanged();
}

void LibraryIndex::watchFolders()
{
    QStringList folders;
    if (m_library) {
        const QString roms = m_library->folder(Library::Folder::Roms);
        if (QFileInfo(roms).isDir()) {
            folders << roms;
            QDirIterator it(roms, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) folders << it.next();
        }
    }
    // Only the difference: on macOS, removing a path and adding it again
    // at once leaves it unwatched.
    QStringList gone;
    for (const QString& dir : m_watcher.directories()) {
        if (!folders.contains(dir)) gone << dir;
    }
    if (!gone.isEmpty()) m_watcher.removePaths(gone);
    QStringList added;
    for (const QString& dir : folders) {
        if (!m_watcher.directories().contains(dir)) added << dir;
    }
    if (!added.isEmpty()) m_watcher.addPaths(added);
}

} // namespace cutenes::app
