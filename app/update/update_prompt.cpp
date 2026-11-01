#include "update/update_prompt.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace cutenes::app {

UpdatePrompt::UpdatePrompt(const update::Release& release, const update::Version& running, bool canInstall,
    const QString& whyNot, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("updatePrompt"));
    setWindowTitle(tr("cuteNES Update"));
    auto* version = new QLabel(tr("cuteNES %1 is available (you have %2).")
                                   .arg(release.version.toString(), running.toString()),
        this);
    version->setObjectName(QStringLiteral("updateVersion"));
    QFont font = version->font();
    font.setWeight(QFont::DemiBold);
    version->setFont(font);
    auto* notes = new QTextBrowser(this);
    notes->setObjectName(QStringLiteral("updateNotes"));
    notes->setOpenExternalLinks(true);
    notes->setMarkdown(release.notes);

    auto* buttons = new QDialogButtonBox(this);
    const auto add = [&](const QString& text, const char* name, QDialogButtonBox::ButtonRole role) {
        QPushButton* b = buttons->addButton(text, role);
        b->setObjectName(QString::fromLatin1(name));
        return b;
    };
    if (canInstall) {
        QPushButton* install = add(tr("Install"), "updateInstall", QDialogButtonBox::AcceptRole);
        install->setDefault(true);
        connect(install, &QPushButton::clicked, this, [this] {
            accept();
            emit installRequested();
        });
    }
    QPushButton* later = add(tr("Ask Again Later"), "updateLater", QDialogButtonBox::RejectRole);
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    QPushButton* skip = add(tr("Skip This Version"), "updateSkip", QDialogButtonBox::DestructiveRole);
    connect(skip, &QPushButton::clicked, this, [this] {
        reject();
        emit skipRequested();
    });
    QPushButton* details = add(tr("See Details"), "updateDetails", QDialogButtonBox::HelpRole);
    if (!canInstall) details->setDefault(true);
    connect(details, &QPushButton::clicked, this, [this] {
        reject();
        emit detailsRequested();
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(version);
    layout->addWidget(notes, 1);
    if (!canInstall) {
        auto* why = new QLabel(whyNot, this);
        why->setObjectName(QStringLiteral("updateWhyNoInstall"));
        why->setWordWrap(true);
        layout->addWidget(why);
    }
    layout->addWidget(buttons);
    resize(520, 400);
}

} // namespace cutenes::app
