#pragma once

// Settings (contracts/menus-and-commands.md "Settings"): one page per
// group. G2 fills the Library page and G3 the Servers page; the others
// arrive with their phases.

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QSpinBox;
class QPushButton;
class QStackedWidget;

namespace cutenes::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // "video", "audio", "input", "hotkeys", "emulation", "library" or "servers".
    void showPage(const QString& page);
    QString currentPage() const;
    void setLibraryRoot(const QString& root);
    // The folders the library scans: its own roms folder (always first, not
    // removable) and the added ones.
    void setRomFolders(const QString& roms, const QStringList& added);

    // Settings > Servers: what is saved, and what is running.
    struct Server {
        bool enabled = false;
        int port = 0;
        bool running = false;
        QString url;
        QString token;
        QString tokenFile;
    };
    void setServers(const Server& api, const Server& mcp);

signals:
    void pageShown(const QString& page);
    void changeLibraryRequested();
    void openLibraryFolderRequested();
    void addRomFolderRequested();
    void removeRomFolderRequested(const QString& path);
    // A Servers control changed: gui.set_setting with this key and value.
    void serverSettingRequested(const QString& key, const QVariant& value);

private:
    QListWidget* m_pages;
    QStackedWidget* m_stack;
    QLabel* m_libraryRoot;
    QListWidget* m_romFolders = nullptr;
    QPushButton* m_removeFolder = nullptr;
    struct ServerControls {
        QCheckBox* enabled = nullptr;
        QSpinBox* port = nullptr;
        QLabel* address = nullptr;
        QPushButton* copy = nullptr;
        QString token;
    };
    ServerControls m_api;
    ServerControls m_mcp;
    QLabel* m_mcpTokenFile = nullptr;
    QWidget* buildServersPage(QWidget* parent);
};

} // namespace cutenes::app
