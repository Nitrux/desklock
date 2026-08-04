#include "SystemMonitor.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

SystemMonitor::SystemMonitor(int updateInterval, bool enabled, QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::refresh);
    setUpdateInterval(updateInterval);
    setEnabled(enabled);
}

void SystemMonitor::setEnabled(bool enabled)
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
    }
}

void SystemMonitor::setUpdateInterval(int updateInterval)
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
    const QString ram = locale.toString(m_memoryUsage);
    if (!online()) {
        return tr("CPU %1%  |  RAM %2%  |  Offline").arg(cpu, ram);
    }

    return tr("CPU %1%  |  RAM %2%  |  %3: ↓ %4  |  ↑ %5")
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
    QProcess process;
    process.start(QStringLiteral("free"));
    if (process.waitForStarted(1000)
        && process.waitForFinished(1000)
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0) {
        const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QStringList lines =
            output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (!line.startsWith(QStringLiteral("Mem:"))) {
                continue;
            }

            const QStringList fields = line.split(
                QRegularExpression(QStringLiteral(R"(\s+)")),
                Qt::SkipEmptyParts);
            if (fields.size() >= 3) {
                bool totalValid = false;
                bool usedValid = false;
                const double total = fields.at(1).toDouble(&totalValid);
                const double used = fields.at(2).toDouble(&usedValid);
                if (totalValid && usedValid && total > 0.0) {
                    const int percentage =
                        qBound(0, qRound((used / total) * 100.0), 100);
                    if (m_memoryUsage != percentage) {
                        m_memoryUsage = percentage;
                        emit memoryUsageChanged(m_memoryUsage);
                    }
                    return;
                }
            }
            break;
        }
    }

    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to read /proc/meminfo:" << file.errorString();
        return;
    }

    quint64 total = 0;
    quint64 available = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const qsizetype separator = line.indexOf(':');
        if (separator < 0) {
            continue;
        }

        const QByteArray key = line.left(separator).trimmed();
        const QByteArray valueField =
            line.mid(separator + 1).trimmed().split(' ').value(0);
        bool valid = false;
        const quint64 value = valueField.toULongLong(&valid);
        if (!valid) {
            continue;
        }

        if (key == "MemTotal") {
            total = value;
        } else if (key == "MemAvailable") {
            available = value;
        }
    }

    if (total == 0 || available == 0) {
        qWarning("Unable to determine memory usage");
        return;
    }

    const quint64 used = total - qMin(total, available);
    const int percentage = qBound(
        0,
        qRound(100.0 * static_cast<double>(used) / static_cast<double>(total)),
        100);
    if (m_memoryUsage != percentage) {
        m_memoryUsage = percentage;
        emit memoryUsageChanged(m_memoryUsage);
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
