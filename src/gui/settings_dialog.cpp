#include "settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_settings(new QSettings("PSX5", "Emulator", this))
{
    setWindowTitle("PSX5 Settings");
    setModal(true);
    resize(600, 500);
    
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Create tab widget
    m_tabWidget = new QTabWidget;
    
    // Add tabs
    m_tabWidget->addTab(createCPUTab(), "CPU");
    m_tabWidget->addTab(createGPUTab(), "GPU");
    m_tabWidget->addTab(createAudioTab(), "Audio");
    m_tabWidget->addTab(createNetworkTab(), "Network");
    m_tabWidget->addTab(createSystemTab(), "System");
    
    mainLayout->addWidget(m_tabWidget);
    
    // Button box
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    
    QPushButton *okButton = new QPushButton("OK");
    QPushButton *cancelButton = new QPushButton("Cancel");
    QPushButton *applyButton = new QPushButton("Apply");
    QPushButton *resetButton = new QPushButton("Reset to Defaults");
    
    connect(okButton, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::applySettings);
    connect(resetButton, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    
    buttonLayout->addWidget(resetButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(applyButton);
    
    mainLayout->addLayout(buttonLayout);
}

QWidget* SettingsDialog::createCPUTab()
{
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(widget);
    
    // CPU Core Settings
    QGroupBox *coreGroup = new QGroupBox("CPU Core");
    QFormLayout *coreLayout = new QFormLayout(coreGroup);
    
    m_cpuInterpreter = new QComboBox;
    m_cpuInterpreter->addItems({"Interpreter", "Dynarec (JIT)", "Cached Interpreter"});
    coreLayout->addRow("CPU Mode:", m_cpuInterpreter);
    
    m_cpuThreads = new QSpinBox;
    m_cpuThreads->setRange(1, 16);
    m_cpuThreads->setValue(8);
    coreLayout->addRow("CPU Threads:", m_cpuThreads);
    
    m_enableSPU = new QCheckBox("Enable SPU emulation");
    coreLayout->addRow(m_enableSPU);
    
    layout->addWidget(coreGroup);
    
    // Advanced Settings
    QGroupBox *advancedGroup = new QGroupBox("Advanced");
    QFormLayout *advancedLayout = new QFormLayout(advancedGroup);
    
    m_abiCompatibility = new QComboBox;
    m_abiCompatibility->addItems({"Strict", "Relaxed", "Legacy"});
    advancedLayout->addRow("ABI Compatibility:", m_abiCompatibility);
    
    m_enableJitCache = new QCheckBox("Enable JIT cache");
    advancedLayout->addRow(m_enableJitCache);
    
    m_debugMode = new QCheckBox("Enable debug mode");
    advancedLayout->addRow(m_debugMode);
    
    layout->addWidget(advancedGroup);
    layout->addStretch();
    
    return widget;
}

QWidget* SettingsDialog::createGPUTab()
{
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(widget);
    
    // Graphics Backend
    QGroupBox *backendGroup = new QGroupBox("Graphics Backend");
    QFormLayout *backendLayout = new QFormLayout(backendGroup);
    
    m_gpuBackend = new QComboBox;
    m_gpuBackend->addItems({"Vulkan", "OpenGL", "Software"});
    backendLayout->addRow("Renderer:", m_gpuBackend);
    
    m_vulkanDevice = new QComboBox;
    m_vulkanDevice->addItems({"Auto-detect", "Device 0", "Device 1"});
    backendLayout->addRow("Vulkan Device:", m_vulkanDevice);
    
    layout->addWidget(backendGroup);
    
    // Display Settings
    QGroupBox *displayGroup = new QGroupBox("Display");
    QFormLayout *displayLayout = new QFormLayout(displayGroup);
    
    m_resolution = new QComboBox;
    m_resolution->addItems({"1280x720", "1920x1080", "2560x1440", "3840x2160"});
    displayLayout->addRow("Resolution:", m_resolution);
    
    m_aspectRatio = new QComboBox;
    m_aspectRatio->addItems({"16:9", "4:3", "Stretch"});
    displayLayout->addRow("Aspect Ratio:", m_aspectRatio);
    
    m_vsync = new QCheckBox("Enable V-Sync");
    displayLayout->addRow(m_vsync);
    
    m_frameLimit = new QSpinBox;
    m_frameLimit->setRange(30, 240);
    m_frameLimit->setValue(60);
    displayLayout->addRow("Frame Limit:", m_frameLimit);
    
    layout->addWidget(displayGroup);
    
    // Advanced Graphics
    QGroupBox *advancedGfxGroup = new QGroupBox("Advanced");
    QFormLayout *advancedGfxLayout = new QFormLayout(advancedGfxGroup);
    
    m_vulkanDebug = new QCheckBox("Enable Vulkan debug layers");
    advancedGfxLayout->addRow(m_vulkanDebug);
    
    m_texturedQuadTest = new QCheckBox("Textured quad test mode");
    advancedGfxLayout->addRow(m_texturedQuadTest);
    
    m_shaderCache = new QCheckBox("Enable shader cache");
    advancedGfxLayout->addRow(m_shaderCache);
    
    layout->addWidget(advancedGfxGroup);
    layout->addStretch();
    
    return widget;
}

QWidget* SettingsDialog::createAudioTab()
{
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(widget);
    
    // Audio Backend
    QGroupBox *backendGroup = new QGroupBox("Audio Backend");
    QFormLayout *backendLayout = new QFormLayout(backendGroup);
    
    m_audioBackend = new QComboBox;
#ifdef _WIN32
    m_audioBackend->addItems({"DirectSound", "WASAPI", "SDL2"});
#elif __APPLE__
    m_audioBackend->addItems({"CoreAudio", "SDL2"});
#else
    m_audioBackend->addItems({"ALSA", "PulseAudio", "SDL2"});
#endif
    backendLayout->addRow("Audio Backend:", m_audioBackend);
    
    m_audioDevice = new QComboBox;
    m_audioDevice->addItems({"Default", "Device 1", "Device 2"});
    backendLayout->addRow("Audio Device:", m_audioDevice);
    
    layout->addWidget(backendGroup);
    
    // Audio Settings
    QGroupBox *settingsGroup = new QGroupBox("Audio Settings");
    QFormLayout *settingsLayout = new QFormLayout(settingsGroup);
    
    m_masterVolume = new QSlider(Qt::Horizontal);
    m_masterVolume->setRange(0, 100);
    m_masterVolume->setValue(100);
    QLabel *volumeLabel = new QLabel("100%");
    connect(m_masterVolume, &QSlider::valueChanged, [volumeLabel](int value) {
        volumeLabel->setText(QString("%1%").arg(value));
    });
    
    QHBoxLayout *volumeLayout = new QHBoxLayout;
    volumeLayout->addWidget(m_masterVolume);
    volumeLayout->addWidget(volumeLabel);
    settingsLayout->addRow("Master Volume:", volumeLayout);
    
    m_sampleRate = new QComboBox;
    m_sampleRate->addItems({"44100 Hz", "48000 Hz", "96000 Hz"});
    settingsLayout->addRow("Sample Rate:", m_sampleRate);
    
    m_bufferSize = new QComboBox;
    m_bufferSize->addItems({"512", "1024", "2048", "4096"});
    settingsLayout->addRow("Buffer Size:", m_bufferSize);
    
    layout->addWidget(settingsGroup);
    
    // 3D Audio
    QGroupBox *audio3dGroup = new QGroupBox("3D Audio (Tempest)");
    QFormLayout *audio3dLayout = new QFormLayout(audio3dGroup);
    
    m_enable3DAudio = new QCheckBox("Enable Tempest 3D AudioTech");
    audio3dLayout->addRow(m_enable3DAudio);
    
    m_hrtfProfile = new QComboBox;
    m_hrtfProfile->addItems({"Default", "Small Head", "Large Head", "Custom"});
    audio3dLayout->addRow("HRTF Profile:", m_hrtfProfile);
    
    layout->addWidget(audio3dGroup);
    layout->addStretch();
    
    return widget;
}

QWidget* SettingsDialog::createNetworkTab()
{
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(widget);
    
    // PSN Settings
    QGroupBox *psnGroup = new QGroupBox("PlayStation Network");
    QFormLayout *psnLayout = new QFormLayout(psnGroup);
    
    m_enablePSN = new QCheckBox("Enable PSN connectivity");
    psnLayout->addRow(m_enablePSN);
    
    m_psnRegion = new QComboBox;
    m_psnRegion->addItems({"US", "EU", "JP", "Asia"});
    psnLayout->addRow("PSN Region:", m_psnRegion);
    
    m_autoLogin = new QCheckBox("Auto-login on startup");
    psnLayout->addRow(m_autoLogin);
    
    layout->addWidget(psnGroup);
    
    // Network Settings
    QGroupBox *networkGroup = new QGroupBox("Network Configuration");
    QFormLayout *networkLayout = new QFormLayout(networkGroup);
    
    m_dnsOverride = new QCheckBox("Override DNS servers");
    networkLayout->addRow(m_dnsOverride);
    
    m_primaryDNS = new QLineEdit;
    m_primaryDNS->setPlaceholderText("8.8.8.8");
    networkLayout->addRow("Primary DNS:", m_primaryDNS);
    
    m_secondaryDNS = new QLineEdit;
    m_secondaryDNS->setPlaceholderText("8.8.4.4");
    networkLayout->addRow("Secondary DNS:", m_secondaryDNS);
    
    m_proxyEnabled = new QCheckBox("Use proxy server");
    networkLayout->addRow(m_proxyEnabled);
    
    m_proxyAddress = new QLineEdit;
    m_proxyAddress->setPlaceholderText("proxy.example.com:8080");
    networkLayout->addRow("Proxy Address:", m_proxyAddress);
    
    layout->addWidget(networkGroup);
    layout->addStretch();
    
    return widget;
}

QWidget* SettingsDialog::createSystemTab()
{
    QWidget *widget = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(widget);
    
    // System Paths
    QGroupBox *pathsGroup = new QGroupBox("System Paths");
    QFormLayout *pathsLayout = new QFormLayout(pathsGroup);
    
    QHBoxLayout *firmwareLayout = new QHBoxLayout;
    m_firmwarePath = new QLineEdit;
    QPushButton *browseFirmware = new QPushButton("Browse...");
    connect(browseFirmware, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getExistingDirectory(this, "Select Firmware Directory");
        if (!path.isEmpty()) {
            m_firmwarePath->setText(path);
        }
    });
    firmwareLayout->addWidget(m_firmwarePath);
    firmwareLayout->addWidget(browseFirmware);
    pathsLayout->addRow("Firmware Path:", firmwareLayout);
    
    QHBoxLayout *gamesLayout = new QHBoxLayout;
    m_gamesPath = new QLineEdit;
    QPushButton *browseGames = new QPushButton("Browse...");
    connect(browseGames, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getExistingDirectory(this, "Select Games Directory");
        if (!path.isEmpty()) {
            m_gamesPath->setText(path);
        }
    });
    gamesLayout->addWidget(m_gamesPath);
    gamesLayout->addWidget(browseGames);
    pathsLayout->addRow("Games Path:", gamesLayout);
    
    QHBoxLayout *trophiesLayout = new QHBoxLayout;
    m_trophiesPath = new QLineEdit;
    QPushButton *browseTrophies = new QPushButton("Browse...");
    connect(browseTrophies, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getExistingDirectory(this, "Select Trophies Directory");
        if (!path.isEmpty()) {
            m_trophiesPath->setText(path);
        }
    });
    trophiesLayout->addWidget(m_trophiesPath);
    trophiesLayout->addWidget(browseTrophies);
    pathsLayout->addRow("Trophies Path:", trophiesLayout);
    
    layout->addWidget(pathsGroup);
    
    // User Account
    QGroupBox *accountGroup = new QGroupBox("User Account");
    QFormLayout *accountLayout = new QFormLayout(accountGroup);
    
    m_userName = new QLineEdit;
    accountLayout->addRow("User Name:", m_userName);
    
    m_userId = new QSpinBox;
    m_userId->setRange(1, 16);
    accountLayout->addRow("User ID:", m_userId);
    
    layout->addWidget(accountGroup);
    
    // System Settings
    QGroupBox *systemGroup = new QGroupBox("System");
    QFormLayout *systemLayout = new QFormLayout(systemGroup);
    
    m_systemLanguage = new QComboBox;
    m_systemLanguage->addItems({"English", "Japanese", "French", "German", "Spanish", "Italian"});
    systemLayout->addRow("System Language:", m_systemLanguage);
    
    m_timeZone = new QComboBox;
    m_timeZone->addItems({"UTC", "PST", "EST", "JST", "CET"});
    systemLayout->addRow("Time Zone:", m_timeZone);
    
    m_enableTrophies = new QCheckBox("Enable trophy system");
    systemLayout->addRow(m_enableTrophies);
    
    layout->addWidget(systemGroup);
    layout->addStretch();
    
    return widget;
}

