#include "dialogs/settings_dialog.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <vector>

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
        } else if (QLatin1String(key) == QLatin1String("video")) {
            page = buildVideoPage(m_stack);
        } else if (QLatin1String(key) == QLatin1String("audio")) {
            page = buildAudioPage(m_stack);
        } else if (QLatin1String(key) == QLatin1String("emulation")) {
            page = buildEmulationPage(m_stack);
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

QWidget* SettingsDialog::buildVideoPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    VideoControls& c = m_video;
    const auto check = [&](QCheckBox*& box, const QString& text, const char* name, const char* key) {
        box = new QCheckBox(text, page);
        box->setObjectName(QString::fromLatin1(name));
        connect(box, &QCheckBox::toggled, this,
            [this, key](bool on) { emit settingRequested(QString::fromLatin1(key), on); });
    };
    const auto combo = [&](QComboBox*& box, const char* name, const char* key,
                           std::initializer_list<std::pair<QString, const char*>> items) {
        box = new QComboBox(page);
        box->setObjectName(QString::fromLatin1(name));
        for (const auto& [text, value] : items) box->addItem(text, QString::fromLatin1(value));
        connect(box, &QComboBox::currentIndexChanged, this, [this, box, key] {
            emit settingRequested(QString::fromLatin1(key), box->currentData().toString());
        });
    };

    c.scale = new QSpinBox(page);
    c.scale->setObjectName(QStringLiteral("videoScale"));
    c.scale->setRange(1, 6);
    c.scale->setSuffix(QStringLiteral("x"));
    c.scale->setAccessibleName(tr("Window scale"));
    connect(c.scale, &QSpinBox::valueChanged, this,
        [this](int scale) { emit settingRequested(QStringLiteral("video.scale"), scale); });
    check(c.fullscreen, tr("Fullscreen"), "fullscreen", "video.fullscreen");
    check(c.integerScaling, tr("Integer scaling (whole multiples only)"), "integerScaling", "video.integerScaling");
    combo(c.aspect, "aspect", "video.aspect",
        {{tr("Square pixels"), "square"}, {tr("8:7 (NTSC hardware)"), "8:7"}, {tr("4:3 stretch"), "4:3"}});
    combo(c.filter, "filter", "video.filter",
        {{tr("Nearest (sharp)"), "nearest"}, {tr("Smooth"), "smooth"}, {tr("Scanlines"), "scanlines"}});
    combo(c.pacing, "pacing", "video.pacing",
        {{tr("Automatic"), "auto"}, {tr("Sync to display"), "display"}, {tr("Sync to audio"), "audio"}});
    c.pacing->setToolTip(tr("Sync to display shows every frame exactly once when the display runs near 60 Hz "
                            "(or 50 Hz for PAL), or a whole multiple of it. Sync to audio keeps the console's "
                            "own rate on any display."));
    c.pacingNow = new QLabel(page);
    c.pacingNow->setObjectName(QStringLiteral("pacingNow"));
    c.pacingNow->setForegroundRole(QPalette::PlaceholderText);
    check(c.alwaysOnTop, tr("Always on top"), "alwaysOnTop", "video.alwaysOnTop");
    check(c.showFps, tr("Show FPS"), "showFps", "video.showFps");
    check(c.showOsd, tr("Show on-screen messages"), "showOsd", "video.showOsd");

    // The crop: each edge in NES pixels.
    auto* crop = new QGridLayout;
    const char* edges[4] = {"top", "bottom", "left", "right"};
    const QString names[4] = {tr("Top"), tr("Bottom"), tr("Left"), tr("Right")};
    for (int i = 0; i < 4; ++i) {
        QSpinBox*& box = c.overscan[i];
        box = new QSpinBox(page);
        QString name = QString::fromLatin1(edges[i]);
        name[0] = name[0].toUpper();
        box->setObjectName(QStringLiteral("overscan") + name);
        box->setRange(0, 16);
        box->setSuffix(tr(" px"));
        box->setAccessibleName(tr("Overscan %1").arg(names[i]));
        const char* edge = edges[i];
        connect(box, &QSpinBox::valueChanged, this, [this, edge](int value) {
            emit settingRequested(QStringLiteral("video.overscan"), QVariantMap{{QString::fromLatin1(edge), value}});
        });
        crop->addWidget(new QLabel(names[i], page), i / 2, (i % 2) * 2, Qt::AlignRight);
        crop->addWidget(box, i / 2, (i % 2) * 2 + 1);
    }
    crop->setColumnStretch(4, 1);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("Window scale"), c.scale);
    form->addRow(tr("Aspect ratio"), c.aspect);
    form->addRow(tr("Filter"), c.filter);
    form->addRow(tr("Frame pacing"), c.pacing);
    form->addRow(QString(), c.pacingNow);
    form->addRow(tr("Overscan"), crop);
    auto* note = new QLabel(tr("Overscan hides the picture's edges, as TVs did: 8 lines at the top and bottom by "
                               "default. Most games put nothing there."),
        page);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 4, 4, 4);
    layout->setSpacing(8);
    layout->addLayout(form);
    layout->addWidget(note);
    for (QCheckBox* box : {c.integerScaling, c.fullscreen, c.alwaysOnTop, c.showFps, c.showOsd}) layout->addWidget(box);
    layout->addStretch();
    setVideo({});
    return page;
}

