#include "dialogs/settings_dialog.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSpinBox>
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
                if (const QListWidgetItem* current = m_romFolders->currentItem()) {
                    emit removeRomFolderRequested(current->data(Qt::UserRole).toString());
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
        } else if (QLatin1String(key) == QLatin1String("servers")) {
            page = buildServersPage(m_stack);
        } else if (QLatin1String(key) == QLatin1String("input")) {
            page = buildInputPage(m_stack);
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

QWidget* SettingsDialog::buildServersPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* grid = new QGridLayout;
    grid->setColumnStretch(3, 1);
    int row = 0;
    const auto addListener = [&](ServerControls& c, const QString& name, const QString& label,
                                 const char* enabledKey, const char* portKey) {
        c.enabled = new QCheckBox(label, page);
        c.enabled->setObjectName(name + QStringLiteral("Enabled"));
        c.port = new QSpinBox(page);
        c.port->setObjectName(name + QStringLiteral("Port"));
        c.port->setRange(0, 65535);
        c.port->setSpecialValueText(tr("Any free port"));
        c.port->setAccessibleName(tr("%1 port").arg(label));
        c.address = new QLabel(page);
        c.address->setObjectName(name + QStringLiteral("Address"));
        c.address->setTextInteractionFlags(Qt::TextSelectableByMouse);
        c.copy = new QPushButton(tr("Copy Token"), page);
        c.copy->setObjectName(QStringLiteral("copy") + name.at(0).toUpper() + name.mid(1) + QStringLiteral("Token"));
        connect(c.enabled, &QCheckBox::toggled, this, [this, enabledKey](bool on) {
            emit settingRequested(QString::fromLatin1(enabledKey), on);
        });
        connect(c.port, &QSpinBox::editingFinished, this, [this, &c, portKey] {
            emit settingRequested(QString::fromLatin1(portKey), c.port->value());
        });
        connect(c.copy, &QPushButton::clicked, this, [&c] { QGuiApplication::clipboard()->setText(c.token); });
        grid->addWidget(c.enabled, row, 0);
        grid->addWidget(new QLabel(tr("Port"), page), row, 1, Qt::AlignRight);
        grid->addWidget(c.port, row, 2);
        grid->addWidget(c.address, row, 3);
        grid->addWidget(c.copy, row, 4);
        ++row;
    };
    addListener(m_api, QStringLiteral("api"), tr("API"), "servers.apiEnabled", "servers.apiPort");
    addListener(m_mcp, QStringLiteral("mcp"), tr("MCP server"), "servers.mcpEnabled", "servers.mcpPort");

    m_mcpTokenFile = new QLabel(page);
    m_mcpTokenFile->setObjectName(QStringLiteral("mcpTokenFile"));
    m_mcpTokenFile->setWordWrap(true);
    m_mcpTokenFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* note = new QLabel(tr("Both are off until you turn them on, and listen on this computer only. The MCP "
                               "server needs the API, so turning it on turns the API on too. Each start makes a "
                               "new token; a client needs it to connect.\n\nTo let an AI assistant such as Claude "
                               "Code play here, turn on the MCP server and add an MCP server that runs "
                               "\"cutenes-server --attach\". It finds this window through the token file."),
        page);
    for (QLabel* label : {note, m_mcpTokenFile}) label->setForegroundRole(QPalette::PlaceholderText);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 4, 4, 4);
    layout->setSpacing(10);
    layout->addLayout(grid);
    layout->addWidget(m_mcpTokenFile);
    layout->addWidget(note);
    layout->addStretch();
    setServers({}, {});
    return page;
}

void SettingsDialog::setServers(const Server& api, const Server& mcp)
{
    const auto show = [](ServerControls& c, const Server& s) {
        const QSignalBlocker blockEnabled(c.enabled);
        const QSignalBlocker blockPort(c.port);
        c.enabled->setChecked(s.enabled);
        if (!c.port->hasFocus()) c.port->setValue(s.port);
        c.address->setText(s.running ? s.url : (s.enabled ? tr("Not running") : tr("Off")));
        c.token = s.token;
        c.copy->setEnabled(s.running);
    };
    show(m_api, api);
    show(m_mcp, mcp);
    m_mcpTokenFile->setText(mcp.running ? tr("Token file: %1").arg(mcp.tokenFile) : QString());
}

