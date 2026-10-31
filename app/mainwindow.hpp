#pragma once

// The main window (spec US3, contracts/menus-and-commands.md). Every menu
// item of the contract is present from G0 on; an item stays disabled until
// the phase that implements it (tasks G034).
//
// The window hosts the one api Session (research R3): the GUI, the API and
// MCP share its queue. Every implemented menu action runs its gui.* command
// through that queue (R11).

#include "host/gui_host.hpp"

#include <cutenes/api/session.hpp>
#include <cutenes/qt/input_mapper.hpp>

#include <QFuture>
#include <QMainWindow>
#include <QThreadPool>
#include <QVariantMap>

#include <memory>

class QMenu;

namespace cutenes::qt {
class AudioOutput;
class EmulationView;
class FrameClock;
} // namespace cutenes::qt

namespace cutenes::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // A top-level menu by its object name: "menuFile", "menuEmulation",
    // "menuView", "menuTools", "menuDebug", "menuSettings" or "menuHelp".
    QMenu* menu(const QString& objectName) const;

    api::Session& session() { return *m_session; }
    qt::EmulationView* view() const { return m_view; }
    qt::AudioOutput* audio() const { return m_audio.get(); }

    // Runs a command on the session without blocking the GUI thread. A
    // failure is shown to the user unless `quiet`.
    QFuture<api::Result> runCommand(const QString& name, const QVariantMap& args = {},
        bool quiet = false);

public slots:
    void showAbout();
    void showOpenRomDialog();
    void openRom(const QString& path);

signals:
    // A command the user started failed (after it was shown).
    void commandFailed(const QString& name, const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void createMenus();
    void wireActions();
    void onSessionEvent(const api::Event& event); // emulation thread
    void setRunState(const QString& runState);    // GUI thread
    void pushInput();
    void showError(const QString& name, const api::Error& error);

    std::unique_ptr<GuiHost> m_host;
    std::unique_ptr<qt::AudioOutput> m_audio;
    qt::EmulationView* m_view = nullptr;
    std::unique_ptr<api::Session> m_session;
    std::unique_ptr<qt::FrameClock> m_clock;
    qt::InputMapper m_input;
    QThreadPool m_commands;
    QString m_romName;
};

} // namespace cutenes::app
