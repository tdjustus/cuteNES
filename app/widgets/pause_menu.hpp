#pragma once

// The pause menu (spec FR-038, contract "Pause menu", #235): PAUSED over the
// middle of the picture, with buttons under it. A pause the player chose
// shows Resume, Save, Battery Save (a game with battery RAM only) and Quit;
// any other pause shows only Resume. The arrow keys move between the
// buttons, wrapping round, Enter or Space presses one, and Esc resumes; a
// gamepad's navigation buttons arrive as those keys (MainWindow).
//
// The menu only asks: each request runs a gui.* command in MainWindow.

#include <QWidget>

class QLabel;
class QPushButton;

namespace cutenes::app {

class PauseMenu final : public QWidget {
    Q_OBJECT

public:
    enum class Mode { None, Resume, Full };

    explicit PauseMenu(const QString& labelStyle, QWidget* parent = nullptr);

    // Shows the buttons `mode` has; Battery Save only with `battery`.
    // Mode::None hides the menu.
    void setMode(Mode mode, bool battery);
    Mode mode() const { return m_mode; }
    QLabel* label() const { return m_label; }
    QPushButton* resumeButton() const { return m_resume; }

signals:
    void resumeRequested();
    void saveRequested();
    void batterySaveRequested();
    void quitRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLabel* m_label = nullptr;
    QPushButton* m_resume = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_battery = nullptr;
    QPushButton* m_quit = nullptr;
    Mode m_mode = Mode::None;
};

} // namespace cutenes::app
