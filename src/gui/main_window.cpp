#include "main_window.h"
#include "game_list_model.h"
#include "log_widget.h"
#include "settings_dialog.h"
#include "psn_manager.h"
#include "trophy_window.h"
#include "emulator_thread.h"
#include "runtime/emulator.h"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSplitter>
#include <QHeaderView>
#include <QStandardPaths>
#include <QDir>
#include <fstream>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_emulator(std::make_shared<Emulator>(1 << 24))
    , m_centralWidget(nullptr)
    , m_centralLayout(nullptr)
    , m_gameListView(nullptr)
    , m_gameListModel(nullptr)
    , m_logDock(nullptr)
    , m_logWidget(nullptr)
    , m_settingsDialog(nullptr)
    , m_psnManager(nullptr)
    , m_trophyWindow(nullptr)
    , m_fpsLabel(nullptr)
    , m_psnStatusLabel(nullptr)
    , m_gameStatusLabel(nullptr)
    , m_progressBar(nullptr)
    , m_statusTimer(nullptr)
    , m_settings(nullptr)
    , m_emulationRunning(false)
    , m_emulatorThread(nullptr)
{
    setWindowTitle("PSX5 Emulator");
    setMinimumSize(1024, 768);
    resize(1280, 800);

    m_emulatorThread = new EmulatorThread(this);
    m_emulatorThread->setEmulator(m_emulator);
    
    // Connect emulator thread signals
    connect(m_emulatorThread, &EmulatorThread::emulationStarted, 
            this, &MainWindow::onEmulationStarted);
    connect(m_emulatorThread, &EmulatorThread::emulationPaused, 
            this, &MainWindow::onEmulationPaused);
    connect(m_emulatorThread, &EmulatorThread::emulationStopped, 
            this, &MainWindow::onEmulationStopped);
    connect(m_emulatorThread, &EmulatorThread::emulationError, 
            this, &MainWindow::onEmulationError);
    connect(m_emulatorThread, &EmulatorThread::fpsUpdated, 
            this, &MainWindow::onFpsUpdated);
    connect(m_emulatorThread, &EmulatorThread::statusUpdated, 
            this, &MainWindow::onStatusUpdated);

    // Initialize settings
    m_settings = new QSettings("PSX5", "Emulator", this);
    
    // Setup UI components
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupGameList();
    setupLogDock();
    
    // Setup status timer
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start(1000); // Update every second
    
    // Load settings
    loadSettings();
    
    // Update initial status
    updatePSNStatus();
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete m_emulatorThread;
}

void MainWindow::setupMenuBar()
{
    // File menu
    QMenu *fileMenu = menuBar()->addMenu("&File");
    
    QAction *openGameAction = fileMenu->addAction("&Open Game...");
    openGameAction->setShortcut(QKeySequence::Open);
    connect(openGameAction, &QAction::triggered, this, &MainWindow::openGame);
    
    QAction *bootFirmwareAction = fileMenu->addAction("&Boot Firmware");
    connect(bootFirmwareAction, &QAction::triggered, this, &MainWindow::bootFirmware);
    
    fileMenu->addSeparator();
    
    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    
    // Emulation menu
    QMenu *emulationMenu = menuBar()->addMenu("&Emulation");
    
    QAction *runAction = emulationMenu->addAction("&Run");
    runAction->setShortcut(Qt::Key_F5);
    connect(runAction, &QAction::triggered, this, &MainWindow::runEmulation);
    
    QAction *pauseAction = emulationMenu->addAction("&Pause");
    pauseAction->setShortcut(Qt::Key_F6);
    connect(pauseAction, &QAction::triggered, this, &MainWindow::pauseEmulation);
    
    QAction *stopAction = emulationMenu->addAction("&Stop");
    stopAction->setShortcut(Qt::Key_F7);
    connect(stopAction, &QAction::triggered, this, &MainWindow::stopEmulation);
    
    // Settings menu
    QMenu *settingsMenu = menuBar()->addMenu("&Settings");
    
    QAction *settingsAction = settingsMenu->addAction("&Configure...");
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    
    // PSN menu
    QMenu *psnMenu = menuBar()->addMenu("&PSN");
    
    QAction *psnLoginAction = psnMenu->addAction("&Login...");
    connect(psnLoginAction, &QAction::triggered, this, &MainWindow::psnLogin);
    
    QAction *psnLogoutAction = psnMenu->addAction("Log&out");
    connect(psnLogoutAction, &QAction::triggered, this, &MainWindow::psnLogout);
    
    psnMenu->addSeparator();
    
    QAction *psnManageAction = psnMenu->addAction("&Manage Accounts...");
    connect(psnManageAction, &QAction::triggered, this, &MainWindow::psnManageAccounts);
    
    // Tools menu
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    
    QAction *trophiesAction = toolsMenu->addAction("&Trophies...");
    connect(trophiesAction, &QAction::triggered, this, &MainWindow::showTrophies);
    
    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    
    QAction *aboutAction = helpMenu->addAction("&About PSX5...");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::setupToolBar()
{
    QToolBar *mainToolBar = addToolBar("Main");
    
    QAction *openAction = mainToolBar->addAction("Open");
    connect(openAction, &QAction::triggered, this, &MainWindow::openGame);
    
    mainToolBar->addSeparator();
    
    QAction *runAction = mainToolBar->addAction("Run");
    connect(runAction, &QAction::triggered, this, &MainWindow::runEmulation);
    
    QAction *pauseAction = mainToolBar->addAction("Pause");
    connect(pauseAction, &QAction::triggered, this, &MainWindow::pauseEmulation);
    
    QAction *stopAction = mainToolBar->addAction("Stop");
    connect(stopAction, &QAction::triggered, this, &MainWindow::stopEmulation);
}

void MainWindow::setupStatusBar()
{
    m_fpsLabel = new QLabel("FPS: 0");
    m_psnStatusLabel = new QLabel("PSN: Offline");
    m_gameStatusLabel = new QLabel("No game loaded");
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    
    statusBar()->addWidget(m_fpsLabel);
    statusBar()->addPermanentWidget(m_psnStatusLabel);
    statusBar()->addPermanentWidget(m_gameStatusLabel);
    statusBar()->addPermanentWidget(m_progressBar);
}

void MainWindow::setupGameList()
{
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    
    m_centralLayout = new QVBoxLayout(m_centralWidget);
    
    m_gameListModel = new GameListModel(this);
    m_gameListView = new QTableView;
    m_gameListView->setModel(m_gameListModel);
    m_gameListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_gameListView->setAlternatingRowColors(true);
    m_gameListView->setSortingEnabled(true);
    m_gameListView->horizontalHeader()->setStretchLastSection(true);
    
    connect(m_gameListView, &QTableView::doubleClicked, 
            this, &MainWindow::onGameDoubleClicked);
    
    m_centralLayout->addWidget(m_gameListView);
}

void MainWindow::setupLogDock()
{
    m_logWidget = new LogWidget;
    m_logDock = new QDockWidget("Log Output", this);
    m_logDock->setWidget(m_logWidget);
    m_logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    
    // Add toggle action to View menu
    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_logDock->toggleViewAction());
}

