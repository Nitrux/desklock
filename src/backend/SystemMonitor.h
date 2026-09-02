// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#pragma once

#include <QObject>
#include <QTimer>

class SystemMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY changed)
    Q_PROPERTY(int memoryUsage READ memoryUsage NOTIFY memoryUsageChanged)
    Q_PROPERTY(QString interfaceName READ interfaceName NOTIFY changed)
    Q_PROPERTY(double receiveRate READ receiveRate NOTIFY changed)
    Q_PROPERTY(double transmitRate READ transmitRate NOTIFY changed)
    Q_PROPERTY(QString receiveRateText READ receiveRateText NOTIFY changed)
    Q_PROPERTY(QString transmitRateText READ transmitRateText NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(QString networkIconName READ networkIconName NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    explicit SystemMonitor(int updateInterval = 3000, bool enabled = true, QObject *parent = nullptr);

    double cpuUsage() const { return m_cpuUsage; }
    int memoryUsage() const { return m_memoryUsage; }
    QString interfaceName() const { return m_interfaceName; }
    double receiveRate() const { return m_receiveRate; }
    double transmitRate() const { return m_transmitRate; }
    QString receiveRateText() const;
    QString transmitRateText() const;
    bool online() const { return !m_interfaceName.isEmpty(); }
    QString networkIconName() const { return m_networkIconName; }
    QString summary() const;
    void setEnabled(bool enabled);
    void setUpdateInterval(int updateInterval);

signals:
    void changed();
    void memoryUsageChanged(int percent);

private:
    void refresh();
    void readCpu();
    void readMemory();
    void readNetwork();

    QTimer m_timer;
    int m_updateInterval = 0;
    bool m_enabled = false;
    quint64 m_previousCpuTotal = 0;
    quint64 m_previousCpuIdle = 0;
    quint64 m_previousReceive = 0;
    quint64 m_previousTransmit = 0;
    double m_cpuUsage = 0.0;
    int m_memoryUsage = 0;
    double m_receiveRate = 0.0;
    double m_transmitRate = 0.0;
    QString m_interfaceName;
    QString m_networkIconName = QStringLiteral("network-offline");
};
