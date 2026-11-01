#include "mainwindow.hpp"

#include "about_dialog.hpp"
#include "dialogs/cheats_window.hpp"
#include "dialogs/key_bindings_page.hpp"
#include "dialogs/settings_dialog.hpp"
#include "library/library_browser.hpp"
#include "library/library_index.hpp"
#include "media/palettes.hpp"

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/emulation_view.hpp>
#include <cutenes/qt/frame_clock.hpp>

#include <QAction>
#include <QtMath>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QTimer>
#include <QScreen>
#include <QWindow>
#include <QLocale>
#include <QIcon>
#include <QPixmap>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include "host/servers.hpp"
#include "settings/key_bindings.hpp"

#include <cutenes/api/token.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>

namespace cutenes::app {

namespace {

// The default window scale (data-model "Settings": scale 3).
constexpr int kDefaultScale = 3;

// The menu inventory of contracts/menus-and-commands.md. Every action
// carries its gui.* command and arguments (research R11), and GuiHost wires
// them to the session from G1 on. An action stays disabled until its phase
// implements it (tasks G034).
class MenuBuilder {
public:
    explicit MenuBuilder(QMenu* menu) : m_menu(menu) {}

    QAction* item(const QString& text, const char* command, const QKeySequence& shortcut = {},
        const QVariantMap& args = {})
    {
        QAction* action = m_menu->addAction(text);
        action->setObjectName(QString::fromLatin1(command));
        action->setProperty("command", QString::fromLatin1(command));
        action->setProperty("commandArgs", args);
        action->setShortcut(shortcut);
        // Only the actions given a role on purpose move to the macOS
        // application menu; Qt would otherwise guess from the text.
        action->setMenuRole(QAction::NoRole);
        action->setEnabled(false);
        action->setToolTip(QObject::tr("Coming in a later update"));
        return action;
    }

    // One action per value, all on the same command: slots, scales, speeds.
    template <typename Values, typename Text, typename Shortcut>
    void items(const Values& values, const char* command, const char* argName, Text text,
        Shortcut shortcut)
    {
        for (const auto& value : values) {
            item(text(value), command, shortcut(value), {{QString::fromLatin1(argName), value}});
        }
    }

    MenuBuilder submenu(const QString& title)
    {
        QMenu* sub = m_menu->addMenu(title);
        sub->setToolTipsVisible(true);
        return MenuBuilder(sub);
    }

    void separator() { m_menu->addSeparator(); }
    QMenu* menu() const { return m_menu; }

private:
    QMenu* m_menu;
};

// The console's frame rates (nesdev wiki "Cycle reference chart").
constexpr double kNtscFps = 60.0988;
constexpr double kPalFps = 50.0070;

// The actions implemented so far (G1, G2), and those of them that need a
// game (contract "Game" column). Settings pages arrive with their phases.
const QStringList kImplemented = {
    QStringLiteral("gui.open_rom"), QStringLiteral("gui.show_cheats"), QStringLiteral("gui.show_cheat_search"), QStringLiteral("gui.set_region"), QStringLiteral("gui.set_palette"), QStringLiteral("gui.close_game"),
    QStringLiteral("gui.toggle_pause"), QStringLiteral("gui.reset"),
    QStringLiteral("gui.power_cycle"), QStringLiteral("gui.exit"),
    QStringLiteral("gui.show_about"), QStringLiteral("gui.show_library"),
    QStringLiteral("gui.open_library_folder"), QStringLiteral("gui.clear_recent"),
    QStringLiteral("gui.show_settings"), QStringLiteral("gui.save_state"),
    QStringLiteral("gui.load_state"), QStringLiteral("gui.quick_save"),
    QStringLiteral("gui.quick_load"), QStringLiteral("gui.select_slot"),
    QStringLiteral("gui.undo_load_state"), QStringLiteral("gui.save_state_file"),
    QStringLiteral("gui.load_state_file"), QStringLiteral("gui.set_rewind"),
    QStringLiteral("gui.set_fast_forward"), QStringLiteral("gui.toggle_fast_forward"),
    QStringLiteral("gui.set_speed"), QStringLiteral("gui.frame_advance"),
    QStringLiteral("gui.import_battery_save"), QStringLiteral("gui.export_battery_save"),
    QStringLiteral("gui.take_screenshot"), QStringLiteral("gui.toggle_fps"), QStringLiteral("gui.toggle_osd"),
    QStringLiteral("gui.show_shortcuts"), QStringLiteral("gui.set_scale"), QStringLiteral("gui.toggle_fullscreen"),
    QStringLiteral("gui.toggle_integer_scaling"), QStringLiteral("gui.set_aspect"), QStringLiteral("gui.set_overscan"),
    QStringLiteral("gui.set_filter"), QStringLiteral("gui.toggle_always_on_top"),
    QStringLiteral("gui.toggle_audio_recording"), QStringLiteral("gui.record_movie"), QStringLiteral("gui.play_movie"),
    QStringLiteral("gui.stop_movie"), QStringLiteral("gui.toggle_movie_read_only"),
};
// Settings > Hotkeys opens the Key Bindings page (#108).
const QStringList kSettingsPages = {QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("emulation"),
    QStringLiteral("library"), QStringLiteral("servers"), QStringLiteral("input"), QStringLiteral("hotkeys")};
const QStringList kNeedGame = {
    QStringLiteral("gui.close_game"), QStringLiteral("gui.show_cheats"), QStringLiteral("gui.show_cheat_search"), QStringLiteral("gui.toggle_pause"),
    QStringLiteral("gui.reset"), QStringLiteral("gui.power_cycle"),
    QStringLiteral("gui.save_state"), QStringLiteral("gui.load_state"),
    QStringLiteral("gui.quick_save"), QStringLiteral("gui.quick_load"),
    QStringLiteral("gui.undo_load_state"), QStringLiteral("gui.save_state_file"),
    QStringLiteral("gui.load_state_file"), QStringLiteral("gui.set_rewind"),
    QStringLiteral("gui.set_fast_forward"), QStringLiteral("gui.toggle_fast_forward"),
    QStringLiteral("gui.frame_advance"), QStringLiteral("gui.import_battery_save"),
    QStringLiteral("gui.export_battery_save"), QStringLiteral("gui.take_screenshot"),
    QStringLiteral("gui.toggle_audio_recording"), QStringLiteral("gui.record_movie"), QStringLiteral("gui.play_movie"),
};
// Movie > Stop and Read-Only: only while a movie records or plays.
const QStringList kNeedMovie = {QStringLiteral("gui.stop_movie"), QStringLiteral("gui.toggle_movie_read_only")};
// Actions that act while their key is held: on at the press, off at the
// release (contract "Fast Forward (hold)", "Rewind (hold)").
const QStringList kHeld = {QStringLiteral("gui.set_fast_forward"), QStringLiteral("gui.set_rewind")};

// Every overlay's design: PAUSED at full size; the speed, the FPS, messages
// and the automation indicator at half, so they stay out of the way of play.
QString overlayStyle(int scale)
{
    return QStringLiteral("QLabel { color: white; background: rgba(0, 0, 0, 150); border-radius: %1px;"
                          " padding: %2px %3px; font-size: %4px; font-weight: 800; letter-spacing: %5px; }")
        .arg(10 / scale).arg(10 / scale).arg(26 / scale).arg(28 / scale).arg(4 / scale);
}

api::Json toJson(const QVariantMap& map)
{
    return api::Json::parse(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact).toStdString());
}

bool isRom(const QUrl& url)
{
    return url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".nes"), Qt::CaseInsensitive);
}

// Whether any of the library's folders holds anything worth moving.
bool hasContents(const Library& library)
{
    for (const auto folder : {Library::Folder::Roms, Library::Folder::Saves, Library::Folder::States,
             Library::Folder::Screenshots, Library::Folder::Movies, Library::Folder::Recordings,
             Library::Folder::Cheats, Library::Folder::Debug}) {
        const QStringList names = QDir(library.folder(folder)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (!(names.isEmpty() || names == QStringList{QStringLiteral("README.txt")})) return true;
    }
    return false;
}

QKeySequence none(const QVariant&)
{
    return {};
}

QList<QVariant> slotNumbers()
{
    QList<QVariant> numbers;
    for (int slot = 1; slot <= 10; ++slot) numbers << slot;
    return numbers;
}

QString slotText(const QVariant& slot)
{
    return QObject::tr("Slot %1").arg(slot.toInt());
}

// Fn for slots 1-10, with an optional modifier (Shift for saving).
auto functionKey(Qt::KeyboardModifiers modifiers)
{
    return [modifiers](const QVariant& slot) {
        return QKeySequence(modifiers | static_cast<Qt::Key>(Qt::Key_F1 + slot.toInt() - 1));
    };
}

} // namespace

