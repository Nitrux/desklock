#pragma once

#include <QObject>
#include <QTimer>

class SystemMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY changed)
    Q_PROPERTY(double memoryUsage READ memoryUsage NOTIFY changed)
    Q_PROPERTY(QString interfaceName READ interfaceName NOTIFY changed)
    Q_PROPERTY(double receiveRate READ receiveRate NOTIFY changed)
    Q_PROPERTY(double transmitRate READ transmitRate NOTIFY changed)
    Q_PROPERTY(QString receiveRateText READ receiveRateText NOTIFY changed)
    Q_PROPERTY(QString transmitRateText READ transmitRateText NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit SystemMonitor(int updateInterval = 3000, bool enabled = true, QObject *parent = nullptr);

    double cpuUsage() const { return m_cpuUsage; }
    double memoryUsage() const { return m_memoryUsage; }
    QString interfaceName() const { return m_interfaceName; }
    double receiveRate() const { return m_receiveRate; }
    double transmitRate() const { return m_transmitRate; }
    QString receiveRateText() const;
    QString transmitRateText() const;
    bool online() const { return !m_interfaceName.isEmpty(); }
    QString summary() const;

signals:
    void changed();

private:
    void refresh();
    void readCpu();
    void readMemory();
    void readNetwork();

    QTimer m_timer;
    int m_updateInterval = 3000;
    quint64 m_previousCpuTotal = 0;
    quint64 m_previousCpuIdle = 0;
    quint64 m_previousReceive = 0;
    quint64 m_previousTransmit = 0;
    double m_cpuUsage = 0.0;
    double m_memoryUsage = 0.0;
    double m_receiveRate = 0.0;
    double m_transmitRate = 0.0;
    QString m_interfaceName;
};
