// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#pragma once

#include <QObject>
#include <QTimer>

class SystemBattery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString info READ info NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit SystemBattery(int updateInterval = 30000, bool enabled = true, QObject *parent = nullptr);

    QString info() const { return m_info; }
    QString iconName() const { return m_iconName; }
    bool available() const { return m_available; }
    void setEnabled(bool enabled);
    void setUpdateInterval(int updateInterval);
    void setDebugBattery(bool debugBattery);

signals:
    void changed();

private:
    void refresh();

    QTimer m_timer;
    QString m_info;
    QString m_iconName = QStringLiteral("battery-full");
    bool m_debugBattery = false;
    int m_debugState = 0;
    bool m_available = false;
    bool m_enabled = false;
    int m_updateInterval = 0;
};