MainWindow::MainWindow(const Options& options, QWidget* parent)
    : QMainWindow(parent),
      m_options(options),
      m_settings(options.settingsFile),
      m_library(options.libraryRoot.isEmpty() ? m_settings.libraryRoot() : options.libraryRoot),
      m_battery(options.batteryInterval)
{
    setWindowTitle(QStringLiteral("cuteNES"));
    setAcceptDrops(true);

    // The default library, or one named on the command line, is created on
    // first use, and missing subfolders are filled in; a saved one that went
    // missing is the user's to decide about (checkLibrary).
    if (m_library.isUsable() || m_library.root() == Library::defaultRoot() || !options.libraryRoot.isEmpty()) {
        m_library.ensureLayout();
    }

    m_pages = new QStackedWidget(this);
    m_browser = new LibraryBrowser(m_pages);
    m_browser->setLibraryRoot(m_library.root());
    m_view = new qt::EmulationView(m_pages);
    m_bindingsPage = new KeyBindingsPage(m_pages);
    m_pages->addWidget(m_browser);
    m_pages->addWidget(m_view);
    m_pages->addWidget(m_bindingsPage);
    m_bindings = std::make_unique<KeyBindings>(m_settings);
    setCentralWidget(m_pages);
    m_osd = new QLabel(m_pages);
    m_osd->setObjectName(QStringLiteral("osdMessage"));
    m_osd->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osd->setStyleSheet(overlayStyle(2));
    m_osd->hide();
    m_osdTimer = new QTimer(this);
    m_osdTimer->setSingleShot(true);
    m_osdTimer->setInterval(2500);
    connect(m_osdTimer, &QTimer::timeout, m_osd, &QWidget::hide);
    m_paused = new QLabel(tr("PAUSED"), m_view);
    m_paused->setObjectName(QStringLiteral("pausedOverlay"));
    m_paused->setStyleSheet(overlayStyle(1));
    m_speed = new QLabel(m_view);
    m_speed->setObjectName(QStringLiteral("speedOverlay"));
    m_speed->setStyleSheet(overlayStyle(2));
    m_fps = new QLabel(m_view);
    m_fps->setObjectName(QStringLiteral("fpsOverlay"));
    m_fps->setStyleSheet(overlayStyle(2));
    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setInterval(1000);
    connect(m_fpsTimer, &QTimer::timeout, this, &MainWindow::updateFps);
    m_movie = new QLabel(m_view);
    m_movie->setObjectName(QStringLiteral("movieOverlay"));
    m_movie->setStyleSheet(overlayStyle(2));
    for (QLabel* overlay : {m_paused, m_speed, m_fps, m_movie}) {
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        overlay->hide();
    }
    m_audio = std::make_unique<qt::AudioOutput>();
    // The saved device, or the default when it is gone; silent if none
    // (spec US7-5).
    const QString device = m_settings.audio().device;
    m_audio->open(device == QLatin1String("default") ? std::string() : device.toStdString());
    if (m_audio->isOpen() && device != QLatin1String("default") && m_audio->device().empty()) {
        QMetaObject::invokeMethod(this, [this, device] {
            showOsd(tr("%1 is not available; sound plays on the default device").arg(device));
        }, Qt::QueuedConnection);
    }

    m_index = new LibraryIndex(options.indexCacheFile.isEmpty() ? LibraryIndex::defaultCacheFile()
                                                                 : options.indexCacheFile,
        m_settings, this);
    connect(m_index, &LibraryIndex::entriesChanged, this, [this] { m_browser->setEntries(m_index->entries()); });
    m_index->setLibrary(m_library);

    connect(m_browser, &LibraryBrowser::openRequested, this, &MainWindow::openRom);
    connect(m_browser, &LibraryBrowser::openFileRequested, this,
        [this] { runCommand(QStringLiteral("gui.open_rom")); });
    connect(m_browser, &LibraryBrowser::addFolderRequested, this, &MainWindow::addRomFolder);
    connect(m_browser, &LibraryBrowser::changeLocationRequested, this, &MainWindow::changeLibraryLocation);
    connect(m_browser, &LibraryBrowser::backRequested, this, &MainWindow::returnToGame);
    connect(m_browser, &LibraryBrowser::keyBindingsRequested, this,
        [this] { runCommand(QStringLiteral("gui.show_shortcuts")); });
    connect(m_bindingsPage, &KeyBindingsPage::backRequested, this, &MainWindow::leaveKeyBindings);
    connect(m_bindingsPage, &KeyBindingsPage::bindRequested, this, &MainWindow::requestBinding);
    connect(m_bindingsPage, &KeyBindingsPage::clearRequested, this, [this](const QString& id, int index) {
        runCommand(QStringLiteral("gui.clear_binding"), {{QStringLiteral("id"), id}, {QStringLiteral("index"), index}});
    });
    connect(m_bindingsPage, &KeyBindingsPage::resetRequested, this, [this](const QString& id) {
        runCommand(QStringLiteral("gui.reset_bindings"),
            id.isEmpty() ? QVariantMap{} : QVariantMap{{QStringLiteral("id"), id}});
    });

    m_host = std::make_unique<GuiHost>(static_cast<GuiHost::Window&>(*this), m_settings, m_battery);
    api::SessionConfig config;
    config.host = m_host.get();
    config.libraryRoot = m_library.folder(Library::Folder::Roms).toStdString();
    config.debugDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath(QStringLiteral("debug"))
                          .toStdString();
    m_session = std::make_unique<api::Session>(config);
    m_host->attach(*m_session);
    m_session->setMediaSinks(m_view, m_host->audioTap(m_audio.get()));
    m_session->setRealtime(true);

    // Settings > Servers: off until the user turns them on (FR-071).
    m_servers = std::make_unique<Servers>(*m_session,
        options.tokenDir.isEmpty() ? api::token::defaultDirectory()
                                   : std::filesystem::path(options.tokenDir.toStdString()));
    m_automation = new QPushButton(m_pages);
    m_automation->setObjectName(QStringLiteral("automationIndicator"));
    m_automation->setFlat(true);
    m_automation->setCursor(Qt::PointingHandCursor);
    m_automation->setStyleSheet(overlayStyle(2).replace(QStringLiteral("QLabel"), QStringLiteral("QPushButton")));
    m_automation->setAccessibleName(tr("Automation"));
    m_automation->hide();
    connect(m_automation, &QPushButton::clicked, this, [this] {
        runCommand(QStringLiteral("gui.show_settings"), {{QStringLiteral("page"), QStringLiteral("servers")}});
    });
    m_automationTimer = new QTimer(this);
    m_automationTimer->setInterval(250);
    connect(m_automationTimer, &QTimer::timeout, this, &MainWindow::updateAutomation);
    // Leaving the application can pass through a moment with no active window.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { QTimer::singleShot(0, this, &MainWindow::followFocus); });
    connect(m_automationTimer, &QTimer::timeout, this, [this] {
        // A device that goes away never stops the game (FR-042).
        switch (m_audio->poll()) {
        case qt::AudioOutput::DeviceChange::MovedToDefault:
            showOsd(tr("The sound device was disconnected; sound plays on the default device"));
            break;
        case qt::AudioOutput::DeviceChange::Lost:
            showOsd(tr("No sound device is left; the game plays on silently"));
            break;
        case qt::AudioOutput::DeviceChange::None:
            break;
        }
    });
    m_automationTimer->start();

    // The frame clock (R4): the console's rate until the window knows its
    // display (followScreen); frames requested while nothing runs are
    // dropped by the session.
    m_clock = std::make_unique<qt::FrameClock>(
        [session = m_session.get()](std::uint32_t frames) { session->requestFrames(frames); });
    m_clock->start(kNtscFps);

    m_commands.setMaxThreadCount(2);
    createMenus();
    wireActions();
    defineBindings();
    applyBindings();
    applyInputSettings();
    applyAudioSettings();

    // Gamepads: a pad plugged back in gets its player back, across runs too.
    m_padBackend = options.gamepads ? options.gamepads : std::shared_ptr<qt::GamepadBackend>(qt::makeSdlGamepads());
    for (const Port port : {Port::One, Port::Two}) {
        const QString guid = m_settings.gamepad(port == Port::One ? 1 : 2);
        if (!guid.isEmpty()) m_padPorts.remember(port, guid.toStdString());
    }
    m_padTimer = new QTimer(this);
    m_padTimer->setTimerType(Qt::PreciseTimer);
    m_padTimer->setInterval(4); // well within a frame
    connect(m_padTimer, &QTimer::timeout, this, &MainWindow::pollGamepads);
    m_padTimer->start();

    applyServers(); // clients may come now: every command's data is there
    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        const QString command = action->property("command").toString();
        if (command == QLatin1String("gui.toggle_fps")) action->setChecked(m_settings.showFps());
        if (command == QLatin1String("gui.toggle_osd")) action->setChecked(m_settings.showOsd());
    }
    rebuildRecentMenu();
    setRunState(QStringLiteral("NeedsRom"));

    // The shortcuts work while the menu bar is hidden (fullscreen) too.
    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        // Open Recent's items come and go, without shortcuts.
        if (!action->menu() && action->parent() != m_recentMenu && !action->property("command").toString().isEmpty()) {
            addAction(action);
        }
    }
    applyVideo(); // the scale, aspect, crop, filter and window state
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    m_automationTimer->stop();
    m_padTimer->stop();
    m_servers.reset(); // clients' calls end with ShuttingDown; the token files go
    m_clock->stop();
    m_host->detach(); // its frame runner must not outlive it
    // FR-012: the battery save is on disk before the window goes (a closed
    // window already did this; without a game the call just fails).
    m_session->call("gui.close_game");
    m_session->setMediaSinks(nullptr, nullptr);
    m_session->shutdown(); // queued and later calls fail with ShuttingDown
    m_commands.waitForDone();
    delete m_index; // it reads m_settings
    m_index = nullptr;
    m_settings.sync();
}

Library MainWindow::library() const
{
    std::lock_guard lock(m_libraryMutex);
    return m_library;
}

bool MainWindow::isShowingLibrary() const
{
    return m_pages->currentWidget() == m_browser;
}

QMenu* MainWindow::menu(const QString& objectName) const
{
    return menuBar()->findChild<QMenu*>(objectName, Qt::FindDirectChildrenOnly);
}