void SettingsDialog::loadSettings()
{
    // CPU settings
    m_cpuInterpreter->setCurrentText(m_settings->value("cpu/interpreter", "Dynarec (JIT)").toString());
    m_cpuThreads->setValue(m_settings->value("cpu/threads", 8).toInt());
    m_enableSPU->setChecked(m_settings->value("cpu/enableSPU", true).toBool());
    m_abiCompatibility->setCurrentText(m_settings->value("cpu/abiCompatibility", "Strict").toString());
    m_enableJitCache->setChecked(m_settings->value("cpu/enableJitCache", true).toBool());
    m_debugMode->setChecked(m_settings->value("cpu/debugMode", false).toBool());
    
    // GPU settings
    m_gpuBackend->setCurrentText(m_settings->value("gpu/backend", "Vulkan").toString());
    m_vulkanDevice->setCurrentText(m_settings->value("gpu/vulkanDevice", "Auto-detect").toString());
    m_resolution->setCurrentText(m_settings->value("gpu/resolution", "1920x1080").toString());
    m_aspectRatio->setCurrentText(m_settings->value("gpu/aspectRatio", "16:9").toString());
    m_vsync->setChecked(m_settings->value("gpu/vsync", true).toBool());
    m_frameLimit->setValue(m_settings->value("gpu/frameLimit", 60).toInt());
    m_vulkanDebug->setChecked(m_settings->value("gpu/vulkanDebug", false).toBool());
    m_texturedQuadTest->setChecked(m_settings->value("gpu/texturedQuadTest", false).toBool());
    m_shaderCache->setChecked(m_settings->value("gpu/shaderCache", true).toBool());
    
    // Audio settings
#ifdef _WIN32
    m_audioBackend->setCurrentText(m_settings->value("audio/backend", "DirectSound").toString());
#elif __APPLE__
    m_audioBackend->setCurrentText(m_settings->value("audio/backend", "CoreAudio").toString());
#else
    m_audioBackend->setCurrentText(m_settings->value("audio/backend", "ALSA").toString());
#endif
    m_audioDevice->setCurrentText(m_settings->value("audio/device", "Default").toString());
    m_masterVolume->setValue(m_settings->value("audio/masterVolume", 100).toInt());
    m_sampleRate->setCurrentText(m_settings->value("audio/sampleRate", "48000 Hz").toString());
    m_bufferSize->setCurrentText(m_settings->value("audio/bufferSize", "1024").toString());
    m_enable3DAudio->setChecked(m_settings->value("audio/enable3D", true).toBool());
    m_hrtfProfile->setCurrentText(m_settings->value("audio/hrtfProfile", "Default").toString());
    
    // Network settings
    m_enablePSN->setChecked(m_settings->value("network/enablePSN", true).toBool());
    m_psnRegion->setCurrentText(m_settings->value("network/psnRegion", "US").toString());
    m_autoLogin->setChecked(m_settings->value("network/autoLogin", false).toBool());
    m_dnsOverride->setChecked(m_settings->value("network/dnsOverride", false).toBool());
    m_primaryDNS->setText(m_settings->value("network/primaryDNS", "").toString());
    m_secondaryDNS->setText(m_settings->value("network/secondaryDNS", "").toString());
    m_proxyEnabled->setChecked(m_settings->value("network/proxyEnabled", false).toBool());
    m_proxyAddress->setText(m_settings->value("network/proxyAddress", "").toString());
    
    // System settings
    QString defaultFirmware = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/PSX5/Firmware";
    QString defaultGames = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/PSX5/Games";
    QString defaultTrophies = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/PSX5/Trophies";
    
    m_firmwarePath->setText(m_settings->value("system/firmwarePath", defaultFirmware).toString());
    m_gamesPath->setText(m_settings->value("system/gamesPath", defaultGames).toString());
    m_trophiesPath->setText(m_settings->value("system/trophiesPath", defaultTrophies).toString());
    m_userName->setText(m_settings->value("system/userName", "PSX5User").toString());
    m_userId->setValue(m_settings->value("system/userId", 1).toInt());
    m_systemLanguage->setCurrentText(m_settings->value("system/language", "English").toString());
    m_timeZone->setCurrentText(m_settings->value("system/timeZone", "UTC").toString());
    m_enableTrophies->setChecked(m_settings->value("system/enableTrophies", true).toBool());
}

