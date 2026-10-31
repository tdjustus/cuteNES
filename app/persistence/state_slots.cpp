#include "persistence/state_slots.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace cutenes::app {

namespace {

QString slotStem(int slot)
{
    return QStringLiteral("slot-%1").arg(slot, 2, 10, QLatin1Char('0'));
}

} // namespace

StateSlots::StateSlots(const Library& library, const GameIdentity& id, const QString& displayName)
    : m_dir(library.gameFile(Library::Folder::States, id, displayName, {})), m_id(id), m_library(library)
{
}

StateSlots::Info StateSlots::info(int slot) const
{
    Info info;
    info.slot = slot;
    if (!isOpen() || !isValidSlot(slot)) return info;
    const QDir dir(m_dir);
    info.statePath = dir.filePath(slotStem(slot) + QStringLiteral(".state"));
    info.thumbnailPath = dir.filePath(slotStem(slot) + QStringLiteral(".png"));
    const QFileInfo file(info.statePath);
    if (file.isFile()) {
        info.empty = false;
        info.savedAt = file.lastModified();
        info.size = file.size();
    }
    return info;
}

bool StateSlots::save(int slot, const QByteArray& state, const QImage& thumbnail, QString* error)
{
    if (!isOpen() || !isValidSlot(slot)) {
        if (error) *error = QObject::tr("No game to save a state for.");
        return false;
    }
    if (!QDir().mkpath(m_dir)) {
        if (error) *error = QObject::tr("Cannot create %1.").arg(QDir::toNativeSeparators(m_dir));
        return false;
    }
    m_library.recordIdentity(Library::Folder::States, m_dir, m_id);
    const Info target = info(slot);
    // The thumbnail first: a slot whose state is new always has its picture.
    if (!thumbnail.isNull()) {
        QSaveFile png(target.thumbnailPath);
        if (!png.open(QIODevice::WriteOnly) || !thumbnail.save(&png, "PNG") || !png.commit()) {
            if (error) *error = png.errorString();
            return false;
        }
    }
    return writeFile(target.statePath, state, error);
}

std::optional<QByteArray> StateSlots::load(int slot, QString* error) const
{
    const Info target = info(slot);
    if (target.empty) {
        if (error) *error = QObject::tr("Slot %1 is empty.").arg(slot);
        return std::nullopt;
    }
    return readFile(target.statePath, error);
}

bool StateSlots::writeFile(const QString& path, const QByteArray& state, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(state) != state.size() || !file.commit()) {
        if (error) *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

std::optional<QByteArray> StateSlots::readFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Cannot read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return std::nullopt;
    }
    return file.readAll();
}

} // namespace cutenes::app
