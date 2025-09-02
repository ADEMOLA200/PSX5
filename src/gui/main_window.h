#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QTreeView>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QSettings>
#include <memory>

class Emulator;
class EmulatorThread;
class GameListModel;
class LogWidget;
class SettingsDialog;
class PSNManager;
class TrophyWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openGame();
    void bootFirmware();
    void runEmulation();
    void pauseEmulation();
    void stopEmulation();
    void showSettings();
    void showTrophies();
    void showAbout();
    void psnLogin();
    void psnLogout();
    void psnManageAccounts();
    void updateStatus();
    void onGameDoubleClicked(const QModelIndex &index);
    
    void onEmulationStarted();
    void onEmulationPaused();
    void onEmulationStopped();
    void onEmulationError(const QString &error);
    void onFpsUpdated(int fps);
    void onStatusUpdated(const QString &status);

private:
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupGameList();
    void setupLogDock();
    void loadSettings();
    void saveSettings();
    void updatePSNStatus();
    void closeEvent(QCloseEvent *event) override;

    // Core components
    std::shared_ptr<Emulator> m_emulator;
    EmulatorThread *m_emulatorThread;
    
    // GUI components
    QWidget *m_centralWidget;
    QVBoxLayout *m_centralLayout;
    QTableView *m_gameListView;
    GameListModel *m_gameListModel;
    
    // Docks
    QDockWidget *m_logDock;
    LogWidget *m_logWidget;
    
    // Dialogs
    SettingsDialog *m_settingsDialog;
    PSNManager *m_psnManager;
    TrophyWindow *m_trophyWindow;
    
    // Status bar widgets
    QLabel *m_fpsLabel;
    QLabel *m_psnStatusLabel;
    QLabel *m_gameStatusLabel;
    QProgressBar *m_progressBar;
    
    // Timers
    QTimer *m_statusTimer;
    
    // Settings
    QSettings *m_settings;
    
    // State
    bool m_emulationRunning;
    QString m_currentGamePath;
    QString m_psnUsername;
};