void SettingsDialog::saveSettings()
{
    // CPU settings
    m_settings->setValue("cpu/interpreter", m_cpuInterpreter->currentText());
    m_settings->setValue("cpu/threads", m_cpuThreads->value());
    m_settings->setValue("cpu/enableSPU", m_enableSPU->isChecked());
    m_settings->setValue("cpu/abiCompatibility", m_abiCompatibility->currentText());
    m_settings->setValue("cpu/enableJitCache", m_enableJitCache->isChecked());
    m_settings->setValue("cpu/debugMode", m_debugMode->isChecked());
    
    // GPU settings
    m_settings->setValue("gpu/backend", m_gpuBackend->currentText());
    m_settings->setValue("gpu/vulkanDevice", m_vulkanDevice->currentText());
    m_settings->setValue("gpu/resolution", m_resolution->currentText());
    m_settings->setValue("gpu/aspectRatio", m_aspectRatio->currentText());
    m_settings->setValue("gpu/vsync", m_vsync->isChecked());
    m_settings->setValue("gpu/frameLimit", m_frameLimit->value());
    m_settings->setValue("gpu/vulkanDebug", m_vulkanDebug->isChecked());
    m_settings->setValue("gpu/texturedQuadTest", m_texturedQuadTest->isChecked());
    m_settings->setValue("gpu/shaderCache", m_shaderCache->isChecked());
    
    // Audio settings
    m_settings->setValue("audio/backend", m_audioBackend->currentText());
    m_settings->setValue("audio/device", m_audioDevice->currentText());
    m_settings->setValue("audio/masterVolume", m_masterVolume->value());
    m_settings->setValue("audio/sampleRate", m_sampleRate->currentText());
    m_settings->setValue("audio/bufferSize", m_bufferSize->currentText());
    m_settings->setValue("audio/enable3D", m_enable3DAudio->isChecked());
    m_settings->setValue("audio/hrtfProfile", m_hrtfProfile->currentText());
    
    // Network settings
    m_settings->setValue("network/enablePSN", m_enablePSN->isChecked());
    m_settings->setValue("network/psnRegion", m_psnRegion->currentText());
    m_settings->setValue("network/autoLogin", m_autoLogin->isChecked());
    m_settings->setValue("network/dnsOverride", m_dnsOverride->isChecked());
    m_settings->setValue("network/primaryDNS", m_primaryDNS->text());
    m_settings->setValue("network/secondaryDNS", m_secondaryDNS->text());
    m_settings->setValue("network/proxyEnabled", m_proxyEnabled->isChecked());
    m_settings->setValue("network/proxyAddress", m_proxyAddress->text());
    
    // System settings
    m_settings->setValue("system/firmwarePath", m_firmwarePath->text());
    m_settings->setValue("system/gamesPath", m_gamesPath->text());
    m_settings->setValue("system/trophiesPath", m_trophiesPath->text());
    m_settings->setValue("system/userName", m_userName->text());
    m_settings->setValue("system/userId", m_userId->value());
    m_settings->setValue("system/language", m_systemLanguage->currentText());
    m_settings->setValue("system/timeZone", m_timeZone->currentText());
    m_settings->setValue("system/enableTrophies", m_enableTrophies->isChecked());
    
    m_settings->sync();
}

