#pragma once

// Help > User Guide (spec FR-020 "Help"): docs/user-guide.md, built into the
// application as a resource and shown as rendered Markdown, so the guide
// works offline and always describes the version that shows it. Links to
// the web open in the browser.

#include <QDialog>

namespace cutenes::app {

class UserGuideWindow final : public QDialog {
    Q_OBJECT

public:
    explicit UserGuideWindow(QWidget* parent = nullptr);
};

} // namespace cutenes::app