void SettingsDialog::setVideo(const Video& v)
{
    VideoControls& c = m_video;
    std::vector<QSignalBlocker> blockers;
    blockers.reserve(13);
    for (QWidget* w : std::initializer_list<QWidget*>{c.scale, c.fullscreen, c.integerScaling, c.aspect, c.filter,
             c.pacing, c.alwaysOnTop, c.showFps, c.showOsd, c.overscan[0], c.overscan[1], c.overscan[2], c.overscan[3]}) {
        blockers.emplace_back(w);
    }
    if (!c.scale->hasFocus()) c.scale->setValue(v.scale);
    c.fullscreen->setChecked(v.fullscreen);
    c.integerScaling->setChecked(v.integerScaling);
    c.aspect->setCurrentIndex(qMax(0, c.aspect->findData(v.aspect)));
    c.filter->setCurrentIndex(qMax(0, c.filter->findData(v.filter)));
    c.pacing->setCurrentIndex(qMax(0, c.pacing->findData(v.pacing)));
    c.pacingNow->setText(v.pacingNow);
    for (int i = 0; i < 4; ++i) {
        if (!c.overscan[i]->hasFocus()) c.overscan[i]->setValue(v.overscan[i]);
    }
    c.alwaysOnTop->setChecked(v.alwaysOnTop);
    c.showFps->setChecked(v.showFps);
    c.showOsd->setChecked(v.showOsd);
}

QWidget* SettingsDialog::buildAudioPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    m_device = new QComboBox(page);
    m_device->setObjectName(QStringLiteral("audioDevice"));
    m_device->setAccessibleName(tr("Output device"));
    connect(m_device, &QComboBox::activated, this,
        [this] { emit settingRequested(QStringLiteral("audio.device"), m_device->currentData().toString()); });
    m_volume = new QSlider(Qt::Horizontal, page);
    m_volume->setObjectName(QStringLiteral("volume"));
    m_volume->setRange(0, 100);
    m_volume->setAccessibleName(tr("Volume"));
    connect(m_volume, &QSlider::valueChanged, this,
        [this](int volume) { emit settingRequested(QStringLiteral("audio.volume"), volume); });
    m_muted = new QCheckBox(tr("Mute"), page);
    m_muted->setObjectName(QStringLiteral("muted"));
    connect(m_muted, &QCheckBox::toggled, this,
        [this](bool on) { emit settingRequested(QStringLiteral("audio.muted"), on); });
    m_latency = new QSpinBox(page);
    m_latency->setObjectName(QStringLiteral("latencyMs"));
    m_latency->setRange(32, 200);
    m_latency->setSuffix(tr(" ms"));
    m_latency->setAccessibleName(tr("Latency"));
    connect(m_latency, &QSpinBox::valueChanged, this,
        [this](int ms) { emit settingRequested(QStringLiteral("audio.latencyMs"), ms); });
    m_ffAudio = new QComboBox(page);
    m_ffAudio->setObjectName(QStringLiteral("fastForwardAudio"));
    m_ffAudio->addItem(tr("Speed up (up to 2x, then mute)"), QStringLiteral("stretch"));
    m_ffAudio->addItem(tr("Mute"), QStringLiteral("mute"));
    connect(m_ffAudio, &QComboBox::currentIndexChanged, this, [this] {
        emit settingRequested(QStringLiteral("audio.fastForwardAudio"), m_ffAudio->currentData().toString());
    });
    auto* volume = new QHBoxLayout;
    volume->addWidget(m_volume, 1);
    volume->addWidget(m_muted);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("Output device"), m_device);
    form->addRow(tr("Volume"), volume);
    form->addRow(tr("Latency"), m_latency);
    form->addRow(tr("Fast-forward sound"), m_ffAudio);
    auto* note = new QLabel(tr("Lower latency answers sooner but may crackle on a busy computer. If the device is "
                               "unplugged, sound moves to the default device."),
        page);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 4, 4, 4);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch();
    setAudio({});
    return page;
}

