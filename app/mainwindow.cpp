#include "mainwindow.hpp"

#include "about_dialog.hpp"
#include "dialogs/settings_dialog.hpp"
#include "library/library_browser.hpp"
#include "library/library_index.hpp"

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/emulation_view.hpp>
#include <cutenes/qt/frame_clock.hpp>

#include <QAction>
#include <QtMath>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QUrl>
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
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

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
    QStringLiteral("gui.open_rom"), QStringLiteral("gui.close_game"),
    QStringLiteral("gui.toggle_pause"), QStringLiteral("gui.reset"),
    QStringLiteral("gui.power_cycle"), QStringLiteral("gui.exit"),
    QStringLiteral("gui.show_about"), QStringLiteral("gui.show_library"),
    QStringLiteral("gui.open_library_folder"), QStringLiteral("gui.clear_recent"),
    QStringLiteral("gui.show_settings"),
};
const QStringList kSettingsPages = {QStringLiteral("library")};
const QStringList kNeedGame = {
    QStringLiteral("gui.close_game"), QStringLiteral("gui.toggle_pause"),
    QStringLiteral("gui.reset"), QStringLiteral("gui.power_cycle"),
};

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
    m_pages->addWidget(m_browser);
    m_pages->addWidget(m_view);
    setCentralWidget(m_pages);
    m_audio = std::make_unique<qt::AudioOutput>();
    m_audio->open(); // silent if no device (spec US7-5)

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

    m_host = std::make_unique<GuiHost>(static_cast<GuiHost::Window&>(*this), m_settings, m_battery);
    api::SessionConfig config;
    config.host = m_host.get();
    config.libraryRoot = m_library.folder(Library::Folder::Roms).toStdString();
    config.debugDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath(QStringLiteral("debug"))
                          .toStdString();
    m_session = std::make_unique<api::Session>(config);
    m_host->attach(*m_session);
    m_session->setMediaSinks(m_view, m_audio.get());
    m_session->setRealtime(true);

    // Timer pacing at the console's rate (R4); frames requested while
    // nothing runs are dropped by the session.
    m_clock = std::make_unique<qt::FrameClock>(
        [session = m_session.get()](std::uint32_t frames) { session->requestFrames(frames); });
    m_clock->start(kNtscFps);

    m_commands.setMaxThreadCount(2);
    createMenus();
    wireActions();
    rebuildRecentMenu();
    setRunState(QStringLiteral("NeedsRom"));

    // Rounded up, so the picture fits at exactly that scale.
    const QSizeF picture = qt::scaledSize(kDefaultScale, m_view->displayOptions());
    const int menuHeight = menuBar()->isNativeMenuBar() ? 0 : menuBar()->sizeHint().height();
    resize(qCeil(picture.width()), qCeil(picture.height()) + menuHeight);
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    m_clock->stop();
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
    file.submenu(tr("&Save State")).items(slotList, "gui.save_state", "slot", slotText,
        functionKey(Qt::ShiftModifier));
    file.submenu(tr("Load S&tate")).items(slotList, "gui.load_state", "slot", slotText,
        functionKey(Qt::NoModifier));
    file.item(tr("&Quick Save"), "gui.quick_save", QKeySequence(Qt::CTRL | Qt::Key_S));
    file.item(tr("Quick &Load"), "gui.quick_load", QKeySequence(Qt::CTRL | Qt::Key_L));
    file.submenu(tr("Select Slo&t")).items(slotList, "gui.select_slot", "slot", slotText,
        [](const QVariant& slot) {
            // Ctrl+1 … Ctrl+9, then Ctrl+0 for slot 10.
            return QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_0 + slot.toInt() % 10));
        });
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
    QAction* pause = emulation.item(tr("&Pause"), "gui.toggle_pause", QKeySequence(Qt::Key_P));
    pause->setShortcuts({QKeySequence(Qt::Key_P), QKeySequence(Qt::Key_Pause)});
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
    palette.item(tr("Load .pal…"), "gui.set_palette");
    view.separator();
    view.item(tr("Show &FPS"), "gui.toggle_fps");
    view.item(tr("Show On-Screen &Messages"), "gui.toggle_osd");
    view.item(tr("Always on &Top"), "gui.toggle_always_on_top");

    MenuBuilder tools(menu(QStringLiteral("menuTools")));
    tools.item(tr("&Cheats…"), "gui.show_cheats");
    tools.item(tr("Cheat &Search…"), "gui.show_cheat_search");
    tools.separator();
    MenuBuilder movie = tools.submenu(tr("&Movie"));
    movie.item(tr("&Record…"), "gui.record_movie");
    movie.item(tr("&Play…"), "gui.play_movie");
    movie.item(tr("&Stop"), "gui.stop_movie");
    movie.item(tr("Read-&Only"), "gui.toggle_movie_read_only");
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
        connect(action, &QAction::triggered, this, [this, action, command] {
            runCommand(command, action->property("commandArgs").toMap());
        });
    }
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
    m_clock->setRate(m_session->machine().region() == Region::Pal ? kPalFps : kNtscFps);
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
    if (event.type != "run_state_changed") return;
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
        if (kNeedGame.contains(action->property("command").toString())) action->setEnabled(game);
    }
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
    m_session->setLiveInput(Port::One, m_input.buttons(Port::One));
    m_session->setLiveInput(Port::Two, m_input.buttons(Port::Two));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::FileOpen: // macOS "Open With" and drops on the Dock icon
        if (watched == qApp) {
            openRom(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        // Game keys while the game shows in this window and no menu or
        // dialog has the keyboard. Auto-repeats change nothing.
        auto* key = static_cast<QKeyEvent*>(event);
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

void MainWindow::changeEvent(QEvent* event)
{
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
