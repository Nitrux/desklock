#pragma once

#include <QObject>
#include <QTimer>

class SystemBattery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString info READ info NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit SystemBattery(int updateInterval = 30000, bool enabled = true, QObject *parent = nullptr);

    QString info() const { return m_info; }
    bool available() const { return m_available; }
    void setEnabled(bool enabled);
    void setUpdateInterval(int updateInterval);

signals:
    void changed();

private:
    void refresh();

    QTimer m_timer;
    QString m_info;
    bool m_available = false;
    bool m_enabled = false;
    int m_updateInterval = 0;
};
