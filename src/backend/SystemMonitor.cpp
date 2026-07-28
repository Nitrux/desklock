#include "SystemMonitor.h"

#include <QFile>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>

SystemMonitor::SystemMonitor(int updateInterval, bool enabled, QObject *parent)
    : QObject(parent)
    , m_updateInterval(qMax(1000, updateInterval))
{
    if (enabled) {
        connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::refresh);
        refresh();
        m_timer.start(m_updateInterval);
    }
}

QString SystemMonitor::receiveRateText() const
{
    return QLocale().formattedDataSize(
        static_cast<qint64>(m_receiveRate), 1, QLocale::DataSizeSIFormat);
}

QString SystemMonitor::transmitRateText() const
{
    return QLocale().formattedDataSize(
        static_cast<qint64>(m_transmitRate), 1, QLocale::DataSizeSIFormat);
}

QString SystemMonitor::summary() const
{
    const QLocale locale;
    const QString cpu = locale.toString(m_cpuUsage, 'f', 0);
    const QString ram = locale.toString(m_memoryUsage, 'f', 0);
    if (!online()) {
        return tr("💻 CPU %1%  |  🧠 RAM %2%  |  🌐 Offline").arg(cpu, ram);
    }

    return tr("💻 CPU %1%  |  🧠 RAM %2%  |  🌐 %3: ↓ %4  |  ↑ %5")
        .arg(cpu,
             ram,
             m_interfaceName,
             receiveRateText() + QStringLiteral("/s"),
             transmitRateText() + QStringLiteral("/s"));
}

void SystemMonitor::refresh()
{
    readCpu();
    readMemory();
    readNetwork();
    emit changed();
}

void SystemMonitor::readCpu()
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QList<QByteArray> fields =
        file.readLine().simplified().split(' ');
    if (fields.size() < 5 || fields[0] != "cpu") {
        return;
    }

    quint64 total = 0;
    for (qsizetype i = 1; i < fields.size(); ++i) {
        total += fields[i].toULongLong();
    }
    const quint64 idle = fields[4].toULongLong()
        + (fields.size() > 5 ? fields[5].toULongLong() : 0);

    if (m_previousCpuTotal > 0 && total > m_previousCpuTotal) {
        const quint64 totalDelta = total - m_previousCpuTotal;
        const quint64 idleDelta = idle - m_previousCpuIdle;
        m_cpuUsage = 100.0
            * static_cast<double>(totalDelta - qMin(totalDelta, idleDelta))
            / static_cast<double>(totalDelta);
    }
    m_previousCpuTotal = total;
    m_previousCpuIdle = idle;
}

void SystemMonitor::readMemory()
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    quint64 total = 0;
    quint64 available = 0;
    while (!file.atEnd()) {
        const QList<QByteArray> fields = file.readLine().simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        if (fields[0] == "MemTotal:") {
            total = fields[1].toULongLong();
        } else if (fields[0] == "MemAvailable:") {
            available = fields[1].toULongLong();
        }
    }
    if (total > 0) {
        m_memoryUsage = 100.0 * static_cast<double>(total - qMin(total, available))
            / static_cast<double>(total);
    }
}

void SystemMonitor::readNetwork()
{
    QFile file(QStringLiteral("/proc/net/dev"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QString selectedInterface;
    quint64 selectedReceive = 0;
    quint64 selectedTransmit = 0;
    quint64 largestTraffic = 0;

    QTextStream input(&file);
    input.readLine();
    input.readLine();
    while (!input.atEnd()) {
        const QString line = input.readLine().trimmed();
        const qsizetype separator = line.indexOf(':');
        if (separator < 0) {
            continue;
        }
        const QString interface = line.left(separator).trimmed();
        if (interface == QStringLiteral("lo")) {
            continue;
        }
        QFile stateFile(QStringLiteral("/sys/class/net/") + interface
                        + QStringLiteral("/operstate"));
        if (stateFile.open(QIODevice::ReadOnly)) {
            const QByteArray state = stateFile.readAll().trimmed();
            if (state != "up" && state != "unknown") {
                continue;
            }
        }
        const QStringList fields =
            line.mid(separator + 1).simplified().split(' ');
        if (fields.size() < 16) {
            continue;
        }
        const quint64 receive = fields[0].toULongLong();
        const quint64 transmit = fields[8].toULongLong();
        if (receive + transmit >= largestTraffic) {
            largestTraffic = receive + transmit;
            selectedInterface = interface;
            selectedReceive = receive;
            selectedTransmit = transmit;
        }
    }

    if (selectedInterface == m_interfaceName) {
        m_receiveRate = static_cast<double>(
            selectedReceive >= m_previousReceive ? selectedReceive - m_previousReceive : 0)
            / (static_cast<double>(m_updateInterval) / 1000.0);
        m_transmitRate = static_cast<double>(
            selectedTransmit >= m_previousTransmit ? selectedTransmit - m_previousTransmit : 0)
            / (static_cast<double>(m_updateInterval) / 1000.0);
    } else {
        m_receiveRate = 0;
        m_transmitRate = 0;
    }

    m_interfaceName = selectedInterface;
    m_previousReceive = selectedReceive;
    m_previousTransmit = selectedTransmit;
}