void MainWindow::createMenus()
{
    // The order and names of spec FR-020.
    const std::pair<const char*, QString> menus[] = {
        {"menuFile", tr("&File")},
        {"menuEmulation", tr("&Emulation")},
        {"menuView", tr("&View")},
        {"menuTools", tr("&Tools")},
        {"menuDebug", tr("&Debug")},
        {"menuSettings", tr("&Settings")},
        {"menuHelp", tr("&Help")},
    };
    for (const auto& [name, title] : menus) {
        QMenu* m = menuBar()->addMenu(title);
        m->setObjectName(QString::fromLatin1(name));
        m->setToolTipsVisible(true);
    }
    const QList<QVariant> slotList = slotNumbers();

    MenuBuilder file(menu(QStringLiteral("menuFile")));
    file.item(tr("&Open ROM…"), "gui.open_rom", QKeySequence(Qt::CTRL | Qt::Key_O));
    m_recentMenu = file.submenu(tr("Open &Recent")).menu();
    m_recentMenu->setObjectName(QStringLiteral("menuRecent"));
    file.item(tr("Game &Library"), "gui.show_library", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    file.item(tr("&Close Game"), "gui.close_game", QKeySequence(Qt::CTRL | Qt::Key_W));
    file.separator();
    MenuBuilder saveState = file.submenu(tr("&Save State"));
    saveState.items(slotList, "gui.save_state", "slot", slotText, functionKey(Qt::ShiftModifier));
    m_saveStateMenu = saveState.menu();
    MenuBuilder loadState = file.submenu(tr("Load S&tate"));
    loadState.items(slotList, "gui.load_state", "slot", slotText, functionKey(Qt::NoModifier));
    m_loadStateMenu = loadState.menu();
    file.item(tr("&Quick Save"), "gui.quick_save", QKeySequence(Qt::CTRL | Qt::Key_S));
    file.item(tr("Quick &Load"), "gui.quick_load", QKeySequence(Qt::CTRL | Qt::Key_L));
    MenuBuilder selectSlot = file.submenu(tr("Select Slo&t"));
    selectSlot.items(slotList, "gui.select_slot", "slot", slotText, [](const QVariant& slot) {
        // Ctrl+1 … Ctrl+9, then Ctrl+0 for slot 10.
        return QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + slot.toInt() % 10));
    });
    m_selectSlotMenu = selectSlot.menu();
    for (QAction* action : m_selectSlotMenu->actions()) action->setCheckable(true);
    for (QMenu* slotMenu : {m_saveStateMenu, m_loadStateMenu, m_selectSlotMenu}) {
        connect(slotMenu, &QMenu::aboutToShow, this, &MainWindow::updateSlotMenus);
    }
    file.item(tr("&Undo Load State"), "gui.undo_load_state", QKeySequence(Qt::CTRL | Qt::Key_Z));
    file.item(tr("Save State to File…"), "gui.save_state_file");
    file.item(tr("Load State from File…"), "gui.load_state_file");
    file.separator();
    file.item(tr("&Import Battery Save…"), "gui.import_battery_save");
    file.item(tr("&Export Battery Save…"), "gui.export_battery_save");
    file.separator();
    file.item(tr("Take Scree&nshot"), "gui.take_screenshot", QKeySequence(Qt::Key_F12));
    file.item(tr("Open Library &Folder"), "gui.open_library_folder");
    file.separator();
    QAction* exitAction = file.item(tr("E&xit"), "gui.exit", QKeySequence(Qt::CTRL | Qt::Key_Q));
    exitAction->setMenuRole(QAction::QuitRole); // the application menu on macOS

    MenuBuilder emulation(menu(QStringLiteral("menuEmulation")));
    m_pauseAction = emulation.item(tr("&Pause"), "gui.toggle_pause", QKeySequence(Qt::Key_P));
    m_pauseAction->setShortcuts({QKeySequence(Qt::Key_P), QKeySequence(Qt::Key_Pause)});
    emulation.item(tr("&Reset"), "gui.reset", QKeySequence(Qt::CTRL | Qt::Key_R));
    emulation.item(tr("Power &Cycle"), "gui.power_cycle", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    emulation.separator();
    emulation.item(tr("&Frame Advance"), "gui.frame_advance", QKeySequence(Qt::Key_Backslash));
    emulation.item(tr("Fast Forward (Hold)"), "gui.set_fast_forward", QKeySequence(Qt::Key_Tab),
        {{QStringLiteral("on"), true}});
    emulation.item(tr("Fast Forward (Toggle)"), "gui.toggle_fast_forward",
        QKeySequence(Qt::SHIFT | Qt::Key_Tab));
    emulation.item(tr("Rewind (Hold)"), "gui.set_rewind", QKeySequence(Qt::Key_Backspace),
        {{QStringLiteral("on"), true}});
    MenuBuilder speed = emulation.submenu(tr("&Speed"));
    speed.item(tr("Slower"), "gui.set_speed", QKeySequence(Qt::Key_Minus),
        {{QStringLiteral("step"), QStringLiteral("slower")}});
    speed.item(tr("Faster"), "gui.set_speed", QKeySequence(Qt::Key_Equal),
        {{QStringLiteral("step"), QStringLiteral("faster")}});
    speed.separator();
    speed.items(QList<QVariant>{25, 50, 75, 100, 150, 200, 300, 400}, "gui.set_speed", "percent",
        [](const QVariant& p) { return QObject::tr("%1 %").arg(p.toInt()); }, none);
    speed.item(tr("Unlimited"), "gui.set_speed", {}, {{QStringLiteral("percent"), QStringLiteral("unlimited")}});
    emulation.submenu(tr("Re&gion")).items(
        QList<QVariant>{QStringLiteral("auto"), QStringLiteral("ntsc"), QStringLiteral("pal")},
        "gui.set_region", "region",
        [](const QVariant& r) {
            const QString v = r.toString();
            return v == QLatin1String("auto") ? QObject::tr("Automatic") : v.toUpper();
        },
        none);

    MenuBuilder view(menu(QStringLiteral("menuView")));
    view.submenu(tr("&Scale")).items(QList<QVariant>{1, 2, 3, 4, 5, 6}, "gui.set_scale", "scale",
        [](const QVariant& n) { return QObject::tr("%1x").arg(n.toInt()); },
        [](const QVariant& n) {
            return QKeySequence(Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + n.toInt()));
        });
    QAction* fullscreen = view.item(tr("&Fullscreen"), "gui.toggle_fullscreen", QKeySequence(Qt::Key_F11));
    fullscreen->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::ALT | Qt::Key_Return)});
    view.item(tr("&Integer Scaling"), "gui.toggle_integer_scaling");
    view.submenu(tr("&Aspect Ratio")).items(
        QList<QVariant>{QStringLiteral("square"), QStringLiteral("8:7"), QStringLiteral("4:3")},
        "gui.set_aspect", "aspect",
        [](const QVariant& a) {
            return a.toString() == QLatin1String("square") ? QObject::tr("Square Pixels") : a.toString();
        },
        none);
    MenuBuilder overscan = view.submenu(tr("&Overscan"));
    overscan.item(tr("None"), "gui.set_overscan", {}, {{QStringLiteral("preset"), QStringLiteral("none")}});
    overscan.item(tr("TV (8 lines)"), "gui.set_overscan", {}, {{QStringLiteral("preset"), QStringLiteral("tv")}});
    overscan.item(tr("Custom…"), "gui.set_overscan", {}, {{QStringLiteral("preset"), QStringLiteral("custom")}});
    view.submenu(tr("F&ilter")).items(
        QList<QVariant>{QStringLiteral("nearest"), QStringLiteral("smooth"), QStringLiteral("scanlines")},
        "gui.set_filter", "filter",
        [](const QVariant& f) {
            QString name = f.toString();
            name[0] = name[0].toUpper();
            return name;
        },
        none);
    MenuBuilder palette = view.submenu(tr("&Palette"));
    palette.item(tr("Default"), "gui.set_palette", {}, {{QStringLiteral("name"), QStringLiteral("default")}});
    palette.item(tr("RGB PPU (2C03)"), "gui.set_palette", {}, {{QStringLiteral("name"), QStringLiteral("rgb")}});
    palette.item(tr("Load .pal…"), "gui.set_palette");
    view.separator();
    view.item(tr("Show &FPS"), "gui.toggle_fps")->setCheckable(true);
    view.item(tr("Show On-Screen &Messages"), "gui.toggle_osd")->setCheckable(true);
    view.item(tr("Always on &Top"), "gui.toggle_always_on_top");

    MenuBuilder tools(menu(QStringLiteral("menuTools")));
    tools.item(tr("&Cheats…"), "gui.show_cheats");
    tools.item(tr("Cheat &Search…"), "gui.show_cheat_search");
    tools.separator();
    MenuBuilder movie = tools.submenu(tr("&Movie"));
    movie.item(tr("&Record from Power-On…"), "gui.record_movie", {}, {{QStringLiteral("from"), QStringLiteral("power_on")}});
    movie.item(tr("Record from Current &State…"), "gui.record_movie", {}, {{QStringLiteral("from"), QStringLiteral("state")}});
    movie.item(tr("&Play…"), "gui.play_movie");
    movie.item(tr("&Stop"), "gui.stop_movie");
    movie.item(tr("Read-&Only"), "gui.toggle_movie_read_only")->setCheckable(true);
    tools.separator();
    tools.item(tr("Record &Audio…"), "gui.toggle_audio_recording");

    MenuBuilder debug(menu(QStringLiteral("menuDebug")));
    debug.item(tr("&Debugger"), "gui.show_debugger", QKeySequence(Qt::CTRL | Qt::Key_D));
    debug.item(tr("&Memory Viewer"), "gui.show_memory_viewer", QKeySequence(Qt::CTRL | Qt::Key_M));
    MenuBuilder ppu = debug.submenu(tr("&PPU Viewer"));
    ppu.item(tr("Pattern Tables"), "gui.show_ppu_viewer", QKeySequence(Qt::CTRL | Qt::Key_P),
        {{QStringLiteral("view"), QStringLiteral("patterns")}});
    ppu.item(tr("Nametables"), "gui.show_ppu_viewer", {}, {{QStringLiteral("view"), QStringLiteral("nametables")}});
    ppu.item(tr("Sprites"), "gui.show_ppu_viewer", {}, {{QStringLiteral("view"), QStringLiteral("sprites")}});
    ppu.item(tr("Palette"), "gui.show_ppu_viewer", {}, {{QStringLiteral("view"), QStringLiteral("palette")}});
    debug.item(tr("&Trace Logger"), "gui.show_trace_logger");
    debug.separator();
    debug.item(tr("&Write Memory Dump"), "gui.write_memory_dump");

    MenuBuilder settings(menu(QStringLiteral("menuSettings")));
    QAction* allSettings = settings.item(tr("&Settings…"), "gui.show_settings",
        QKeySequence(Qt::CTRL | Qt::Key_Comma));
    allSettings->setMenuRole(QAction::PreferencesRole); // the application menu on macOS
    settings.separator();
    const std::pair<QString, const char*> pages[] = {
        {tr("&Video…"), "video"}, {tr("&Audio…"), "audio"}, {tr("&Input…"), "input"},
        {tr("&Hotkeys…"), "hotkeys"}, {tr("&Emulation…"), "emulation"},
        {tr("&Library…"), "library"}, {tr("Se&rvers…"), "servers"},
    };
    for (const auto& [text, page] : pages) {
        settings.item(text, "gui.show_settings", {}, {{QStringLiteral("page"), QString::fromLatin1(page)}});
    }

    MenuBuilder help(menu(QStringLiteral("menuHelp")));
    help.item(tr("&Keyboard Shortcuts"), "gui.show_shortcuts");
    help.item(tr("&User Guide"), "gui.show_user_guide");
    help.item(tr("Open &Log Folder"), "gui.open_log_folder");
    help.separator();
    QAction* about = help.item(tr("&About cuteNES"), "gui.show_about");
    about->setMenuRole(QAction::AboutRole); // the application menu on macOS
}

void MainWindow::wireActions()
{
    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        const QString command = action->property("command").toString();
        if (!kImplemented.contains(command)) continue;
        if (command == QLatin1String("gui.show_settings")) {
            const QString page = action->property("commandArgs").toMap().value(QStringLiteral("page")).toString();
            if (!page.isEmpty() && !kSettingsPages.contains(page)) continue;
        }
        action->setToolTip({});
        action->setEnabled(true);
        if (kHeld.contains(command)) action->setAutoRepeat(false);
        connect(action, &QAction::triggered, this, [this, action, command] {
            QVariantMap args = action->property("commandArgs").toMap();
            if (kHeld.contains(command)) {
                // Held by its key: on now, off at the release (eventFilter).
                // Chosen from the menu, nothing is held: it toggles.
                bool held = false;
                for (const QKeySequence& key : action->shortcuts()) held = held || (!key.isEmpty() && m_keysDown.contains(key[0].key()));
                if (!held) {
                    const Playback now = playback();
                    const bool on = command == QLatin1String("gui.set_rewind") ? now.rewinding : now.fastForward;
                    args.insert(QStringLiteral("on"), !on);
                }
            }
            if (command == QLatin1String("gui.save_state")) return askSaveState(args.value(QStringLiteral("slot")).toInt());
            if (command == QLatin1String("gui.quick_save")) return askSaveState(m_host->selectedSlot());
            runCommand(command, args);
        });
    }
}

void MainWindow::defineBindings()
{
    // Every menu action with a fixed command, in menu order, labelled by its
    // menu path; the recent games come and go, so they have none.
    QVector<KeyBindings::Definition> definitions;
    const auto plain = [](QString text) { return text.remove(QLatin1Char('&')).remove(QStringLiteral("…")); };
    std::function<void(QMenu*, const QString&)> walk = [&](QMenu* m, const QString& path) {
        for (QAction* action : m->actions()) {
            if (action->menu()) {
                if (action->menu() != m_recentMenu) walk(action->menu(), path + QStringLiteral(" > ") + plain(action->text()));
                continue;
            }
            const QString command = action->property("command").toString();
            if (command.isEmpty()) continue;
            const QString id = KeyBindings::idFor(command, action->property("commandArgs").toMap());
            m_bindingActions.insert(id, action);
            if (m_bindingActions.count(id) > 1) continue;
            KeyBindings::Definition d{id, path + QStringLiteral(" > ") + plain(action->text()), false, {}};
            const QList<QKeySequence> keys = action->shortcuts();
            for (qsizetype i = 0; i < qMin(qsizetype(2), keys.size()); ++i) {
                d.defaults[std::size_t(i)] = keys[i].toString(QKeySequence::PortableText);
            }
            definitions.push_back(d);
        }
    };
    // The game buttons first, players 1 and 2 (contract "Game input
    // defaults"): player 1's key, then the player's pad control. Return
    // stands for keypad Enter too (applyBindings).
    const struct {
        const char* name;
        QString text;
        const char* key;
        const char* pad;
    } buttons[] = {{"a", tr("A"), "X", "South"}, {"b", tr("B"), "Z", "West"},
        {"select", tr("Select"), "Shift", "Back"}, {"start", tr("Start"), "Return", "Start"},
        {"up", tr("Up"), "Up", "DPadUp"}, {"down", tr("Down"), "Down", "DPadDown"},
        {"left", tr("Left"), "Left", "DPadLeft"}, {"right", tr("Right"), "Right", "DPadRight"},
        {"turbo_a", tr("Turbo A"), "S", "North"}, {"turbo_b", tr("Turbo B"), "A", "East"}};
    for (const int player : {1, 2}) {
        for (const auto& b : buttons) {
            KeyBindings::Definition d{QStringLiteral("p%1.%2").arg(player).arg(QLatin1String(b.name)),
                tr("Player %1 %2").arg(player).arg(b.text), true, {}};
            d.defaults = {player == 1 ? QString::fromLatin1(b.key) : QString(), QStringLiteral("Pad:") + QLatin1String(b.pad)};
            definitions.push_back(d);
        }
    }
    for (QAction* top : menuBar()->actions()) {
        if (top->menu()) walk(top->menu(), plain(top->text()));
    }
    m_bindings->define(definitions);
}

