#include "SystemBattery.h"

#include <QDir>
#include <QFile>

namespace {
QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(file.readAll()).trimmed()
        : QString();
}
}

SystemBattery::SystemBattery(int updateInterval, bool enabled, QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &SystemBattery::refresh);
    setUpdateInterval(updateInterval);
    setEnabled(enabled);
}

void SystemBattery::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    if (m_enabled) {
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
        if (m_available || !m_info.isEmpty()) {
            m_available = false;
            m_info.clear();
            emit changed();
        }
    }
}

void SystemBattery::setUpdateInterval(int updateInterval)
{
    const int interval = qMax(1000, updateInterval);
    if (m_updateInterval == interval) {
        return;
    }

    m_updateInterval = interval;
    m_timer.setInterval(m_updateInterval);
    if (m_enabled) {
        m_timer.start();
    }
}

void SystemBattery::refresh()
{
    QString batteryPath;
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    for (const QString &entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = supplies.absoluteFilePath(entry);
        if (readText(path + QStringLiteral("/type")) == QStringLiteral("Battery")) {
            batteryPath = path;
            break;
        }
    }

    const bool available = !batteryPath.isEmpty();
    QString info;
    if (available) {
        const QString capacity = readText(batteryPath + QStringLiteral("/capacity"));
        const QString status = readText(batteryPath + QStringLiteral("/status"));
        const int percent = capacity.toInt();
        const QString level = percent < 10 ? QStringLiteral("caution")
            : percent < 30 ? QStringLiteral("low")
            : percent < 80 ? QStringLiteral("good") : QStringLiteral("full");
        const bool charging = status == QStringLiteral("Charging")
            || status == QStringLiteral("Full");
        m_iconName = charging
            ? QStringLiteral("battery-%1-charging").arg(level)
            : QStringLiteral("battery-%1").arg(level);
        info = tr("%1% (%2)").arg(capacity, status);
    }

    if (m_available != available || m_info != info) {
        m_available = available;
        m_info = info;
        emit changed();
    }
}