QWidget* SettingsDialog::buildInputPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    m_turboRate = new QSpinBox(page);
    m_turboRate->setObjectName(QStringLiteral("turboRate"));
    m_turboRate->setRange(2, 30);
    m_turboRate->setSuffix(tr(" presses a second"));
    m_turboRate->setAccessibleName(tr("Turbo rate"));
    connect(m_turboRate, &QSpinBox::valueChanged, this,
        [this](int rate) { emit settingRequested(QStringLiteral("input.turboRate"), rate); });
    m_opposite = new QCheckBox(tr("Allow opposite directions (Left with Right, Up with Down)"), page);
    m_opposite->setObjectName(QStringLiteral("allowOppositeDirections"));
    m_opposite->setToolTip(tr("Off, the last direction pressed wins, as on a real controller. Some games "
                              "glitch when both are held."));
    connect(m_opposite, &QCheckBox::toggled, this,
        [this](bool on) { emit settingRequested(QStringLiteral("input.allowOppositeDirections"), on); });
    m_pauseOnDisconnect = new QCheckBox(tr("Pause when a gamepad is unplugged"), page);
    m_pauseOnDisconnect->setObjectName(QStringLiteral("pauseOnGamepadDisconnect"));
    connect(m_pauseOnDisconnect, &QCheckBox::toggled, this,
        [this](bool on) { emit settingRequested(QStringLiteral("input.pauseOnGamepadDisconnect"), on); });

    m_gamepads = new QLabel(page);
    m_gamepads->setObjectName(QStringLiteral("gamepadList"));
    m_gamepads->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_gamepads->setWordWrap(true);
    auto* bindings = new QPushButton(tr("Key Bindings…"), page);
    bindings->setObjectName(QStringLiteral("inputKeyBindings"));
    connect(bindings, &QPushButton::clicked, this, &SettingsDialog::keyBindingsRequested);
    auto* note = new QLabel(tr("The first gamepad plugged in is player 1 and the second player 2; a pad "
                               "plugged back in gets its player back. Remap the keyboard and gamepad "
                               "controls, and the hotkeys, on the Key Bindings page."),
        page);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);

    auto* rate = new QHBoxLayout;
    rate->addWidget(new QLabel(tr("Turbo rate"), page));
    rate->addWidget(m_turboRate);
    rate->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(bindings);
    buttons->addStretch();
    auto* heading = new QLabel(tr("Gamepads"), page);
    QFont font = heading->font();
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 4, 4, 4);
    layout->setSpacing(10);
    layout->addLayout(rate);
    layout->addWidget(m_opposite);
    layout->addWidget(m_pauseOnDisconnect);
    layout->addSpacing(8);
    layout->addWidget(heading);
    layout->addWidget(m_gamepads);
    layout->addWidget(note);
    layout->addLayout(buttons);
    layout->addStretch();
    setInput({});
    return page;
}

void SettingsDialog::setInput(const Input& input)
{
    const QSignalBlocker blockRate(m_turboRate);
    const QSignalBlocker blockOpposite(m_opposite);
    const QSignalBlocker blockPause(m_pauseOnDisconnect);
    if (!m_turboRate->hasFocus()) m_turboRate->setValue(input.turboRate);
    m_opposite->setChecked(input.allowOppositeDirections);
    m_pauseOnDisconnect->setChecked(input.pauseOnGamepadDisconnect);
    m_gamepads->setText(input.gamepads.isEmpty() ? tr("No gamepad is plugged in.") : input.gamepads.join(QLatin1Char('\n')));
}

void SettingsDialog::setLibraryRoot(const QString& root)
{
    m_libraryRoot->setText(QDir::toNativeSeparators(root));
}

} // namespace cutenes::app