void SettingsDialog::applySettings()
{
    saveSettings();
    
    applyEmulatorSettings();
    
    QMessageBox::information(this, "Settings Applied", "Settings have been applied successfully.");
}

void SettingsDialog::applyEmulatorSettings()
{
    // Apply CPU settings
    QString cpuMode = m_settings->value("cpu/interpreter", "Dynarec (JIT)").toString();
    int cpuThreads = m_settings->value("cpu/threads", 8).toInt();
    bool enableSPU = m_settings->value("cpu/enableSPU", true).toBool();
    bool enableJitCache = m_settings->value("cpu/enableJitCache", true).toBool();
    bool debugMode = m_settings->value("cpu/debugMode", false).toBool();
    
    // Apply GPU settings
    QString gpuBackend = m_settings->value("gpu/backend", "Vulkan").toString();
    QString resolution = m_settings->value("gpu/resolution", "1920x1080").toString();
    bool vsync = m_settings->value("gpu/vsync", true).toBool();
    int frameLimit = m_settings->value("gpu/frameLimit", 60).toInt();
    bool vulkanDebug = m_settings->value("gpu/vulkanDebug", false).toBool();
    bool shaderCache = m_settings->value("gpu/shaderCache", true).toBool();
    
    // Apply Audio settings
    QString audioBackend = m_settings->value("audio/backend").toString();
    int masterVolume = m_settings->value("audio/masterVolume", 100).toInt();
    QString sampleRate = m_settings->value("audio/sampleRate", "48000 Hz").toString();
    QString bufferSize = m_settings->value("audio/bufferSize", "1024").toString();
    bool enable3DAudio = m_settings->value("audio/enable3D", true).toBool();
    
    // Apply Network settings
    bool enablePSN = m_settings->value("network/enablePSN", true).toBool();
    QString psnRegion = m_settings->value("network/psnRegion", "US").toString();
    bool dnsOverride = m_settings->value("network/dnsOverride", false).toBool();
    
    // Apply System settings
    QString systemLanguage = m_settings->value("system/language", "English").toString();
    QString timeZone = m_settings->value("system/timeZone", "UTC").toString();
    bool enableTrophies = m_settings->value("system/enableTrophies", true).toBool();
    
    // Emit signals to notify other components
    emit cpuSettingsChanged(cpuMode, cpuThreads, enableSPU, enableJitCache, debugMode);
    emit gpuSettingsChanged(gpuBackend, resolution, vsync, frameLimit, vulkanDebug, shaderCache);
    emit audioSettingsChanged(audioBackend, masterVolume, sampleRate.left(5).toInt(), 
                             bufferSize.toInt(), enable3DAudio);
    emit networkSettingsChanged(enablePSN, psnRegion, dnsOverride);
    emit systemSettingsChanged(systemLanguage, timeZone, enableTrophies);
}

void SettingsDialog::resetToDefaults()
{
    int ret = QMessageBox::question(this, "Reset Settings", 
        "Are you sure you want to reset all settings to their default values?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        m_settings->clear();
        loadSettings();
    }
}

void SettingsDialog::accept()
{
    saveSettings();
    QDialog::accept();
}
