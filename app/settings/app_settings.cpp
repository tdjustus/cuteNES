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
const QString kTurboRate = QStringLiteral("input/turboRate");
const QString kOpposite = QStringLiteral("input/allowOppositeDirections");
const QString kPauseOnDisconnect = QStringLiteral("input/pauseOnGamepadDisconnect");
const QString kGamepad = QStringLiteral("gamepads/port%1");

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

QMap<QString, QStringList> AppSettings::bindings(const QString& group) const
{
    // One string per id, the pair split by a tab: no key's name has one.
    std::lock_guard lock(m_mutex);
    QMap<QString, QStringList> out;
    m_settings.beginGroup(group);
    for (const QString& id : m_settings.childKeys()) {
        // Every id has a dot (p1.a, gui.quick_save); the group's options
        // (input/turboRate) do not.
        if (!id.contains(QLatin1Char('.'))) continue;
        QStringList pair = m_settings.value(id).toString().split(QLatin1Char('\t'));
        while (pair.size() < 2) pair << QString();
        out.insert(id, pair.mid(0, 2));
    }
    m_settings.endGroup();
    return out;
}

void AppSettings::setBinding(const QString& group, const QString& id, const QStringList& pair)
{
    std::lock_guard lock(m_mutex);
    const QString key = group + QLatin1Char('/') + id;
    if (pair.isEmpty()) {
        m_settings.remove(key);
    } else {
        m_settings.setValue(key, pair.join(QLatin1Char('\t')));
    }
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

AppSettings::InputSettings AppSettings::input() const
{
    std::lock_guard lock(m_mutex);
    InputSettings s;
    s.turboRate = m_settings.value(kTurboRate, s.turboRate).toInt();
    s.allowOppositeDirections = m_settings.value(kOpposite, s.allowOppositeDirections).toBool();
    s.pauseOnGamepadDisconnect = m_settings.value(kPauseOnDisconnect, s.pauseOnGamepadDisconnect).toBool();
    return s;
}

void AppSettings::setInput(const InputSettings& s)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kTurboRate, s.turboRate);
    m_settings.setValue(kOpposite, s.allowOppositeDirections);
    m_settings.setValue(kPauseOnDisconnect, s.pauseOnGamepadDisconnect);
}

QString AppSettings::gamepad(int port) const
{
    std::lock_guard lock(m_mutex);
    return m_settings.value(kGamepad.arg(port)).toString();
}

void AppSettings::setGamepad(int port, const QString& guid)
{
    std::lock_guard lock(m_mutex);
    m_settings.setValue(kGamepad.arg(port), guid);
}

void AppSettings::sync()
{
    std::lock_guard lock(m_mutex);
    m_settings.sync();
}

} // namespace cutenes::app
