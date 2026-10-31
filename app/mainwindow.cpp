#include "mainwindow.hpp"

#include "about_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QVariant>

namespace cutenes::app {

namespace {

// 3x the NES picture at 8:7 pixel aspect (data-model "Settings": scale 3).
constexpr int kDefaultWidth = 256 * 3 * 8 / 7;
constexpr int kDefaultHeight = 240 * 3;

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

private:
    QMenu* m_menu;
};

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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("cuteNES"));
    resize(kDefaultWidth, kDefaultHeight);

    auto* placeholder = new QLabel(tr("Open a ROM to start playing."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    createMenus();
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
    file.submenu(tr("Open &Recent")).item(tr("Clear List"), "gui.clear_recent");
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
    exitAction->setEnabled(true);
    exitAction->setToolTip({});
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

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
    about->setEnabled(true);
    about->setToolTip({});
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    auto* dialog = new AboutDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show(); // non-modal: a command that opens a window returns at once (R11)
}

} // namespace cutenes::app
