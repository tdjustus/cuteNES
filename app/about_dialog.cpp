#include "about_dialog.hpp"

#include <cutenes/api/registry.hpp>
#include <cutenes/version.hpp>

#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace cutenes::app {

namespace {

struct License {
    const char* name;
    const char* note;
    const char* resource;
};

// One tab per component. Qt is dynamically linked under LGPL-3.0, which
// incorporates the GPL-3.0's terms, so both texts ship (research R1).
const License kLicenses[] = {
    {"cuteNES", "MIT License.", ":/licenses/cutenes.txt"},
    {"Qt",
        "This application uses the Qt framework under the GNU Lesser General "
        "Public License, version 3, dynamically linked so that it can be replaced. "
        "Qt's source code is available from https://download.qt.io/.",
        ":/licenses/qt-lgpl-3.0.txt"},
    {"Qt (GPL-3.0)", "The GNU GPL v3, which the LGPL v3 incorporates.",
        ":/licenses/qt-gpl-3.0.txt"},
    {"SDL3", "Simple DirectMedia Layer, zlib License (audio and gamepads).",
        ":/licenses/sdl3.txt"},
    {"nlohmann/json", "MIT License.", ":/licenses/nlohmann-json.txt"},
    {"cpp-httplib", "MIT License.", ":/licenses/cpp-httplib.txt"},
    {"stb_image_write", "Public domain or MIT License, at your choice; the text is in "
        "the header, third_party/stb/stb_image_write.h.", nullptr},
};

QString readResource(const char* path)
{
    if (path == nullptr) return {};
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

QString AboutDialog::versionText()
{
    return QStringLiteral("cuteNES %1 · core %2 · API %3 · Qt %4")
        .arg(QStringLiteral(CUTENES_APP_VERSION),
            QString::fromLatin1(cutenes::coreVersionString()),
            QString::fromStdString(cutenes::api::surfaceVersion()),
            QString::fromLatin1(qVersion()));
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About cuteNES"));
    setObjectName(QStringLiteral("aboutDialog"));

    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("<h2>cuteNES</h2><p>An accurate, AI-drivable NES emulator.</p>"), this);
    layout->addWidget(title);
    auto* versions = new QLabel(versionText(), this);
    versions->setObjectName(QStringLiteral("versions"));
    versions->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(versions);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("licenses"));
    for (const License& license : kLicenses) {
        auto* text = new QTextBrowser(tabs);
        text->setPlainText(QString::fromUtf8(license.note) + QStringLiteral("\n\n")
            + readResource(license.resource));
        tabs->addTab(text, QString::fromUtf8(license.name));
    }
    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(640, 480);
}

} // namespace cutenes::app