void MainWindow::applyBindings()
{
    m_input.unbindAll();
    m_padHotkeys.clear();
    for (const KeyBindings::Entry& e : m_bindings->entries()) {
        if (e.definition.control) {
            const QStringList id = e.definition.id.split(QLatin1Char('.'));
            const Port port = id[0] == QLatin1String("p1") ? Port::One : Port::Two;
            static const std::map<QString, std::pair<Button, bool>> named = {
                {QStringLiteral("a"), {Button::A, false}}, {QStringLiteral("b"), {Button::B, false}},
                {QStringLiteral("select"), {Button::Select, false}}, {QStringLiteral("start"), {Button::Start, false}},
                {QStringLiteral("up"), {Button::Up, false}}, {QStringLiteral("down"), {Button::Down, false}},
                {QStringLiteral("left"), {Button::Left, false}}, {QStringLiteral("right"), {Button::Right, false}},
                {QStringLiteral("turbo_a"), {Button::A, true}}, {QStringLiteral("turbo_b"), {Button::B, true}}};
            const auto [button, turbo] = named.at(id[1]);
            for (const QString& b : e.bindings) {
                for (const qt::PadControl c : KeyBindings::padControls(b)) m_input.bindPad(port, c, button, turbo);
                const int key = KeyBindings::keyOf(b);
                if (key) m_input.bind(key, port, button, turbo);
                if (key == Qt::Key_Return) m_input.bind(Qt::Key_Enter, port, button, turbo);
            }
            continue;
        }
        QList<QKeySequence> keys;
        for (const QString& b : e.bindings) {
            if (KeyBindings::isPad(b)) {
                m_padHotkeys.push_back({KeyBindings::padControls(b), e.definition.id});
            } else if (!b.isEmpty()) {
                keys << QKeySequence::fromString(b, QKeySequence::PortableText);
            }
        }
        for (QAction* action : m_bindingActions.values(e.definition.id)) action->setShortcuts(keys);
    }
    pushInput();
    m_bindingsPage->setEntries(m_bindings->entries());
}

void MainWindow::bindingsChanged()
{
    QMetaObject::invokeMethod(this, &MainWindow::applyBindings, Qt::QueuedConnection);
}

QVector<MainWindow::Gamepad> MainWindow::gamepads() const
{
    std::lock_guard lock(m_padMutex);
    QVector<Gamepad> out;
    for (const auto& [id, pad] : m_pads) out.push_back(pad);
    std::sort(out.begin(), out.end(), [](const Gamepad& a, const Gamepad& b) { return a.port < b.port; });
    return out;
}

void MainWindow::inputSettingsChanged()
{
    QMetaObject::invokeMethod(this, &MainWindow::applyInputSettings, Qt::QueuedConnection);
}

void MainWindow::audioSettingsChanged()
{
    QMetaObject::invokeMethod(this, &MainWindow::applyAudioSettings, Qt::QueuedConnection);
}

void MainWindow::applyAudioSettings()
{
    const AppSettings::AudioSettings a = m_settings.audio();
    const std::string device = a.device == QLatin1String("default") ? std::string() : a.device.toStdString();
    if (m_audio->device() != device || !m_audio->isOpen()) m_audio->open(device);
    m_audio->setVolume(a.volume);
    m_audio->setUserMuted(a.muted);
    m_audio->setTargetLatencyMs(a.latencyMs);
    updateSettingsDialog();
}

void MainWindow::offerResume(const QString& autoState)
{
    QMetaObject::invokeMethod(this, [this, autoState] {
        auto* box = new QMessageBox(QMessageBox::Question, tr("Resume"),
            tr("Resume %1 where you left off?").arg(m_romName), QMessageBox::Yes | QMessageBox::No, this);
        box->setObjectName(QStringLiteral("resumeOffer"));
        box->setInformativeText(tr("No starts the game afresh."));
        box->setDefaultButton(QMessageBox::Yes);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QMessageBox::finished, this, [this, box, autoState](int) {
            if (box->clickedButton() == box->button(QMessageBox::Yes)) {
                runCommand(QStringLiteral("gui.load_state_file"), {{QStringLiteral("path"), autoState}})
                    .then(this, [this](const api::Result& r) {
                        if (!r.isError()) runCommand(QStringLiteral("core.resume"), {}, true);
                    });
            } else {
                runCommand(QStringLiteral("core.resume"), {}, true);
            }
        });
        box->open();
    }, Qt::QueuedConnection);
}

void MainWindow::recordingChanged(const QString& path)
{
    QMetaObject::invokeMethod(this, [this, recording = !path.isEmpty()] {
        for (QAction* action : menuBar()->findChildren<QAction*>()) {
            if (action->property("command").toString() == QLatin1String("gui.toggle_audio_recording")) {
                action->setText(recording ? tr("Stop &Recording") : tr("Record &Audio…"));
            }
        }
    }, Qt::QueuedConnection);
}

void MainWindow::applyInputSettings()
{
    const AppSettings::InputSettings input = m_settings.input();
    m_turboRate = input.turboRate;
    m_input.setAllowOppositeDirections(input.allowOppositeDirections);
    pushInput();
    updateSettingsDialog();
}

void MainWindow::pollGamepads()
{
    for (const qt::PadEvent& e : m_padBackend->poll()) {
        switch (e.kind) {
        case qt::PadEvent::Kind::Added: {
            const auto port = m_padPorts.added(e.id, e.guid);
            if (!port) break; // a third pad drives no one
            const int player = *port == Port::One ? 1 : 2;
            const QString name = QString::fromStdString(e.name);
            {
                std::lock_guard lock(m_padMutex);
                m_pads[e.id] = {player, name, QString::fromStdString(e.guid)};
            }
            m_settings.setGamepad(player, QString::fromStdString(e.guid));
            showOsd(tr("Player %1: %2").arg(player).arg(name));
            updateSettingsDialog();
            break;
        }
        case qt::PadEvent::Kind::Removed: {
            const auto port = m_padPorts.removed(e.id);
            {
                std::lock_guard lock(m_padMutex);
                m_pads.erase(e.id);
            }
            if (!port) break;
            releasePad(*port);
            // US5-4: the game waits for the player (the setting, on by default).
            if (hasGame() && m_runState == QLatin1String("Running") && m_settings.input().pauseOnGamepadDisconnect) {
                runCommand(QStringLiteral("core.pause"), {}, true);
            }
            showOsd(tr("Player %1's controller was disconnected").arg(*port == Port::One ? 1 : 2));
            updateSettingsDialog();
            break;
        }
        case qt::PadEvent::Kind::Control:
            if (const auto port = m_padPorts.portOf(e.id)) padControl(*port, e.control, e.pressed);
            break;
        }
    }
}

void MainWindow::releasePad(Port port)
{
    const std::size_t i = port == Port::One ? 0 : 1;
    m_input.releasePad(port);
    m_padHeld[i].clear();
    m_padTaken[i].clear();
    for (qsizetype h = m_padHolds.size() - 1; h >= 0; --h) {
        if (m_padHolds[h].port != port) continue;
        QVariantMap args = m_padHolds[h].args;
        args.insert(QStringLiteral("on"), false);
        runCommand(m_padHolds[h].command, args, true);
        m_padHolds.remove(h);
    }
    pushInput();
}

void MainWindow::padControl(Port port, qt::PadControl control, bool pressed)
{
    const std::size_t i = port == Port::One ? 0 : 1;
    QVector<qt::PadControl>& held = m_padHeld[i];
    const bool capturing = m_pages->currentWidget() == m_bindingsPage && m_bindingsPage->isCapturing();
    if (!pressed) {
        // A release always reaches what its press did.
        held.removeAll(control);
        for (qsizetype h = m_padHolds.size() - 1; h >= 0; --h) {
            if (m_padHolds[h].port != port || !m_padHolds[h].controls.contains(control)) continue;
            QVariantMap args = m_padHolds[h].args;
            args.insert(QStringLiteral("on"), false);
            runCommand(m_padHolds[h].command, args, true);
            m_padHolds.remove(h);
        }
        if (capturing) m_bindingsPage->padControl(control, false);
        if (m_padTaken[i].erase(control) == 0 && m_input.handlePad(port, control, false)) pushInput();
        return;
    }
    if (held.contains(control)) return;
    held.push_back(control);

    // Presses go to this window and its dialogs while one has the focus.
    QWidget* active = QApplication::activeWindow();
    const bool ours = active == this || (active && active->parentWidget() && active->parentWidget()->window() == this);
    if (!ours) return;
    const bool free = active == this && !QApplication::activePopupWidget() && !QApplication::activeModalWidget();
    if (free && capturing) {
        m_bindingsPage->padControl(control, true);
        return;
    }

    // A hotkey: the largest combination, ending with this control, whose
    // controls are all held. Its controls leave the game until let go.
    const PadHotkey* hotkey = nullptr;
    if (free) {
        for (const PadHotkey& h : m_padHotkeys) {
            if (!h.controls.contains(control) || (hotkey && hotkey->controls.size() >= h.controls.size())) continue;
            bool all = true;
            for (const qt::PadControl c : h.controls) all = all && held.contains(c);
            if (all) hotkey = &h;
        }
    }
    if (hotkey) {
        for (const qt::PadControl c : hotkey->controls) {
            m_padTaken[i].insert(c);
            m_input.handlePad(port, c, false);
        }
        pushInput();
        for (QAction* action : m_bindingActions.values(hotkey->id)) {
            if (!action->isEnabled()) continue;
            const QString command = action->property("command").toString();
            if (kHeld.contains(command)) {
                // On while the combination is held, as with its key.
                QVariantMap args = action->property("commandArgs").toMap();
                args.insert(QStringLiteral("on"), true);
                runCommand(command, args);
                m_padHolds.push_back({port, hotkey->controls, command, action->property("commandArgs").toMap()});
            } else {
                action->trigger();
            }
            break;
        }
        return;
    }

    if (free && m_pages->currentWidget() == m_view && hasGame()) {
        if (m_input.handlePad(port, control, true)) pushInput();
        return;
    }
    // Elsewhere the pad moves the focus like the keyboard (spec US1-2): the
    // D-pad and the left stick are the arrows, South confirms, East goes back.
    int key = 0;
    switch (control) {
    case qt::PadControl::DPadUp:
    case qt::PadControl::LeftStickUp: key = Qt::Key_Up; break;
    case qt::PadControl::DPadDown:
    case qt::PadControl::LeftStickDown: key = Qt::Key_Down; break;
    case qt::PadControl::DPadLeft:
    case qt::PadControl::LeftStickLeft: key = Qt::Key_Left; break;
    case qt::PadControl::DPadRight:
    case qt::PadControl::LeftStickRight: key = Qt::Key_Right; break;
    case qt::PadControl::South: key = Qt::Key_Return; break;
    case qt::PadControl::East: key = Qt::Key_Escape; break;
    default: return;
    }
    QWidget* target = QApplication::focusWidget() ? QApplication::focusWidget() : active;
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
    QPointer<QWidget> guard(target);
    QApplication::sendEvent(target, &down);
    if (guard) QApplication::sendEvent(target, &up);
}

