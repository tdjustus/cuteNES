#include "dialogs/user_guide.hpp"

#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace cutenes::app {

UserGuideWindow::UserGuideWindow(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("userGuide"));
    setWindowTitle(tr("cuteNES User Guide"));
    auto* browser = new QTextBrowser(this);
    browser->setObjectName(QStringLiteral("userGuideText"));
    browser->setOpenExternalLinks(true);
    QFile file(QStringLiteral(":/docs/user-guide.md"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        browser->setMarkdown(QString::fromUtf8(file.readAll()));
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(browser);
    resize(720, 640);
}

} // namespace cutenes::app
