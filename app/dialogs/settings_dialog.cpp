#include "dialogs/settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace cutenes::app {

namespace {

const std::pair<const char*, const char*> kPages[] = {
    {"video", QT_TRANSLATE_NOOP("SettingsDialog", "Video")},
    {"audio", QT_TRANSLATE_NOOP("SettingsDialog", "Audio")},
    {"input", QT_TRANSLATE_NOOP("SettingsDialog", "Input")},
    {"hotkeys", QT_TRANSLATE_NOOP("SettingsDialog", "Hotkeys")},
    {"emulation", QT_TRANSLATE_NOOP("SettingsDialog", "Emulation")},
    {"library", QT_TRANSLATE_NOOP("SettingsDialog", "Library")},
    {"servers", QT_TRANSLATE_NOOP("SettingsDialog", "Servers")},
};

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent), m_pages(new QListWidget(this)), m_stack(new QStackedWidget(this)),
      m_libraryRoot(new QLabel(this))
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));

    for (const auto& [key, title] : kPages) {
        auto* item = new QListWidgetItem(tr(title), m_pages);
        item->setData(Qt::UserRole, QString::fromLatin1(key));
        QWidget* page = nullptr;
        if (QLatin1String(key) == QLatin1String("library")) {
            page = new QWidget(m_stack);
            const auto heading = [page](const QString& text) {
                auto* label = new QLabel(text, page);
                QFont font = label->font();
                font.setWeight(QFont::DemiBold);
                label->setFont(font);
                return label;
            };
            m_libraryRoot->setObjectName(QStringLiteral("libraryRoot"));
            m_libraryRoot->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_libraryRoot->setWordWrap(true);
            auto* change = new QPushButton(tr("Change…"), page);
            change->setObjectName(QStringLiteral("changeLibrary"));
            auto* open = new QPushButton(tr("Open Folder"), page);
            connect(change, &QPushButton::clicked, this, &SettingsDialog::changeLibraryRequested);
            connect(open, &QPushButton::clicked, this, &SettingsDialog::openLibraryFolderRequested);
            auto* locationButtons = new QHBoxLayout;
            locationButtons->addWidget(change);
            locationButtons->addWidget(open);
            locationButtons->addStretch();

            m_romFolders = new QListWidget(page);
            m_romFolders->setObjectName(QStringLiteral("romFolders"));
            m_romFolders->setMinimumHeight(110);
            m_romFolders->setTextElideMode(Qt::ElideMiddle);
            auto* addFolder = new QPushButton(tr("Add Folder…"), page);
            addFolder->setObjectName(QStringLiteral("addRomFolder"));
            m_removeFolder = new QPushButton(tr("Remove"), page);
            m_removeFolder->setObjectName(QStringLiteral("removeRomFolder"));
            m_removeFolder->setEnabled(false);
            connect(addFolder, &QPushButton::clicked, this, &SettingsDialog::addRomFolderRequested);
            connect(m_removeFolder, &QPushButton::clicked, this, [this] {
                if (const QListWidgetItem* item = m_romFolders->currentItem()) {
                    emit removeRomFolderRequested(item->data(Qt::UserRole).toString());
                }
            });
            // The library's own roms folder (row 0) cannot be removed.
            connect(m_romFolders, &QListWidget::currentRowChanged, this,
                [this](int row) { m_removeFolder->setEnabled(row > 0); });
            auto* folderButtons = new QHBoxLayout;
            folderButtons->addWidget(addFolder);
            folderButtons->addWidget(m_removeFolder);
            folderButtons->addStretch();

            auto* locationNote = new QLabel(tr("Holds your battery saves, save states, screenshots, movies, "
                                               "recordings, cheats and debug output, in visible subfolders. "
                                               "Settings are kept separately, so the library can be backed up "
                                               "or synced on its own."),
                page);
            auto* foldersNote = new QLabel(tr("The library lists the ROMs in these folders and their subfolders. "
                                              "Added folders are only read: cuteNES never writes into them."),
                page);
            for (QLabel* note : {locationNote, foldersNote}) {
                note->setWordWrap(true);
                note->setForegroundRole(QPalette::PlaceholderText);
            }

            auto* layout = new QVBoxLayout(page);
            layout->setContentsMargins(16, 4, 4, 4);
            layout->setSpacing(6);
            layout->addWidget(heading(tr("Library location")));
            layout->addWidget(m_libraryRoot);
            layout->addWidget(locationNote);
            layout->addLayout(locationButtons);
            layout->addSpacing(16);
            layout->addWidget(heading(tr("ROM folders")));
            layout->addWidget(foldersNote);
            layout->addWidget(m_romFolders, 1);
            layout->addLayout(folderButtons);
        } else {
            page = new QLabel(tr("Coming in a later update."), m_stack);
            static_cast<QLabel*>(page)->setAlignment(Qt::AlignCenter);
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        page->setObjectName(QStringLiteral("page-") + QString::fromLatin1(key));
        m_stack->addWidget(page);
    }
    m_pages->setFixedWidth(m_pages->sizeHintForColumn(0) + 32);
    connect(m_pages, &QListWidget::currentRowChanged, this, [this](int row) {
        m_stack->setCurrentIndex(row);
        emit pageShown(currentPage());
    });

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::close);
    auto* body = new QHBoxLayout;
    body->addWidget(m_pages);
    body->addWidget(m_stack, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(close);
    resize(720, 480);
    showPage(QStringLiteral("library"));
}

void SettingsDialog::showPage(const QString& page)
{
    for (int row = 0; row < m_pages->count(); ++row) {
        if (m_pages->item(row)->data(Qt::UserRole).toString() == page) {
            m_pages->setCurrentRow(row);
            return;
        }
    }
}

QString SettingsDialog::currentPage() const
{
    const QListWidgetItem* item = m_pages->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SettingsDialog::setRomFolders(const QString& roms, const QStringList& added)
{
    m_romFolders->clear();
    auto* own = new QListWidgetItem(tr("%1 (library)").arg(QDir::toNativeSeparators(roms)), m_romFolders);
    own->setData(Qt::UserRole, roms);
    own->setToolTip(tr("The library's own roms folder is always scanned."));
    for (const QString& folder : added) {
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(folder), m_romFolders);
        item->setData(Qt::UserRole, folder);
    }
    m_removeFolder->setEnabled(false);
}

void SettingsDialog::setLibraryRoot(const QString& root)
{
    m_libraryRoot->setText(QDir::toNativeSeparators(root));
}

} // namespace cutenes::app
