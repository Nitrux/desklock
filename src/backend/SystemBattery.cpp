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
    if (enabled) {
        connect(&m_timer, &QTimer::timeout, this, &SystemBattery::refresh);
        m_timer.start(qMax(1000, updateInterval));
        refresh();
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
        info = tr("🔋 %1% (%2)").arg(capacity, status);
    }

    if (m_available != available || m_info != info) {
        m_available = available;
        m_info = info;
        emit changed();
    }
}
