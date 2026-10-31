#include "library/game_identity.hpp"

#include <QCryptographicHash>

namespace cutenes::app {

namespace {

constexpr qsizetype kHeaderSize = 16;
constexpr qsizetype kTrainerSize = 512;
constexpr qsizetype kMaxNameLength = 80;

} // namespace

std::optional<GameIdentity> identifyRom(const QByteArray& file)
{
    if (file.size() <= kHeaderSize || !file.startsWith(QByteArray("NES\x1A", 4))) {
        return std::nullopt;
    }
    const bool trainer = (static_cast<unsigned char>(file[6]) & 0x04) != 0;
    const qsizetype start = kHeaderSize + (trainer ? kTrainerSize : 0);
    if (file.size() <= start) {
        return std::nullopt;
    }
    const QByteArray digest = QCryptographicHash::hash(
        QByteArrayView(file).sliced(start), QCryptographicHash::Sha1);
    return GameIdentity{QString::fromLatin1(digest.toHex())};
}

QString sanitizeDisplayName(const QString& stem)
{
    static const QString kForbidden = QStringLiteral("<>:\"/\\|?*");
    QString name = stem.trimmed();
    for (QChar& c : name) {
        if (c.category() == QChar::Other_Control || kForbidden.contains(c)) {
            c = QLatin1Char('_');
        }
    }
    return name.left(kMaxNameLength).trimmed();
}

QString gameBaseName(const QString& displayName, const GameIdentity& id, int collision)
{
    const QString tag = collision <= 1 ? id.id8() : QStringLiteral("%1-%2").arg(id.id8()).arg(collision);
    return QStringLiteral("%1 [%2]").arg(displayName, tag);
}

} // namespace cutenes::app
