#pragma once

// Help > About (spec FR-020 "Help"): the application, core and API surface
// versions, and every license the application ships under or with
// (constitution: Qt's LGPL-3.0 notices).

#include <QDialog>

namespace cutenes::app {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

    // "cuteNES 0.2.0 · core 0.1.0 · API 0.1 · Qt 6.12.0"
    static QString versionText();
};

} // namespace cutenes::app