void MainWindow::openGame()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Open Game",
        QString(),
        "PlayStation Files (*.elf *.pkg);;All Files (*)"
    );
    
    if (!fileName.isEmpty()) {
        std::ifstream file(fileName.toStdString(), std::ios::binary | std::ios::ate);
        if (!file) {
            QMessageBox::warning(this, "Error", "Failed to open file");
            return;
        }
        
        auto size = file.tellg();
        std::vector<uint8_t> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        
        if (m_emulator->load_module(bytes, 0x1000)) {
            m_currentGamePath = fileName;
            m_gameStatusLabel->setText(QString("Loaded: %1").arg(QFileInfo(fileName).baseName()));
            m_logWidget->addMessage(QString("Loaded game: %1").arg(fileName));
        } else {
            QMessageBox::warning(this, "Error", "Failed to load game module");
            m_logWidget->addMessage(QString("Failed to load game: %1").arg(fileName), LogWidget::Error);
        }
    }
}

void MainWindow::bootFirmware()
{
    QString firmwarePath = m_settings->value("system/firmwarePath").toString();
    if (firmwarePath.isEmpty()) {
        QMessageBox::information(this, "Boot Firmware", 
            "Please configure firmware path in Settings first.");
        showSettings();
        return;
    }
    
    QDir firmwareDir(firmwarePath);
    if (!firmwareDir.exists()) {
        QMessageBox::warning(this, "Error", 
            "Firmware directory does not exist. Please check your settings.");
        return;
    }
    
    m_logWidget->addMessage("Booting firmware...");
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 100);
    
    // Look for firmware files
    QStringList firmwareFiles = firmwareDir.entryList(
        QStringList() << "*.bin" << "*.elf" << "*.self", 
        QDir::Files
    );
    
    if (firmwareFiles.isEmpty()) {
        QMessageBox::warning(this, "Error", 
            "No firmware files found in the specified directory.");
        m_progressBar->setVisible(false);
        return;
    }
    
    // Load firmware files
    bool success = true;
    int progress = 0;
    
    for (const QString &fileName : firmwareFiles) {
        QString fullPath = firmwareDir.absoluteFilePath(fileName);
        m_logWidget->addMessage(QString("Loading firmware file: %1").arg(fileName));
        
        QFile file(fullPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            
            // Determine load address based on file type
            uint64_t loadAddress = 0x1000000; // Default firmware load address
            if (fileName.contains("kernel")) {
                loadAddress = 0x800000;
            } else if (fileName.contains("bootloader")) {
                loadAddress = 0x100000;
            }
            
            // Load into emulator
            std::vector<uint8_t> bytes(data.begin(), data.end());
            if (!m_emulator->load_module(bytes, loadAddress)) {
                m_logWidget->addMessage(QString("Failed to load firmware file: %1").arg(fileName), LogWidget::Error);
                success = false;
                break;
            }
            
            progress += (100 / firmwareFiles.size());
            m_progressBar->setValue(progress);
            
            // Process events to keep UI responsive
            QApplication::processEvents();
        } else {
            m_logWidget->addMessage(QString("Failed to open firmware file: %1").arg(fileName), LogWidget::Error);
            success = false;
            break;
        }
    }
    
    m_progressBar->setVisible(false);
    
    if (success) {
        m_logWidget->addMessage("Firmware boot completed successfully");
        m_gameStatusLabel->setText("Firmware loaded");
        
        // Initialize firmware environment
        initializeFirmwareEnvironment();
    } else {
        QMessageBox::warning(this, "Error", "Firmware boot failed. Check log for details.");
    }
}

