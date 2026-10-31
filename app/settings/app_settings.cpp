#include "settings/app_settings.hpp"

#include "library/library.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace cutenes::app {

namespace {

const QString kLibraryRoot = QStringLiteral("library/root");
const QString kRecent = QStringLiteral("recent/paths");
const QString kPlayed = QStringLiteral("played/");
const QString kSettingsPage = QStringLiteral("window/settingsPage");

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

void AppSettings::sync()
{
    std::lock_guard lock(m_mutex);
    m_settings.sync();
}

} // namespace cutenes::app
