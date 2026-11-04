#include "library/hidden_games.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace cutenes::app {

QString HiddenGames::fileIn(const QString& root)
{
    return QDir(root).filePath(QStringLiteral(".hiddenroms"));
}

HiddenGames HiddenGames::read(const QString& file, QStringList* skipped)
{
    HiddenGames hidden;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return hidden;
    static const QRegularExpression game(QStringLiteral("^([0-9A-Fa-f]{40})(?:\\s+(.*))?$"));
    int number = 0;
    while (!f.atEnd()) {
        ++number;
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#'))) {
            hidden.m_comments << line;
            continue;
        }
        const QRegularExpressionMatch m = game.match(line);
        if (!m.hasMatch()) {
            if (skipped) *skipped << QStringLiteral("line %1: %2").arg(number).arg(line);
            continue;
        }
        hidden.hide(m.captured(1).toLower(), m.captured(2).trimmed());
    }
    return hidden;
}

bool HiddenGames::write(const QString& file, QString* error) const
{
    if (m_games.isEmpty()) {
        if (!QFile::exists(file) || QFile::remove(file)) return true;
        if (error) *error = QStringLiteral("could not remove %1").arg(QDir::toNativeSeparators(file));
        return false;
    }
    QByteArray text;
    for (const QString& comment : m_comments) text += comment.toUtf8() + '\n';
    for (const HiddenGame& g : m_games) text += g.identity.toLatin1() + "  " + g.name.toUtf8() + '\n';
    // A temporary file, then a rename: a crash leaves the old list or the
    // new one, never half of one.
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly) && out.write(text) == text.size() && out.commit()) return true;
    if (error) *error = out.errorString();
    return false;
}

bool HiddenGames::contains(const QString& identity) const
{
    return std::any_of(m_games.begin(), m_games.end(), [&](const HiddenGame& g) { return g.identity == identity; });
}

bool HiddenGames::hide(const QString& identity, const QString& name)
{
    if (contains(identity)) return false;
    m_games.push_back({identity, name});
    return true;
}

bool HiddenGames::unhide(const QString& identity)
{
    const auto it = std::find_if(m_games.begin(), m_games.end(), [&](const HiddenGame& g) { return g.identity == identity; });
    if (it == m_games.end()) return false;
    m_games.erase(it);
    return true;
}

} // namespace cutenes::app
