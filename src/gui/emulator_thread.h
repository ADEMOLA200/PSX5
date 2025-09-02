#pragma once

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <memory>

class Emulator;

class EmulatorThread : public QThread
{
    Q_OBJECT

public:
    enum State {
        Stopped,
        Running,
        Paused
    };

    explicit EmulatorThread(QObject *parent = nullptr);
    ~EmulatorThread();

    void setEmulator(std::shared_ptr<Emulator> emulator);
    void startEmulation();
    void pauseEmulation();
    void stopEmulation();
    void resetEmulation();
    
    State getState() const;
    bool isRunning() const;

signals:
    void emulationStarted();
    void emulationPaused();
    void emulationStopped();
    void emulationError(const QString &error);
    void fpsUpdated(int fps);
    void statusUpdated(const QString &status);

protected:
    void run() override;

private:
    void emulationLoop();
    void calculateFPS();

    std::shared_ptr<Emulator> m_emulator;
    mutable QMutex m_stateMutex;
    QWaitCondition m_pauseCondition;
    
    State m_state;
    bool m_shouldStop;
    
    // Performance tracking
    int m_frameCount;
    qint64 m_lastFpsUpdate;
    int m_currentFps;
};