void MainWindow::initializeFirmwareEnvironment()
{
    // Set up firmware-specific environment
    m_emulator->set_register(0, 0x1000000); // Set entry point
    m_emulator->set_register(1, 0x2000000); // Set stack pointer
    
    // Initialize system calls
    m_emulator->initialize_syscalls();
    
    m_logWidget->addMessage("Firmware environment initialized");
}

void MainWindow::runEmulation()
{
    if (m_currentGamePath.isEmpty()) {
        QMessageBox::information(this, "Run Emulation", "Please load a game first.");
        return;
    }
    
    if (m_emulatorThread->getState() == EmulatorThread::Stopped) {
        m_emulatorThread->startEmulation();
    } else if (m_emulatorThread->getState() == EmulatorThread::Paused) {
        m_emulatorThread->startEmulation();
    }
}

void MainWindow::pauseEmulation()
{
    if (m_emulatorThread->getState() == EmulatorThread::Running) {
        m_emulatorThread->pauseEmulation();
    }
}

void MainWindow::stopEmulation()
{
    if (m_emulatorThread->getState() != EmulatorThread::Stopped) {
        m_emulatorThread->stopEmulation();
    }
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
    }
    m_settingsDialog->exec();
}

void MainWindow::showTrophies()
{
    if (!m_trophyWindow) {
        m_trophyWindow = new TrophyWindow(this);
    }
    m_trophyWindow->show();
    m_trophyWindow->raise();
    m_trophyWindow->activateWindow();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About PSX5", 
        "PSX5 Emulator\n\n"
        "A PlayStation 5 emulator with Qt GUI.\n"
        "Built with modern C++ and Qt6.");
}

void MainWindow::psnLogin()
{
    if (!m_psnManager) {
        m_psnManager = new PSNManager(this);
    }
    
    if (m_psnManager->showLoginDialog()) {
        m_psnUsername = m_psnManager->getCurrentUsername();
        updatePSNStatus();
        m_logWidget->addMessage(QString("PSN login successful: %1").arg(m_psnUsername));
    }
}

void MainWindow::psnLogout()
{
    if (!m_psnManager) return;
    
    m_psnManager->logout();
    m_psnUsername.clear();
    updatePSNStatus();
    m_logWidget->addMessage("PSN logout successful");
}

void MainWindow::psnManageAccounts()
{
    if (!m_psnManager) {
        m_psnManager = new PSNManager(this);
    }
    m_psnManager->showAccountManager();
}

void MainWindow::updateStatus()
{
    EmulatorThread::State state = m_emulatorThread->getState();
    
    // Update status based on emulator state
    switch (state) {
    case EmulatorThread::Running:
        // FPS is updated via signal, no need to update here
        break;
    case EmulatorThread::Paused:
        m_fpsLabel->setText("FPS: Paused");
        break;
    case EmulatorThread::Stopped:
        m_fpsLabel->setText("FPS: 0");
        break;
    }
}

void MainWindow::onEmulationStarted()
{
    m_emulationRunning = true;
    m_gameStatusLabel->setText(QString("Running: %1").arg(QFileInfo(m_currentGamePath).baseName()));
}

void MainWindow::onEmulationPaused()
{
    m_gameStatusLabel->setText(QString("Paused: %1").arg(QFileInfo(m_currentGamePath).baseName()));
}

void MainWindow::onEmulationStopped()
{
    m_emulationRunning = false;
    m_gameStatusLabel->setText(QString("Stopped: %1").arg(QFileInfo(m_currentGamePath).baseName()));
}

void MainWindow::onEmulationError(const QString &error)
{
    m_emulationRunning = false;
    m_logWidget->addMessage(error, LogWidget::Error);
    QMessageBox::critical(this, "Emulation Error", error);
}

void MainWindow::onFpsUpdated(int fps)
{
    m_fpsLabel->setText(QString("FPS: %1").arg(fps));
}

void MainWindow::onStatusUpdated(const QString &status)
{
    m_logWidget->addMessage(status);
}

void MainWindow::loadSettings()
{
    restoreGeometry(m_settings->value("geometry").toByteArray());
    restoreState(m_settings->value("windowState").toByteArray());
    
    m_psnUsername = m_settings->value("psn/username").toString();
    updatePSNStatus();
}

void MainWindow::saveSettings()
{
    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("windowState", saveState());
    m_settings->setValue("psn/username", m_psnUsername);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}
