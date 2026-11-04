#pragma once

// The games hidden from the library (#181, spec FR-007,
// contracts/library-layout.md "Hidden games"): `.hiddenroms` in the library
// folder, one `<sha1>  <display name>` line a game. cuteNES never touches a
// ROM file, so hiding is this list, read by identity (research R7) so that a
// renamed or moved ROM stays hidden. Plain data; the caller serializes use.

#include <QString>
#include <QStringList>
#include <QVector>

namespace cutenes::app {

struct HiddenGame {
    QString identity; // 40 lowercase hex digits
    QString name;     // the name it had when hidden, for people
    bool operator==(const HiddenGame&) const = default;
};

class HiddenGames {
public:
    // `.hiddenroms` in the library folder `root`.
    static QString fileIn(const QString& root);

    // A missing or unreadable file hides nothing. Lines that are not blank,
    // not `#` comments and not a game go to `skipped` ("line N: text"); a
    // second line for one identity is ignored.
    static HiddenGames read(const QString& file, QStringList* skipped = nullptr);
    // Rewrites the file atomically, the comment lines first; with no game
    // left, removes it instead.
    bool write(const QString& file, QString* error = nullptr) const;

    const QVector<HiddenGame>& games() const { return m_games; }
    bool contains(const QString& identity) const;
    // False when it already is / was not.
    bool hide(const QString& identity, const QString& name);
    bool unhide(const QString& identity);

private:
    QStringList m_comments;
    QVector<HiddenGame> m_games;
};

} // namespace cutenes::app