void SettingsDialog::setAudio(const Audio& a)
{
    const QSignalBlocker b1(m_device), b2(m_volume), b3(m_muted), b4(m_latency), b5(m_ffAudio);
    m_device->clear();
    m_device->addItem(tr("Default device"), QStringLiteral("default"));
    for (const QString& name : a.devices) m_device->addItem(name, name);
    if (m_device->findData(a.device) < 0) m_device->addItem(tr("%1 (not connected)").arg(a.device), a.device);
    m_device->setCurrentIndex(m_device->findData(a.device));
    if (!m_volume->isSliderDown()) m_volume->setValue(a.volume);
    m_muted->setChecked(a.muted);
    if (!m_latency->hasFocus()) m_latency->setValue(a.latencyMs);
    m_ffAudio->setCurrentIndex(qMax(0, m_ffAudio->findData(a.fastForwardAudio)));
}

QWidget* SettingsDialog::buildEmulationPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    m_pauseInactive = new QCheckBox(tr("Pause when cuteNES is in the background"), page);
    m_pauseInactive->setObjectName(QStringLiteral("pauseWhenInactive"));
    connect(m_pauseInactive, &QCheckBox::toggled, this,
        [this](bool on) { emit settingRequested(QStringLiteral("emulation.pauseWhenInactive"), on); });
    m_resumeOnOpen = new QCheckBox(tr("Resume games where I left off"), page);
    m_resumeOnOpen->setObjectName(QStringLiteral("resumeOnOpen"));
    m_resumeOnOpen->setToolTip(tr("Closing a game keeps its state, and opening it again offers to go on from there."));
    connect(m_resumeOnOpen, &QCheckBox::toggled, this,
        [this](bool on) { emit settingRequested(QStringLiteral("emulation.resumeOnOpen"), on); });
    m_ffSpeed = new QComboBox(page);
    m_ffSpeed->setObjectName(QStringLiteral("fastForwardSpeed"));
    for (const int percent : {150, 200, 300, 400}) m_ffSpeed->addItem(tr("%1 %").arg(percent), percent);
    m_ffSpeed->setAccessibleName(tr("Fast-forward speed"));
    connect(m_ffSpeed, &QComboBox::currentIndexChanged, this, [this] {
        emit settingRequested(QStringLiteral("emulation.fastForwardSpeed"), m_ffSpeed->currentData().toInt());
    });
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("Fast-forward speed"), m_ffSpeed);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 4, 4, 4);
    layout->addWidget(m_pauseInactive);
    layout->addWidget(m_resumeOnOpen);
    layout->addLayout(form);
    layout->addStretch();
    setEmulation({});
    return page;
}

void SettingsDialog::setEmulation(const Emulation& e)
{
    const QSignalBlocker b1(m_pauseInactive), b2(m_resumeOnOpen), b3(m_ffSpeed);
    m_pauseInactive->setChecked(e.pauseWhenInactive);
    m_resumeOnOpen->setChecked(e.resumeOnOpen);
    m_ffSpeed->setCurrentIndex(qMax(0, m_ffSpeed->findData(e.fastForwardSpeed)));
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
