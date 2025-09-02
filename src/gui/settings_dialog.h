#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QSettings>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QSlider;
class QLineEdit;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void applySettings();
    void resetToDefaults();
    void accept() override;

private:
    void setupUI();
    QWidget* createCPUTab();
    QWidget* createGPUTab();
    QWidget* createAudioTab();
    QWidget* createNetworkTab();
    QWidget* createSystemTab();
    void loadSettings();
    void saveSettings();

    QTabWidget *m_tabWidget;
    QSettings *m_settings;
    
    // CPU settings
    QComboBox *m_cpuInterpreter;
    QSpinBox *m_cpuThreads;
    QCheckBox *m_enableSPU;
    QComboBox *m_abiCompatibility;
    QCheckBox *m_enableJitCache;
    QCheckBox *m_debugMode;
    
    // GPU settings
    QComboBox *m_gpuBackend;
    QComboBox *m_vulkanDevice;
    QComboBox *m_resolution;
    QComboBox *m_aspectRatio;
    QCheckBox *m_vsync;
    QSpinBox *m_frameLimit;
    QCheckBox *m_vulkanDebug;
    QCheckBox *m_texturedQuadTest;
    QCheckBox *m_shaderCache;
    
    // Audio settings
    QComboBox *m_audioBackend;
    QComboBox *m_audioDevice;
    QSlider *m_masterVolume;
    QComboBox *m_sampleRate;
    QComboBox *m_bufferSize;
    QCheckBox *m_enable3DAudio;
    QComboBox *m_hrtfProfile;
    
    // Network settings
    QCheckBox *m_enablePSN;
    QComboBox *m_psnRegion;
    QCheckBox *m_autoLogin;
    QCheckBox *m_dnsOverride;
    QLineEdit *m_primaryDNS;
    QLineEdit *m_secondaryDNS;
    QCheckBox *m_proxyEnabled;
    QLineEdit *m_proxyAddress;
    
    // System settings
    QLineEdit *m_firmwarePath;
    QLineEdit *m_gamesPath;
    QLineEdit *m_trophiesPath;
    QLineEdit *m_userName;
    QSpinBox *m_userId;
    QComboBox *m_systemLanguage;
    QComboBox *m_timeZone;
    QCheckBox *m_enableTrophies;
};
