#include "log_widget.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QScrollBar>

LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
    , m_logDisplay(nullptr)
    , m_levelFilter(nullptr)
    , m_clearButton(nullptr)
    , m_saveButton(nullptr)
    , m_autoScroll(nullptr)
    , m_currentLevel(Info)
    , m_autoScrollEnabled(true)
{
    setupUI();
}

void LogWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Control bar
    QHBoxLayout *controlLayout = new QHBoxLayout;
    
    m_levelFilter = new QComboBox;
    m_levelFilter->addItems({"Debug", "Info", "Warning", "Error", "Critical"});
    m_levelFilter->setCurrentIndex(static_cast<int>(m_currentLevel));
    connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogWidget::onLevelFilterChanged);
    
    m_clearButton = new QPushButton("Clear");
    connect(m_clearButton, &QPushButton::clicked, this, &LogWidget::onClearClicked);
    
    m_saveButton = new QPushButton("Save Log...");
    connect(m_saveButton, &QPushButton::clicked, this, &LogWidget::onSaveClicked);
    
    m_autoScroll = new QCheckBox("Auto-scroll");
    m_autoScroll->setChecked(m_autoScrollEnabled);
    connect(m_autoScroll, &QCheckBox::toggled, this, &LogWidget::onAutoScrollToggled);
    
    controlLayout->addWidget(new QLabel("Level:"));
    controlLayout->addWidget(m_levelFilter);
    controlLayout->addStretch();
    controlLayout->addWidget(m_autoScroll);
    controlLayout->addWidget(m_clearButton);
    controlLayout->addWidget(m_saveButton);
    
    // Log display
    m_logDisplay = new QTextEdit;
    m_logDisplay->setReadOnly(true);
    m_logDisplay->setFont(QFont("Consolas", 9));
    
    mainLayout->addLayout(controlLayout);
    mainLayout->addWidget(m_logDisplay);
}

void LogWidget::addMessage(const QString &message, LogLevel level)
{
    if (level < m_currentLevel) {
        return; // Filter out messages below current level
    }
    
    QString formattedMessage = formatMessage(message, level);
    
    // Set color based on level
    QColor color = getLevelColor(level);
    m_logDisplay->setTextColor(color);
    m_logDisplay->append(formattedMessage);
    
    // Auto-scroll to bottom if enabled
    if (m_autoScrollEnabled) {
        QScrollBar *scrollBar = m_logDisplay->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());
    }
}

void LogWidget::clear()
{
    m_logDisplay->clear();
}

void LogWidget::setLogLevel(LogLevel level)
{
    m_currentLevel = level;
    m_levelFilter->setCurrentIndex(static_cast<int>(level));
}

void LogWidget::onLevelFilterChanged()
{
    m_currentLevel = static_cast<LogLevel>(m_levelFilter->currentIndex());
}

void LogWidget::onClearClicked()
{
    clear();
}

void LogWidget::onSaveClicked()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Log File",
        QString("psx5_log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "Text Files (*.txt);;All Files (*)"
    );
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << m_logDisplay->toPlainText();
            QMessageBox::information(this, "Log Saved", "Log file saved successfully.");
        } else {
            QMessageBox::warning(this, "Save Error", "Failed to save log file.");
        }
    }
}

void LogWidget::onAutoScrollToggled(bool enabled)
{
    m_autoScrollEnabled = enabled;
}

QString LogWidget::formatMessage(const QString &message, LogLevel level)
{
    QString levelStr;
    switch (level) {
    case Debug:    levelStr = "DEBUG"; break;
    case Info:     levelStr = "INFO"; break;
    case Warning:  levelStr = "WARN"; break;
    case Error:    levelStr = "ERROR"; break;
    case Critical: levelStr = "CRIT"; break;
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    return QString("[%1] [%2] %3").arg(timestamp, levelStr, message);
}

QColor LogWidget::getLevelColor(LogLevel level)
{
    switch (level) {
    case Debug:    return QColor(128, 128, 128); // Gray
    case Info:     return QColor(0, 0, 0);       // Black
    case Warning:  return QColor(255, 140, 0);   // Orange
    case Error:    return QColor(255, 0, 0);     // Red
    case Critical: return QColor(139, 0, 0);     // Dark Red
    default:       return QColor(0, 0, 0);
    }
}