void MainWindow::showKeyBindings(const QString& section, bool paused)
{
    QMetaObject::invokeMethod(this, [this, section, paused] {
        if (m_pages->currentWidget() != m_bindingsPage) m_bindingsReturn = m_pages->currentWidget();
        if (paused) m_resumeOnReturn = true;
        m_input.clear();
        pushInput();
        m_pages->setCurrentWidget(m_bindingsPage);
        m_bindingsPage->showSection(section);
    }, Qt::QueuedConnection);
}

void MainWindow::leaveKeyBindings()
{
    if (m_bindingsReturn == m_view && hasGame()) {
        returnToGame();
    } else {
        m_pages->setCurrentWidget(m_browser);
        m_browser->view()->setFocus();
    }
}

void MainWindow::requestBinding(const QString& id, int index, const QString& binding)
{
    // The page asks before a conflicting binding is saved (contract "Key
    // Bindings page"); automation passes force instead.
    const auto entry = m_bindings->entry(id);
    if (!entry) return;
    QString why;
    const auto normalized = KeyBindings::normalize(binding, entry->definition.control, &why);
    if (!normalized) return showOsd(why);
    const QVariantMap args = {{QStringLiteral("id"), id}, {QStringLiteral("index"), index},
        {QStringLiteral("binding"), *normalized}};
    const QVector<KeyBindings::Conflict> conflicts = m_bindings->conflicts(id, *normalized);
    if (conflicts.isEmpty()) {
        runCommand(QStringLiteral("gui.set_binding"), args);
        return;
    }
    QStringList others;
    for (const KeyBindings::Conflict& c : conflicts) others << c.label;
    const QString key = KeyBindings::displayText(*normalized, entry->definition.control);
    auto* box = new QMessageBox(QMessageBox::Question, tr("Key Bindings"),
        tr("%1 is already used by %2. Use it for %3 instead?").arg(key, others.join(tr(", ")), entry->definition.label),
        QMessageBox::Yes | QMessageBox::No, this);
    box->setObjectName(QStringLiteral("bindingConflict"));
    box->setDefaultButton(QMessageBox::No);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::finished, this, [this, box, args](int) {
        if (box->clickedButton() != box->button(QMessageBox::Yes)) return;
        QVariantMap forced = args;
        forced.insert(QStringLiteral("force"), true);
        runCommand(QStringLiteral("gui.set_binding"), forced);
    });
    box->open();
}

void MainWindow::rebuildRecentMenu()
{
    // File > Open Recent: the newest first, each opening by its place in
    // the list (gui.open_recent), then Clear List.
    m_recentMenu->clear();
    const QStringList recent = m_settings.recent();
    for (int i = 0; i < recent.size(); ++i) {
        const QString text = i < 9 ? QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(recent[i]).fileName())
                                   : QFileInfo(recent[i]).fileName();
        QAction* action = m_recentMenu->addAction(text);
        action->setObjectName(QStringLiteral("gui.open_recent"));
        action->setProperty("command", QStringLiteral("gui.open_recent"));
        action->setProperty("commandArgs", QVariantMap{{QStringLiteral("index"), i}});
        action->setToolTip(recent[i]);
        action->setMenuRole(QAction::NoRole);
        connect(action, &QAction::triggered, this, [this, i] {
            runCommand(QStringLiteral("gui.open_recent"), {{QStringLiteral("index"), i}});
        });
    }
    if (!recent.isEmpty()) m_recentMenu->addSeparator();
    QAction* clear = m_recentMenu->addAction(tr("Clear List"));
    clear->setObjectName(QStringLiteral("gui.clear_recent"));
    clear->setProperty("command", QStringLiteral("gui.clear_recent"));
    clear->setProperty("commandArgs", QVariantMap{});
    clear->setMenuRole(QAction::NoRole);
    clear->setEnabled(!recent.isEmpty());
    connect(clear, &QAction::triggered, this, [this] { runCommand(QStringLiteral("gui.clear_recent")); });
}

QFuture<api::Result> MainWindow::runCommand(const QString& name, const QVariantMap& args, bool quiet)
{
    QFuture<api::Result> future = QtConcurrent::run(&m_commands,
        [session = m_session.get(), command = name.toStdString(), json = toJson(args)] {
            return session->call(command, json);
        });
    if (!quiet) {
        future.then(this, [this, name](const api::Result& result) {
            if (result.isError()) {
                showError(name, *result.error);
            } else if (result.value.contains("warning") && result.value["warning"].is_string()) {
                showWarning(QString::fromStdString(result.value["warning"].get<std::string>()));
            }
        });
    }
    return future;
}

