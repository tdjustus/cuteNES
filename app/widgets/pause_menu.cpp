#include "widgets/pause_menu.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace cutenes::app {

PauseMenu::PauseMenu(const QString& labelStyle, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("pauseMenu"));
    // The menu itself is see-through: only the label and buttons draw.
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_label = new QLabel(tr("PAUSED"), this);
    m_label->setObjectName(QStringLiteral("pausedOverlay"));
    m_label->setStyleSheet(labelStyle);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_label, 0, Qt::AlignHCenter);

    const auto add = [&](const QString& text, const char* name, void (PauseMenu::*request)()) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setFocusPolicy(Qt::StrongFocus);
        b->setAutoDefault(false);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { color: white; background: rgba(0, 0, 0, 170); border: 2px solid transparent;"
            " border-radius: 6px; padding: 6px 18px; font-size: 15px; font-weight: 700; min-width: 140px; }"
            " QPushButton:hover { background: rgba(40, 40, 40, 200); }"
            " QPushButton:focus { border-color: white; }"));
        b->installEventFilter(this);
        connect(b, &QPushButton::clicked, this, request);
        layout->addWidget(b, 0, Qt::AlignHCenter);
        return b;
    };
    m_resume = add(tr("Resume"), "pauseResume", &PauseMenu::resumeRequested);
    m_save = add(tr("Save"), "pauseSave", &PauseMenu::saveRequested);
    m_battery = add(tr("Battery Save"), "pauseBatterySave", &PauseMenu::batterySaveRequested);
    m_quit = add(tr("Quit"), "pauseQuit", &PauseMenu::quitRequested);
    hide();
}

void PauseMenu::setMode(Mode mode, bool battery)
{
    m_mode = mode;
    const bool full = mode == Mode::Full;
    m_save->setVisible(full);
    m_battery->setVisible(full && battery);
    m_quit->setVisible(full);
    setVisible(mode != Mode::None);
    adjustSize();
}

bool PauseMenu::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QPushButton*>(watched);
    if (!button || event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    const int key = static_cast<QKeyEvent*>(event)->key();
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Left:
    case Qt::Key_Down:
    case Qt::Key_Right: {
        // The shown buttons, in order, wrapping round.
        QList<QPushButton*> shown;
        for (QPushButton* b : {m_resume, m_save, m_battery, m_quit}) {
            if (b->isVisibleTo(this)) shown << b;
        }
        const qsizetype at = shown.indexOf(button);
        if (at < 0) return true;
        const qsizetype step = key == Qt::Key_Up || key == Qt::Key_Left ? -1 : 1;
        shown[(at + step + shown.size()) % shown.size()]->setFocus(Qt::TabFocusReason);
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        button->click(); // Space presses it already
        return true;
    case Qt::Key_Escape:
        emit resumeRequested();
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

} // namespace cutenes::app
