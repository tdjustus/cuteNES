#pragma once

// The update prompt (contract "Updates"; spec FR-093, FR-096): the new
// version and the running one, the release notes, and Install (only where
// this build can replace itself; otherwise a line saying why), Ask Again
// Later, Skip This Version and See Details.

#include "update/update_feed.hpp"

#include <QDialog>

namespace cutenes::app {

class UpdatePrompt final : public QDialog {
    Q_OBJECT

public:
    UpdatePrompt(const update::Release& release, const update::Version& running, bool canInstall,
        const QString& whyNot, QWidget* parent = nullptr);

signals:
    void installRequested();
    void skipRequested();
    void detailsRequested();
};

} // namespace cutenes::app
