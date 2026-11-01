#include "settings/app_settings.hpp"

#include "library/library.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace cutenes::app {

namespace {

const QString kLibraryRoot = QStringLiteral("library/root");
const QString kRecent = QStringLiteral("recent/paths");
const QString kRomFolders = QStringLiteral("library/romFolders");
const QString kPlayed = QStringLiteral("played/");
const QString kSlot = QStringLiteral("slot/");
const QString kSettingsPage = QStringLiteral("window/settingsPage");
const QString kShowFps = QStringLiteral("video/showFps");
const QString kShowOsd = QStringLiteral("video/showOsd");

} // namespace

QString AppSettings::defaultFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("cutenes.ini"));
}

AppSettings::AppSettings(const QString& file) : m_settings(file, QSettings::IniFormat) {}

QString AppSettings::fileName() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.fileName();
}

QString AppSettings::libraryRoot() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kLibraryRoot, Library::defaultRoot()).toString();
}

void AppSettings::setLibraryRoot(const QString& root)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kLibraryRoot, QDir::cleanPath(root));
}

QStringList AppSettings::romFolders() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kRomFolders).toStringList();
}

void AppSettings::setRomFolders(const QStringList& folders)
{
    std::lock_guard lock(m_mutex);
    if (folders.isEmpty()) {
        m_settings.remove(kRomFolders);
    } else {
        m_settings.setValue(kRomFolders, folders);
    }
}

QStringList AppSettings::recent() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kRecent).toStringList();
}

void AppSettings::addRecent(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    std::lock_guard lock(m_mutex);
    QStringList list = m_settings.value(kRecent).toStringList();
    list.removeAll(absolute);
    list.prepend(absolute);
    while (list.size() > kMaxRecent) list.removeLast();
    m_settings.setValue(kRecent, list);
}

void AppSettings::clearRecent()
{
    std::lock_guard lock(m_mutex);
    m_settings.remove(kRecent);
}

QDateTime AppSettings::lastPlayed(const QString& sha1) const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kPlayed + sha1).toDateTime();
}

void AppSettings::setLastPlayed(const QString& sha1, const QDateTime& when)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kPlayed + sha1, when.toUTC());
}

int AppSettings::selectedSlot(const QString& sha1) const
{
    std::lock_guard lock(m_mutex);
    const int slot = m_settings.value(kSlot + sha1, 1).toInt();
    return slot >= 1 && slot <= 10 ? slot : 1;
}

void AppSettings::setSelectedSlot(const QString& sha1, int slot)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kSlot + sha1, slot);
}

QString AppSettings::lastSettingsPage() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kSettingsPage, QStringLiteral("library")).toString();
}

void AppSettings::setLastSettingsPage(const QString& page)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kSettingsPage, page);
}

AppSettings::ServerSettings AppSettings::servers() const
{
    std::lock_guard lock(m_mutex);
    ServerSettings s;
    s.apiEnabled = m_settings.value(QStringLiteral("servers/apiEnabled"), s.apiEnabled).toBool();
    s.apiPort = m_settings.value(QStringLiteral("servers/apiPort"), s.apiPort).toInt();
    s.mcpEnabled = m_settings.value(QStringLiteral("servers/mcpEnabled"), s.mcpEnabled).toBool();
    s.mcpPort = m_settings.value(QStringLiteral("servers/mcpPort"), s.mcpPort).toInt();
    return s;
}

void AppSettings::setServers(const ServerSettings& s)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(QStringLiteral("servers/apiEnabled"), s.apiEnabled);
    m_settings.setValue(QStringLiteral("servers/apiPort"), s.apiPort);
    m_settings.setValue(QStringLiteral("servers/mcpEnabled"), s.mcpEnabled);
    m_settings.setValue(QStringLiteral("servers/mcpPort"), s.mcpPort);
}

bool AppSettings::showFps() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kShowFps, false).toBool();
}

void AppSettings::setShowFps(bool on)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kShowFps, on);
}

bool AppSettings::showOsd() const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kShowOsd, true).toBool();
}

void AppSettings::setShowOsd(bool on)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kShowOsd, on);
}

void AppSettings::sync()
{
    std::lock_guard lock(m_mutex);
    m_settings.sync();
}

} // namespace cutenes::app
