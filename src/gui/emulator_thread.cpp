#include "emulator_thread.h"
#include "runtime/emulator.h"
#include <QElapsedTimer>
#include <QDebug>

EmulatorThread::EmulatorThread(QObject *parent)
    : QThread(parent)
    , m_emulator(nullptr)
    , m_state(Stopped)
    , m_shouldStop(false)
    , m_frameCount(0)
    , m_lastFpsUpdate(0)
    , m_currentFps(0)
{
}

EmulatorThread::~EmulatorThread()
{
    stopEmulation();
    wait(); // Wait for thread to finish
}

void EmulatorThread::setEmulator(std::shared_ptr<Emulator> emulator)
{
    QMutexLocker locker(&m_stateMutex);
    m_emulator = emulator;
}

void EmulatorThread::startEmulation()
{
    QMutexLocker locker(&m_stateMutex);
    
    if (!m_emulator) {
        emit emulationError("No emulator instance available");
        return;
    }
    
    if (m_state == Stopped) {
        m_shouldStop = false;
        start(); // Start the thread
    } else if (m_state == Paused) {
        m_state = Running;
        m_pauseCondition.wakeAll();
        emit emulationStarted();
    }
}

void EmulatorThread::pauseEmulation()
{
    QMutexLocker locker(&m_stateMutex);
    
    if (m_state == Running) {
        m_state = Paused;
        emit emulationPaused();
    }
}

void EmulatorThread::stopEmulation()
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_shouldStop = true;
        m_state = Stopped;
        m_pauseCondition.wakeAll();
    }
    
    if (isRunning()) {
        wait(5000); // Wait up to 5 seconds for thread to finish
        if (isRunning()) {
            terminate(); // Force terminate if it doesn't stop gracefully
            wait(1000);
        }
    }
    
    emit emulationStopped();
}

void EmulatorThread::resetEmulation()
{
    stopEmulation();
    
    // Reset emulator state
    if (m_emulator) {
        // TODO: Add reset method to Emulator class
        emit statusUpdated("Emulator reset");
    }
}

EmulatorThread::State EmulatorThread::getState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_state;
}

bool EmulatorThread::isRunning() const
{
    return QThread::isRunning();
}

void EmulatorThread::run()
{
    {
        QMutexLocker locker(&m_stateMutex);
        m_state = Running;
        m_frameCount = 0;
        m_lastFpsUpdate = QDateTime::currentMSecsSinceEpoch();
    }
    
    emit emulationStarted();
    emit statusUpdated("Emulation started");
    
    try {
        emulationLoop();
    } catch (const std::exception &e) {
        emit emulationError(QString("Emulation error: %1").arg(e.what()));
    } catch (...) {
        emit emulationError("Unknown emulation error occurred");
    }
    
    {
        QMutexLocker locker(&m_stateMutex);
        m_state = Stopped;
    }
    
    emit emulationStopped();
    emit statusUpdated("Emulation stopped");
}

void EmulatorThread::emulationLoop()
{
    QElapsedTimer frameTimer;
    frameTimer.start();
    
    const int targetFPS = 60;
    const qint64 frameTimeMs = 1000 / targetFPS;
    
    while (true) {
        {
            QMutexLocker locker(&m_stateMutex);
            
            if (m_shouldStop) {
                break;
            }
            
            if (m_state == Paused) {
                m_pauseCondition.wait(&m_stateMutex);
                continue;
            }
        }
        
        qint64 frameStart = frameTimer.elapsed();
        
        // Run emulator for one frame
        if (m_emulator) {
            try {
                // Run emulator steps (approximate one frame worth)
                m_emulator->run_until_halt(100000);
                
                m_frameCount++;
                calculateFPS();
                
            } catch (const std::exception &e) {
                emit emulationError(QString("Emulator runtime error: %1").arg(e.what()));
                break;
            }
        }
        
        // Frame rate limiting
        qint64 frameEnd = frameTimer.elapsed();
        qint64 frameTime = frameEnd - frameStart;
        
        if (frameTime < frameTimeMs) {
            msleep(frameTimeMs - frameTime);
        }
    }
}

void EmulatorThread::calculateFPS()
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 timeDiff = currentTime - m_lastFpsUpdate;
    
    if (timeDiff >= 1000) { // Update FPS every second
        m_currentFps = (m_frameCount * 1000) / timeDiff;
        emit fpsUpdated(m_currentFps);
        
        m_frameCount = 0;
        m_lastFpsUpdate = currentTime;
    }
}

#include "emulator_thread.moc"
