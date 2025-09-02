#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTime>

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    enum LogLevel {
        Debug = 0,
        Info,
        Warning,
        Error,
        Critical
    };

    explicit LogWidget(QWidget *parent = nullptr);

    void addMessage(const QString &message, LogLevel level = Info);
    void clear();
    void setLogLevel(LogLevel level);

private slots:
    void onLevelFilterChanged();
    void onClearClicked();
    void onSaveClicked();
    void onAutoScrollToggled(bool enabled);

private:
    void setupUI();
    QString formatMessage(const QString &message, LogLevel level);
    QColor getLevelColor(LogLevel level);

    QTextEdit *m_logDisplay;
    QComboBox *m_levelFilter;
    QPushButton *m_clearButton;
    QPushButton *m_saveButton;
    QCheckBox *m_autoScroll;
    
    LogLevel m_currentLevel;
    bool m_autoScrollEnabled;
};