void MainWindow::showError(const QString& name, const api::Error& error)
{
    if (error.name == "ShuttingDown") return;
    QString text = QString::fromStdString(error.message);
    if (error.data.contains("detail") && error.data["detail"].is_string()) {
        const auto detail = error.data["detail"].get<std::string>();
        if (!detail.empty()) text += QStringLiteral("\n\n") + QString::fromStdString(detail);
    }
    if (error.data.contains("error") && error.data["error"].is_string()) {
        text += QStringLiteral(" (%1)").arg(QString::fromStdString(error.data["error"].get<std::string>()));
    }
    // An empty slot, or nothing to undo, is worth a note, not a dialog
    // in the middle of play.
    if (error.name == "NotFound" && (name == QLatin1String("gui.load_state") || name == QLatin1String("gui.quick_load")
            || name == QLatin1String("gui.undo_load_state"))) {
        showOsd(text);
        emit commandFailed(name, text);
        return;
    }
    auto* box = new QMessageBox(QMessageBox::Warning, QStringLiteral("cuteNES"), text, QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("commandError"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open(); // never blocks: automation keeps running
    emit commandFailed(name, text);
}

void MainWindow::showWarning(const QString& message)
{
    auto* box = new QMessageBox(QMessageBox::Information, QStringLiteral("cuteNES"), message, QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("warning"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    emit warningShown(message);
}

// --- GuiHost::Window (emulation thread) -------------------------------------

void MainWindow::showLibrary(const QString& select, bool paused)
{
    QMetaObject::invokeMethod(this, [this, select, paused] {
        if (paused) m_resumeOnReturn = true;
        m_input.clear();
        pushInput();
        m_pages->setCurrentWidget(m_browser);
        if (!select.isEmpty()) m_browser->select(select);
        m_browser->view()->setFocus();
    }, Qt::QueuedConnection);
}

void MainWindow::showSettings(const QString& page)
{
    QMetaObject::invokeMethod(this, [this, page] {
        if (!m_settingsDialog) {
            m_settingsDialog = new SettingsDialog(this);
            m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(m_settingsDialog, &SettingsDialog::pageShown, this,
                [this](const QString& shown) { m_settings.setLastSettingsPage(shown); });
            connect(m_settingsDialog, &SettingsDialog::changeLibraryRequested, this,
                &MainWindow::changeLibraryLocation);
            connect(m_settingsDialog, &SettingsDialog::openLibraryFolderRequested, this,
                [this] { runCommand(QStringLiteral("gui.open_library_folder")); });
            connect(m_settingsDialog, &SettingsDialog::addRomFolderRequested, this, &MainWindow::addRomFolder);
            connect(m_settingsDialog, &SettingsDialog::removeRomFolderRequested, this, [this](const QString& path) {
                runCommand(QStringLiteral("gui.remove_rom_folder"), {{QStringLiteral("path"), path}});
            });
            connect(m_settingsDialog, &SettingsDialog::keyBindingsRequested, this, [this] {
                runCommand(QStringLiteral("gui.show_shortcuts"), {{QStringLiteral("section"), QStringLiteral("controls")}});
            });
            connect(m_settingsDialog, &SettingsDialog::settingRequested, this,
                [this](const QString& key, const QVariant& value) {
                    runCommand(QStringLiteral("gui.set_setting"), {{QStringLiteral("key"), key}, {QStringLiteral("value"), value}});
                });
            updateSettingsDialog();
        }
        m_settingsDialog->showPage(page);
        m_settingsDialog->show(); // non-modal (R11)
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
    }, Qt::QueuedConnection);
}

void MainWindow::openLibraryFolder()
{
    QMetaObject::invokeMethod(this, [this] {
        const QString root = library().root();
        if (m_options.openExternally) QDesktopServices::openUrl(QUrl::fromLocalFile(root));
        emit libraryFolderOpened(root);
    }, Qt::QueuedConnection);
}

void MainWindow::exit()
{
    QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection);
}

QVector<LibraryEntry> MainWindow::libraryEntries() const
{
    return m_index->entries();
}

void MainWindow::gameOpened(const QString& path, const GameIdentity&, const BatterySaver::Opened&)
{
    m_consoleRate = m_session->machine().region() == Region::Pal ? kPalFps : kNtscFps;
    playbackChanged(playback()); // the speed applies to the new game's rate
    QMetaObject::invokeMethod(this, [this, path] {
        m_romName = QFileInfo(path).fileName();
        setWindowTitle(tr("%1 — cuteNES").arg(m_romName));
        m_resumeOnReturn = false;
        showGame();
        rebuildRecentMenu();
        // A ROM from outside the library joins it through the recent list.
        bool known = false;
        for (const auto& e : m_index->entries()) known = known || e.path == path;
        if (known) {
            m_index->refreshAnnotations();
        } else {
            m_index->rescan();
        }
    }, Qt::QueuedConnection);
}

void MainWindow::gameClosed()
{
    m_view->clearFrame();
    QMetaObject::invokeMethod(this, [this] {
        m_romName.clear();
        setWindowTitle(QStringLiteral("cuteNES"));
        m_resumeOnReturn = false;
        if (!m_closing) m_pages->setCurrentWidget(m_browser);
        m_index->refreshAnnotations();
    }, Qt::QueuedConnection);
}

void MainWindow::recentChanged()
{
    QMetaObject::invokeMethod(this, [this] {
        rebuildRecentMenu();
        m_index->refreshAnnotations();
    }, Qt::QueuedConnection);
}

void MainWindow::libraryChanged(const Library& changed)
{
    QMetaObject::invokeMethod(this, [this, changed] { useLibrary(changed); }, Qt::QueuedConnection);
}

void MainWindow::romFoldersChanged()
{
    QMetaObject::invokeMethod(this, [this] {
        m_index->rescan();
        updateSettingsDialog();
    }, Qt::QueuedConnection);
}

void MainWindow::sessionEvent(const api::Event& event)
{
    if (event.type == "frame") {
        // Show FPS counts the game's frames, not the window's paints.
        std::lock_guard lock(m_playbackMutex);
        m_lastFrameEvent = {event.frame, std::chrono::steady_clock::now()};
        // The movie's frame count moves with play.
        if (m_host && m_host->movieStatus().mode != GuiHost::MovieStatus::Mode::None) {
            QMetaObject::invokeMethod(this, [this] { updateOverlays(); }, Qt::QueuedConnection);
        }
        return;
    }
    if (event.type != "run_state_changed") return;
    // A power-on may have changed the region (Emulation > Region): the
    // frame clock follows the region in effect.
    const double rate = m_session->machine().region() == Region::Pal ? kPalFps : kNtscFps;
    if (rate != m_consoleRate.load()) {
        m_consoleRate = rate;
        playbackChanged(playback());
    }
    m_pausedByAutomation = event.data.value("pausedByAutomation", false);
    const std::string runState = event.data.value("runState", "");
    // Stale sound never plays after a pause or a closed game (R4).
    if (runState != "Running") m_audio->clear();
    QMetaObject::invokeMethod(this, [this, state = QString::fromStdString(runState)] {
        setRunState(state);
    }, Qt::QueuedConnection);
}

// --- GUI thread --------------------------------------------------------------

void MainWindow::useLibrary(const Library& changed)
{
    {
        std::lock_guard lock(m_libraryMutex);
        m_library = changed;
    }
    m_browser->setLibraryRoot(changed.root());
    updateSettingsDialog();
    m_index->setLibrary(changed);
}

void MainWindow::updateSettingsDialog()
{
    if (!m_settingsDialog) return;
    const Library current = library();
    m_settingsDialog->setLibraryRoot(current.root());
    m_settingsDialog->setRomFolders(current.folder(Library::Folder::Roms), m_settings.romFolders());
    const AppSettings::ServerSettings saved = m_settings.servers();
    const auto shown = [](bool enabled, int port, const Servers::Listener& running) {
        return SettingsDialog::Server{enabled, port, running.on, running.url, running.token, running.tokenFile};
    };
    m_settingsDialog->setServers(shown(saved.apiEnabled, saved.apiPort, m_servers->api()),
        shown(saved.mcpEnabled, saved.mcpPort, m_servers->mcp()));
    const AppSettings::InputSettings input = m_settings.input();
    SettingsDialog::Input page{input.turboRate, input.allowOppositeDirections, input.pauseOnGamepadDisconnect, {}};
    for (const Gamepad& pad : gamepads()) page.gamepads << tr("Player %1: %2").arg(pad.port).arg(pad.name);
    m_settingsDialog->setInput(page);
    const AppSettings::VideoSettings v = m_settings.video();
    SettingsDialog::Video video{v.scale, v.fullscreen, v.integerScaling, v.aspect,
        {v.overscanTop, v.overscanBottom, v.overscanLeft, v.overscanRight}, v.filter, v.alwaysOnTop,
        m_settings.showFps(), m_settings.showOsd(), v.pacing, pacingText()};
    m_settingsDialog->setVideo(video);
    const AppSettings::AudioSettings a = m_settings.audio();
    SettingsDialog::Audio audio{a.device, {}, a.volume, a.muted, a.latencyMs, a.fastForwardAudio};
    for (const std::string& name : qt::AudioOutput::devices()) audio.devices << QString::fromStdString(name);
    m_settingsDialog->setAudio(audio);
    const AppSettings::EmulationSettings e = m_settings.emulation();
    m_settingsDialog->setEmulation({e.pauseWhenInactive, e.resumeOnOpen, e.fastForwardSpeed});
}

void MainWindow::serversChanged()
{
    // Queued: the call that changed the setting may be on the listener
    // being stopped, and it must have its reply first.
    QMetaObject::invokeMethod(this, [this] { applyServers(); }, Qt::QueuedConnection);
}

void MainWindow::applyServers()
{
    AppSettings::ServerSettings wanted = m_settings.servers();
    QString error;
    if (!m_servers->apply(wanted.apiEnabled, wanted.apiPort, wanted.mcpEnabled, wanted.mcpPort, &error)) {
        // A port in use: what could not start is turned off (spec edge case).
        wanted.apiEnabled = m_servers->api().on;
        wanted.mcpEnabled = m_servers->mcp().on;
        m_settings.setServers(wanted);
        showWarning(error);
    }
    updateSettingsDialog();
    updateAutomation();
}

void MainWindow::updateAutomation()
{
    // FR-073: shown while a client is there (an open stream, a call
    // running, or a call in the last few seconds), and says when
    // automation has paused the game.
    const api::ListenerActivity a = m_servers->activity();
    constexpr auto kRecent = std::chrono::seconds(5);
    const bool active = a.streams > 0 || a.calls > 0
        || (a.lastCall != std::chrono::steady_clock::time_point{}
            && std::chrono::steady_clock::now() - a.lastCall < kRecent);
    const bool paused = m_pausedByAutomation && hasGame() && m_runState == QLatin1String("Paused");
    m_automation->setVisible(active || paused);
    if (!m_automation->isVisible()) return;
    m_automation->setText(paused ? tr("AUTOMATION · PAUSED") : tr("AUTOMATION"));
    QStringList tip;
    tip << tr("An API or MCP client is connected.");
    if (m_servers->api().on) tip << tr("API: %1").arg(m_servers->api().url);
    if (m_servers->mcp().on) tip << tr("MCP server: %1").arg(m_servers->mcp().url);
    if (paused) tip << tr("Paused by automation. Pause / Resume plays on.");
    tip << tr("Click for Settings > Servers.");
    m_automation->setToolTip(tip.join(QLatin1Char('\n')));
    m_automation->adjustSize();
    constexpr int kMargin = 12;
    m_automation->move(m_pages->width() - m_automation->width() - kMargin,
        m_pages->height() - m_automation->height() - kMargin);
    m_automation->raise();
}

void MainWindow::addRomFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add a ROM Folder"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (!folder.isEmpty()) runCommand(QStringLiteral("gui.add_rom_folder"), {{QStringLiteral("path"), folder}});
}

void MainWindow::setRunState(const QString& runState)
{
    m_runState = runState;
    const bool game = hasGame();
    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        const QString command = action->property("command").toString();
        if (kNeedGame.contains(command)) action->setEnabled(game);
        if (kNeedMovie.contains(command)) {
            action->setEnabled(game && m_host && m_host->movieStatus().mode != GuiHost::MovieStatus::Mode::None);
        }
    }
    updateOverlays();
    // The menu offers what the next press does.
    m_pauseAction->setText(runState == QLatin1String("Paused") ? tr("&Resume") : tr("&Pause"));
    if (game) {
        const QString title = runState == QLatin1String("Paused") ? tr("%1 (paused) — cuteNES")
                                                                 : tr("%1 — cuteNES");
        setWindowTitle(title.arg(m_romName.isEmpty() ? tr("Game") : m_romName));
    } else if (!m_closing) {
        m_pages->setCurrentWidget(m_browser);
    }
}

void MainWindow::showGame()
{
    m_pages->setCurrentWidget(m_view);
    m_view->setFocus();
}

void MainWindow::returnToGame()
{
    if (!hasGame()) return;
    showGame();
    if (m_resumeOnReturn) {
        m_resumeOnReturn = false;
        runCommand(QStringLiteral("core.resume"), {}, true);
    }
}

void MainWindow::showOpenRomDialog()
{
    // Callable from any thread: gui.open_rom without a path lands here.
    QMetaObject::invokeMethod(this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open ROM"),
            library().folder(Library::Folder::Roms), tr("NES ROMs (*.nes)"));
        if (!path.isEmpty()) openRom(path);
    }, Qt::QueuedConnection);
}

void MainWindow::showFileDialog(FileDialog dialog)
{
    // Callable from any thread, like showOpenRomDialog.
    QMetaObject::invokeMethod(this, [this, dialog] {
        if (dialog == FileDialog::Palette) {
            const QString path = QFileDialog::getOpenFileName(this, tr("Load Palette"), library().root(),
                tr("Palettes (*.pal)"));
            if (!path.isEmpty()) runCommand(QStringLiteral("gui.set_palette"), {{QStringLiteral("path"), path}});
            return;
        }
        const bool battery = dialog == FileDialog::ExportBattery || dialog == FileDialog::ImportBattery;
        const bool save = dialog == FileDialog::SaveState || dialog == FileDialog::ExportBattery;
        const QString folder = library().folder(battery ? Library::Folder::Saves : Library::Folder::States);
        const QString filter = battery ? tr("Battery saves (*.sav)") : tr("cuteNES states (*.state)");
        QString title;
        QString command;
        switch (dialog) {
        case FileDialog::SaveState: title = tr("Save State to File"); command = QStringLiteral("gui.save_state_file"); break;
        case FileDialog::LoadState: title = tr("Load State from File"); command = QStringLiteral("gui.load_state_file"); break;
        case FileDialog::ExportBattery: title = tr("Export Battery Save"); command = QStringLiteral("gui.export_battery_save"); break;
        case FileDialog::ImportBattery: title = tr("Import Battery Save"); command = QStringLiteral("gui.import_battery_save"); break;
        case FileDialog::Palette: break;
        }
        const QString path = save ? QFileDialog::getSaveFileName(this, title, folder, filter)
                                  : QFileDialog::getOpenFileName(this, title, folder, filter);
        if (!path.isEmpty()) runCommand(command, {{QStringLiteral("path"), path}});
    }, Qt::QueuedConnection);
}

void MainWindow::displayChanged()
{
    QMetaObject::invokeMethod(this, [this] {
        for (QAction* action : menuBar()->findChildren<QAction*>()) {
            const QString command = action->property("command").toString();
            if (command == QLatin1String("gui.toggle_fps")) action->setChecked(m_settings.showFps());
            if (command == QLatin1String("gui.toggle_osd")) action->setChecked(m_settings.showOsd());
        }
        if (!m_settings.showOsd()) m_osd->hide();
        updateOverlays();
        updateSettingsDialog();
    }, Qt::QueuedConnection);
}

void MainWindow::videoChanged(bool showPage)
{
    QMetaObject::invokeMethod(this, [this, showPage] {
        applyVideo();
        if (showPage) showSettings(QStringLiteral("video"));
    }, Qt::QueuedConnection);
}

