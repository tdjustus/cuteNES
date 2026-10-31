#include "dialogs/settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
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
            m_libraryRoot->setObjectName(QStringLiteral("libraryRoot"));
            m_libraryRoot->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_libraryRoot->setWordWrap(true);
            auto* change = new QPushButton(tr("Change…"), page);
            change->setObjectName(QStringLiteral("changeLibrary"));
            auto* open = new QPushButton(tr("Open Folder"), page);
            connect(change, &QPushButton::clicked, this, &SettingsDialog::changeLibraryRequested);
            connect(open, &QPushButton::clicked, this, &SettingsDialog::openLibraryFolderRequested);
            auto* buttons = new QHBoxLayout;
            buttons->addWidget(change);
            buttons->addWidget(open);
            buttons->addStretch();
            auto* form = new QFormLayout;
            form->addRow(tr("Location:"), m_libraryRoot);
            form->addRow(QString(), buttons);
            auto* note = new QLabel(tr("The library holds your ROMs, battery saves, save states, screenshots, "
                                       "movies, recordings, cheats and debug output, in visible subfolders. "
                                       "Settings are kept separately, so the library can be backed up or "
                                       "synced on its own."),
                page);
            note->setWordWrap(true);
            auto* layout = new QVBoxLayout(page);
            layout->addLayout(form);
            layout->addWidget(note);
            layout->addStretch();
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
    resize(640, 360);
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

void SettingsDialog::setLibraryRoot(const QString& root)
{
    m_libraryRoot->setText(QDir::toNativeSeparators(root));
}

} // namespace cutenes::app