void MainWindow::applyVideo()
{
    const AppSettings::VideoSettings v = m_settings.video();
    qt::DisplayOptions options;
    options.aspect = v.aspect == QLatin1String("square") ? qt::Aspect::Square
        : v.aspect == QLatin1String("4:3")              ? qt::Aspect::FourThree
                                                        : qt::Aspect::EightToSeven;
    options.overscan = {v.overscanTop, v.overscanBottom, v.overscanLeft, v.overscanRight};
    options.integerScaling = v.integerScaling;
    options.filter = v.filter == QLatin1String("smooth") ? qt::Filter::Smooth
        : v.filter == QLatin1String("scanlines")         ? qt::Filter::Scanlines
                                                         : qt::Filter::Nearest;
    m_view->setDisplayOptions(options);

    const std::optional<AppSettings::VideoSettings> was = m_video;
    m_video = v;
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) != v.alwaysOnTop) {
        const bool shown = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, v.alwaysOnTop); // hides the window
        if (shown) show();
    }
    const bool leaving = was && was->fullscreen && !v.fullscreen;
    if (v.fullscreen != bool(windowState() & Qt::WindowFullScreen)) {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    }
    // In fullscreen the menu bar hides, but for the top edge or Alt
    // (eventFilter); macOS shows its own at the top edge.
    if (!menuBar()->isNativeMenuBar()) {
        menuBar()->setVisible(!v.fullscreen);
        if (!m_menuEdgeTimer) {
            // Fullscreen: the menu bar shows at the top edge, and hides once
            // the pointer leaves it with no menu open.
            m_menuEdgeTimer = new QTimer(this);
            m_menuEdgeTimer->setInterval(100);
            connect(m_menuEdgeTimer, &QTimer::timeout, this, [this] {
                const int y = mapFromGlobal(QCursor::pos()).y();
                if (y <= 1 && isActiveWindow()) {
                    menuBar()->show();
                } else if (menuBar()->isVisible() && y > menuBar()->height() + 8 && !QApplication::activePopupWidget()) {
                    menuBar()->hide();
                }
            });
        }
        v.fullscreen ? m_menuEdgeTimer->start() : m_menuEdgeTimer->stop();
    }
    // A new size of picture or scale resizes the window, unless the user
    // has it fullscreen or maximized.
    const bool resized = !was || leaving || was->scale != v.scale || was->aspect != v.aspect
        || was->overscanTop != v.overscanTop || was->overscanBottom != v.overscanBottom
        || was->overscanLeft != v.overscanLeft || was->overscanRight != v.overscanRight;
    if (resized && !v.fullscreen && !(windowState() & Qt::WindowMaximized)) {
        if (leaving) {
            // Out of fullscreen once the platform has restored the window.
            QTimer::singleShot(0, this, [this] {
                if (!isFullScreen()) resizeToScale();
            });
        } else {
            resizeToScale();
        }
    }

    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        const QString command = action->property("command").toString();
        const QVariantMap args = action->property("commandArgs").toMap();
        static const QStringList checked = {QStringLiteral("gui.set_scale"), QStringLiteral("gui.toggle_fullscreen"),
            QStringLiteral("gui.toggle_integer_scaling"), QStringLiteral("gui.set_aspect"),
            QStringLiteral("gui.set_filter"), QStringLiteral("gui.toggle_always_on_top"),
            QStringLiteral("gui.set_overscan"), QStringLiteral("gui.set_region"), QStringLiteral("gui.set_palette")};
        if (checked.contains(command)) action->setCheckable(true);
        if (command == QLatin1String("gui.set_scale")) {
            action->setChecked(args.value(QStringLiteral("scale")).toInt() == v.scale);
        } else if (command == QLatin1String("gui.toggle_fullscreen")) {
            action->setChecked(v.fullscreen);
        } else if (command == QLatin1String("gui.toggle_integer_scaling")) {
            action->setChecked(v.integerScaling);
        } else if (command == QLatin1String("gui.set_aspect")) {
            action->setChecked(args.value(QStringLiteral("aspect")).toString() == v.aspect);
        } else if (command == QLatin1String("gui.set_filter")) {
            action->setChecked(args.value(QStringLiteral("filter")).toString() == v.filter);
        } else if (command == QLatin1String("gui.toggle_always_on_top")) {
            action->setChecked(v.alwaysOnTop);
        } else if (command == QLatin1String("gui.set_region")) {
            action->setChecked(args.value(QStringLiteral("region")).toString() == m_settings.emulation().region);
        } else if (command == QLatin1String("gui.set_palette")) {
            // A built-in by name; Load .pal… is checked while a file is in use.
            const bool file = !builtinPalette(v.palette).has_value();
            action->setChecked(args.isEmpty() ? file : args.value(QStringLiteral("name")).toString() == v.palette);
        } else if (command == QLatin1String("gui.set_overscan")) {
            const bool none = v.overscanTop == 0 && v.overscanBottom == 0 && v.overscanLeft == 0 && v.overscanRight == 0;
            const bool tv = v.overscanTop == 8 && v.overscanBottom == 8 && v.overscanLeft == 0 && v.overscanRight == 0;
            const QString preset = args.value(QStringLiteral("preset")).toString();
            action->setChecked(preset == QLatin1String("none") ? none
                    : preset == QLatin1String("tv")         ? tv
                                                            : !none && !tv);
        }
    }
    playbackChanged(playback()); // video.pacing
    updateSettingsDialog();
}

void MainWindow::resizeToScale()
{
    // Rounded up, so the picture fits at exactly that scale (FR-040).
    const QSizeF picture = qt::scaledSize(m_video ? m_video->scale : kDefaultScale, m_view->displayOptions());
    const int menuHeight = menuBar()->isNativeMenuBar() || menuBar()->isHidden() ? 0 : menuBar()->sizeHint().height();
    resize(qCeil(picture.width()), qCeil(picture.height()) + menuHeight);
}

void MainWindow::showMessage(const QString& text)
{
    QMetaObject::invokeMethod(this, [this, text] { showOsd(text); }, Qt::QueuedConnection);
}

void MainWindow::showOsd(const QString& text)
{
    // The message floats over the page, so it never takes room from the
    // game (a status bar did, for good).
    if (!m_settings.showOsd()) return; // View > Show On-Screen Messages
    m_osd->setText(text);
    m_osd->adjustSize();
    placeOsd();
    m_osd->show();
    m_osd->raise();
    m_osdTimer->start();
    emit messageShown(text);
}

void MainWindow::releaseHeld(int key)
{
    for (QAction* action : menuBar()->findChildren<QAction*>()) {
        const QString command = action->property("command").toString();
        if (!kHeld.contains(command) || !action->isEnabled()) continue;
        for (const QKeySequence& shortcut : action->shortcuts()) {
            if (!shortcut.isEmpty() && shortcut[0].key() == key) {
                runCommand(command, {{QStringLiteral("on"), false}}, true);
            }
        }
    }
}

double MainWindow::frameRate() const
{
    return m_clock->rate();
}

MainWindow::Playback MainWindow::playback() const
{
    std::lock_guard lock(m_playbackMutex);
    return m_playback;
}

void MainWindow::playbackChanged(const Playback& playback)
{
    // Any thread (the host calls it on the emulation thread). Unlimited
    // asks for frames far faster than they can run; the session keeps at
    // most two pending, so they run back to back (R4).
    {
        std::lock_guard lock(m_playbackMutex);
        m_playback = playback;
    }
    const QString mode = m_settings.video().pacing;
    const qt::Pacing pacing = qt::choosePacing(mode == QLatin1String("audio") ? qt::PacingMode::Audio
            : mode == QLatin1String("display")                               ? qt::PacingMode::Display
                                                                             : qt::PacingMode::Auto,
        m_displayHz.load(), m_consoleRate.load());
    {
        std::lock_guard lock(m_playbackMutex);
        m_pacing = {pacing.display, pacing.divisor, m_displayHz.load(), pacing.framesPerSecond};
    }
    constexpr double kUnlimited = 1000.0;
    m_clock->setRate(playback.speed > 0 ? pacing.framesPerSecond * playback.speed : kUnlimited);
    m_audio->setSpeed(playback.speed > 0 ? playback.speed : 1.0);
    m_audio->setMuted(playback.muted);
    QMetaObject::invokeMethod(this, [this] { updateOverlays(); }, Qt::QueuedConnection);
}

void MainWindow::showCheats(bool search)
{
    QMetaObject::invokeMethod(this, [this, search] {
        const CommandRunner run = [this](const QString& name, const QVariantMap& args) {
            return runCommand(name, args);
        };
        QDialog* window = nullptr;
        if (search) {
            if (!m_cheatSearchWindow) m_cheatSearchWindow = new CheatSearchWindow(run, this);
            window = m_cheatSearchWindow;
        } else {
            if (!m_cheatsWindow) m_cheatsWindow = new CheatsWindow(run, this);
            m_cheatsWindow->refresh();
            window = m_cheatsWindow;
        }
        window->show();
        window->raise();
        window->activateWindow();
    }, Qt::QueuedConnection);
}

void MainWindow::showMovieDialog(bool record, const QString& from, const QString& folder)
{
    QMetaObject::invokeMethod(this, [this, record, from, folder] {
        const QString start = folder.isEmpty() ? library().folder(Library::Folder::Movies) : folder;
        const QString filter = tr("cuteNES movies (*.cnm)");
        if (record) {
            if (!folder.isEmpty()) QDir().mkpath(folder);
            const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            const QString path = QFileDialog::getSaveFileName(this,
                from == QLatin1String("state") ? tr("Record Movie from Current State") : tr("Record Movie from Power-On"),
                QDir(start).filePath(stamp + QStringLiteral(".cnm")), filter);
            if (!path.isEmpty()) {
                runCommand(QStringLiteral("gui.record_movie"), {{QStringLiteral("path"), path}, {QStringLiteral("from"), from}});
            }
            return;
        }
        const QString path = QFileDialog::getOpenFileName(this, tr("Play Movie"), start, filter);
        if (!path.isEmpty()) runCommand(QStringLiteral("gui.play_movie"), {{QStringLiteral("path"), path}});
    }, Qt::QueuedConnection);
}

void MainWindow::movieChanged()
{
    QMetaObject::invokeMethod(this, [this] {
        const GuiHost::MovieStatus movie = m_host->movieStatus();
        using Mode = GuiHost::MovieStatus::Mode;
        for (QAction* action : menuBar()->findChildren<QAction*>()) {
            const QString command = action->property("command").toString();
            if (!kNeedMovie.contains(command)) continue;
            action->setEnabled(hasGame() && movie.mode != Mode::None);
            if (action->isCheckable()) action->setChecked(movie.mode == Mode::Playing);
        }
        updateOverlays();
    }, Qt::QueuedConnection);
}

void MainWindow::cheatsChanged()
{
    QMetaObject::invokeMethod(this, [this] {
        if (m_cheatsWindow) m_cheatsWindow->refresh();
    }, Qt::QueuedConnection);
}

QString MainWindow::pacingText() const
{
    const PacingNow now = pacingNow();
    if (now.display) {
        return now.divisor == 1 ? tr("Now: in step with the %1 Hz display").arg(now.displayHz, 0, 'f', 2)
                                : tr("Now: a frame every %1 refreshes of the %2 Hz display")
                                      .arg(now.divisor)
                                      .arg(now.displayHz, 0, 'f', 2);
    }
    return now.displayHz > 0 ? tr("Now: the console's own rate (the %1 Hz display is too far from it)")
                                   .arg(now.displayHz, 0, 'f', 2)
                             : tr("Now: the console's own rate");
}

MainWindow::PacingNow MainWindow::pacingNow() const
{
    std::lock_guard lock(m_playbackMutex);
    return m_pacing;
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (!m_screenWatched) {
        m_screenWatched = true;
        connect(windowHandle(), &QWindow::screenChanged, this, &MainWindow::followScreen);
        followScreen();
    }
}

void MainWindow::followScreen()
{
    // Sync to display needs the refresh rate of the screen the window is on
    // (R4); moving to another screen, or the rate changing, re-chooses.
    QScreen* screen = windowHandle() ? windowHandle()->screen() : this->screen();
    disconnect(m_refreshConnection);
    m_refreshConnection = screen ? connect(screen, &QScreen::refreshRateChanged, this, &MainWindow::followScreen)
                                 : QMetaObject::Connection();
    m_displayHz = screen ? screen->refreshRate() : 0.0;
    playbackChanged(playback());
    updateSettingsDialog();
}

void MainWindow::updateOverlays()
{
    const bool game = hasGame();
    m_paused->setVisible(game && m_runState == QLatin1String("Paused"));
    m_paused->adjustSize();
    m_paused->move(m_view->rect().center() - m_paused->rect().center());
    m_paused->raise();

    const Playback now = playback();
    const bool faster = now.speed == 0 || now.speed > 1;
    QString text;
    if (now.rewinding) {
        text = faster ? tr("◀◀ FAST REWIND") : tr("◀◀ REWIND");
    } else if (now.fastForward) {
        text = tr("▶▶ FAST FORWARD");
    } else if (now.speed == 0) {
        text = tr("▶▶ UNLIMITED");
    } else if (qRound(now.speed * 100) != 100) {
        text = tr("SPEED %1%").arg(qRound(now.speed * 100));
    }
    m_speed->setText(text);
    m_speed->setVisible(game && !text.isEmpty());
    m_speed->adjustSize();
    constexpr int kMargin = 12;
    m_speed->move(m_view->width() - m_speed->width() - kMargin, kMargin);
    m_speed->raise();

    const bool fps = game && m_settings.showFps();
    if (fps && !m_fps->isVisible()) {
        m_fps->setText(tr("— FPS"));
        {
            std::lock_guard lock(m_playbackMutex);
            m_fpsSample = m_lastFrameEvent;
        }
        m_fpsTimer->start();
    } else if (!fps) {
        m_fpsTimer->stop();
    }
    m_fps->setVisible(fps);
    m_fps->adjustSize();
    m_fps->move(kMargin, kMargin);
    m_fps->raise();

    const GuiHost::MovieStatus movie = m_host ? m_host->movieStatus() : GuiHost::MovieStatus();
    using Mode = GuiHost::MovieStatus::Mode;
    m_movie->setText(movie.mode == Mode::Recording ? tr("● REC %1").arg(movie.frame)
                                                   : tr("▶ %1 / %2").arg(movie.frame).arg(movie.frames));
    m_movie->setVisible(game && movie.mode != Mode::None);
    m_movie->adjustSize();
    m_movie->move(kMargin, fps ? kMargin + m_fps->height() + kMargin / 2 : kMargin);
    m_movie->raise();
}

void MainWindow::updateFps()
{
    // The frames between the last two frame events the session sent (at
    // most 10 a second, each stamped with the core's frame counter).
    FrameSample now;
    {
        std::lock_guard lock(m_playbackMutex);
        now = m_lastFrameEvent;
    }
    if (now.frame == m_fpsSample.frame || now.at <= m_fpsSample.at) {
        m_fps->setText(tr("0 FPS")); // no frame since the last count
    } else {
        const double seconds = std::chrono::duration<double>(now.at - m_fpsSample.at).count();
        m_fps->setText(tr("%1 FPS").arg(qRound(static_cast<double>(now.frame - m_fpsSample.frame) / seconds)));
    }
    m_fpsSample = now;
    m_fps->adjustSize();
}

void MainWindow::placeOsd()
{
    constexpr int kMargin = 12;
    m_osd->move(kMargin, m_pages->height() - m_osd->height() - kMargin);
}

void MainWindow::askSaveState(int slot)
{
    if (!hasGame() || findChild<QDialog*>(QStringLiteral("saveStateDialog"))) return; // a held key asks once
    // The state is the moment the key was pressed, not when the name is done.
    const bool wasRunning = m_runState == QLatin1String("Running");
    if (wasRunning) runCommand(QStringLiteral("core.pause"), {}, true);

    const StateSlots::Info info = m_host->stateSlots().info(slot);
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("saveStateDialog"));
    dialog->setWindowTitle(tr("Save State to Slot %1").arg(slot));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new QLabel(tr("Name:"), dialog));
    auto* name = new QLineEdit(StateSlots::defaultName(), dialog);
    name->setObjectName(QStringLiteral("stateName"));
    name->selectAll();
    name->setMinimumWidth(320);
    layout->addWidget(name);
    if (!info.empty) {
        auto* warning = new QLabel(tr("Slot %1 already holds “%2”, saved %3. Saving replaces it.")
                                       .arg(slot)
                                       .arg(info.name.toHtmlEscaped(), QLocale().toString(info.savedAt, QLocale::ShortFormat)),
            dialog);
        warning->setObjectName(QStringLiteral("replaceWarning"));
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    if (!info.empty) buttons->button(QDialogButtonBox::Save)->setText(tr("Replace"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this, slot, name, wasRunning](int result) {
        if (result == QDialog::Accepted) {
            runCommand(QStringLiteral("gui.save_state"), {{QStringLiteral("slot"), slot}, {QStringLiteral("name"), name->text()}});
        }
        if (wasRunning) runCommand(QStringLiteral("core.resume"), {}, true);
    });
    dialog->open(); // never blocks: automation keeps running
}

void MainWindow::updateSlotMenus()
{
    const StateSlots gameSlots = m_host->stateSlots();
    const int selected = m_host->selectedSlot();
    const QLocale locale;
    for (QMenu* menu : {m_saveStateMenu, m_loadStateMenu, m_selectSlotMenu}) {
        for (QAction* action : menu->actions()) {
            const int slot = action->property("commandArgs").toMap().value(QStringLiteral("slot")).toInt();
            const StateSlots::Info info = gameSlots.info(slot);
            QString text = slotText(slot);
            if (gameSlots.isOpen()) {
                text = info.empty ? tr("Slot %1 — Empty").arg(slot) : tr("Slot %1 — %2").arg(slot).arg(info.name);
            }
            action->setText(text);
            action->setToolTip(info.empty ? QString() : tr("Saved %1").arg(locale.toString(info.savedAt, QLocale::ShortFormat)));
            action->setIcon(info.empty ? QIcon() : QIcon(QPixmap(info.thumbnailPath)));
            if (action->isCheckable()) action->setChecked(slot == selected);
        }
    }
}

void MainWindow::openRom(const QString& path)
{
    runCommand(QStringLiteral("gui.open_rom"), {{QStringLiteral("path"), path}});
}

void MainWindow::checkLibrary()
{
    const Library current = library();
    if (current.isUsable()) return;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("cuteNES"),
        tr("The library folder %1 is missing or cannot be written.").arg(QDir::toNativeSeparators(current.root())),
        QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("libraryUnusable"));
    box.setInformativeText(tr("Without it, battery saves are not kept."));
    QPushButton* recreate = box.addButton(tr("Recreate"), QMessageBox::AcceptRole);
    QPushButton* choose = box.addButton(tr("Choose Another…"), QMessageBox::ActionRole);
    box.addButton(tr("Continue Without Saving"), QMessageBox::RejectRole);
    box.setDefaultButton(recreate);
    box.exec();
    if (box.clickedButton() == recreate) {
        QString error;
        if (current.ensureLayout(&error)) {
            m_index->setLibrary(current);
        } else {
            QMessageBox::warning(this, QStringLiteral("cuteNES"), error);
        }
    } else if (box.clickedButton() == choose) {
        changeLibraryLocation();
    }
}

void MainWindow::changeLibraryLocation()
{
    const Library current = library();
    const QString root = QFileDialog::getExistingDirectory(this, tr("Choose the Library Location"),
        QFileInfo(current.root()).absolutePath());
    if (root.isEmpty() || QDir(root) == QDir(current.root())) return;
    bool move = false;
    if (current.isUsable() && hasContents(current)) {
        const auto answer = QMessageBox::question(this, QStringLiteral("cuteNES"),
            tr("Move your ROMs, saves and other files from %1 to %2?")
                .arg(QDir::toNativeSeparators(current.root()), QDir::toNativeSeparators(root)),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Cancel) return;
        move = answer == QMessageBox::Yes;
    }
    relocateLibrary(root, move);
}

QFuture<api::Result> MainWindow::relocateLibrary(const QString& root, bool moveContents)
{
    const QVariantMap setRoot = {{QStringLiteral("key"), QStringLiteral("library.root")},
        {QStringLiteral("value"), root}};
    if (!moveContents) return runCommand(QStringLiteral("gui.set_setting"), setRoot);
    // The open game's battery save lives in the library: close it first, so
    // nothing writes there while the files move.
    const Library from = library();
    return QtConcurrent::run(&m_commands, [this, from, root, setRoot] {
        m_session->call("gui.close_game");
        QString error;
        if (!from.moveContentsTo(Library(root), &error)) {
            return api::Result::fail("IoError", "could not move the library: " + error.toStdString());
        }
        return m_session->call("gui.set_setting", toJson(setRoot));
    }).then(this, [this](const api::Result& result) {
        if (result.isError()) showError(QStringLiteral("gui.set_setting"), *result.error);
        return result;
    });
}

void MainWindow::pushInput()
{
    for (const Port port : {Port::One, Port::Two}) {
        m_session->setLiveInput(port, m_input.buttons(port));
        m_session->setLiveTurbo(port, m_input.turbo(port), m_turboRate);
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Resize: // the application's filter sees every widget's
        if (watched == m_pages) placeOsd();
        if (watched == m_view) updateOverlays();
        break;
    case QEvent::FileOpen: // macOS "Open With" and drops on the Dock icon
        if (watched == qApp) {
            openRom(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        break;
    case QEvent::ShortcutOverride: { // a shortcut's key, before the action fires
        auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat()) m_keysDown.insert(key->key());
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        // Game keys while the game shows in this window and no menu or
        // dialog has the keyboard. Auto-repeats change nothing.
        auto* key = static_cast<QKeyEvent*>(event);
        // Fullscreen: Alt alone shows or hides the menu bar.
        if (event->type() == QEvent::KeyRelease && key->key() == Qt::Key_Alt && !key->isAutoRepeat()
            && isFullScreen() && !menuBar()->isNativeMenuBar() && m_keysDown == QSet<int>{Qt::Key_Alt}) {
            menuBar()->setVisible(!menuBar()->isVisible());
        }
        if (!key->isAutoRepeat() && event->type() == QEvent::KeyRelease && m_keysDown.remove(key->key())) {
            releaseHeld(key->key());
        } else if (!key->isAutoRepeat() && event->type() == QEvent::KeyPress) {
            m_keysDown.insert(key->key());
        }
        if (QApplication::activeWindow() != this || QApplication::activePopupWidget()
            || QApplication::activeModalWidget() || m_pages->currentWidget() != m_view) {
            break;
        }
        if (key->isAutoRepeat()) {
            return m_input.isBound(key->key());
        }
        if (m_input.handleKey(key->key(), event->type() == QEvent::KeyPress)) {
            pushInput();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::followFocus()
{
    // Our own dialogs keep the game going; another application pauses it
    // (FR-036), and coming back resumes what this paused.
    QWidget* active = QApplication::activeWindow();
    // No active window while one of ours is open is a handover (a dialog
    // opening or closing, which some platforms never activate), not the
    // player leaving; nor is it while the application is still active.
    if (!active) {
        if (QGuiApplication::applicationState() == Qt::ApplicationActive) return;
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top != this && top->isVisible() && top->parentWidget() && top->parentWidget()->window() == this) return;
        }
    }
    const bool ours = active == this || (active && active->parentWidget() && active->parentWidget()->window() == this);
    if (!ours) {
        if (!m_pausedByFocus && m_settings.emulation().pauseWhenInactive && m_runState == QLatin1String("Running")) {
            m_pausedByFocus = true;
            runCommand(QStringLiteral("core.pause"), {}, true);
        }
    } else if (m_pausedByFocus) {
        m_pausedByFocus = false;
        if (hasGame()) runCommand(QStringLiteral("core.resume"), {}, true);
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ActivationChange) {
        // Once the activation settles: a dialog of ours may be taking it.
        QTimer::singleShot(0, this, &MainWindow::followFocus);
    }
    // Keys held while the window loses focus would stay down (G015).
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        m_input.clear();
        pushInput();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // FR-012: write the battery save before quitting. The handler never
    // waits for the GUI, so waiting for it here is safe.
    m_closing = true;
    m_session->call("gui.close_game");
    m_settings.sync();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && isRom(urls.first())) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !isRom(urls.first())) return;
    event->acceptProposedAction();
    openRom(urls.first().toLocalFile());
}

void MainWindow::showAbout()
{
    QMetaObject::invokeMethod(this, [this] {
        auto* dialog = new AboutDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show(); // non-modal: a command that opens a window returns at once (R11)
    }, Qt::QueuedConnection);
}

} // namespace cutenes::app
