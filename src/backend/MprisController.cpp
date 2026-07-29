#include "MprisController.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QTimer>

namespace {
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto rootInterface = "org.mpris.MediaPlayer2";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto servicePrefix = "org.mpris.MediaPlayer2.";

QVariantMap variantMap(const QVariant &value)
{
    if (value.canConvert<QVariantMap>()) {
        return value.toMap();
    }
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    }
    return {};
}

QStringList stringList(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        return value.toStringList();
    }
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QStringList>(value.value<QDBusArgument>());
    }
    return {};
}

QVariant unwrapped(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

QString formattedTime(qint64 microseconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, microseconds / 1000000);
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}
}

MprisController::MprisController(bool enabled, QObject *parent)
    : QObject(parent)
{
    m_positionTimer.setInterval(1000);
    connect(&m_positionTimer, &QTimer::timeout, this, [this]() {
        if (!playing()) {
            return;
        }
        const qint64 nextPosition = m_positionUs + 1000000;
        const qint64 boundedPosition = m_durationUs > 0
            ? qMin(nextPosition, m_durationUs) : nextPosition;
        if (boundedPosition == m_positionUs) {
            return;
        }
        m_positionUs = boundedPosition;
        emit changed();
    });
    setEnabled(enabled);
}

void MprisController::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    if (m_enabled) {
        QDBusConnection::sessionBus().connect(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("NameOwnerChanged"),
            this,
            SLOT(serviceOwnerChanged(QString,QString,QString)));
        QTimer::singleShot(0, this, &MprisController::refreshPlayers);
    } else {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("NameOwnerChanged"),
            this,
            SLOT(serviceOwnerChanged(QString,QString,QString)));
        clear();
    }
}

void MprisController::playPause()
{
    if (m_canControl) {
        callPlayer(QStringLiteral("PlayPause"));
    }
}

void MprisController::next()
{
    if (m_canGoNext) {
        callPlayer(QStringLiteral("Next"));
    }
}

void MprisController::previous()
{
    if (m_canGoPrevious) {
        callPlayer(QStringLiteral("Previous"));
    }
}

QString MprisController::timeText() const
{
    const QString duration = m_durationUs > 0
        ? formattedTime(m_durationUs) : QStringLiteral("--:--");
    return formattedTime(m_positionUs) + QStringLiteral(" / ") + duration;
}

void MprisController::positionSeeked(qlonglong position)
{
    if (!m_enabled) {
        return;
    }
    m_positionUs = qMax<qint64>(0, position);
    if (m_durationUs > 0) {
        m_positionUs = qMin(m_positionUs, m_durationUs);
    }
    emit changed();
}

void MprisController::serviceOwnerChanged(const QString &name,
                                          const QString &,
                                          const QString &)
{
    if (!m_enabled) {
        return;
    }
    if (name.startsWith(QLatin1String(servicePrefix))) {
        refreshPlayers();
    }
}

void MprisController::propertiesChanged(const QString &interface,
                                        const QVariantMap &properties,
                                        const QStringList &)
{
    if (!m_enabled) {
        return;
    }
    if (interface != QLatin1String(playerInterface)) {
        return;
    }
    updateFromProperties(properties);
    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        refreshPlayers();
    }
}

QVariantMap MprisController::propertiesFor(const QString &service,
                                           const QString &interface) const
{
    QDBusInterface properties(
        service,
        QLatin1String(objectPath),
        QLatin1String(propertiesInterface),
        QDBusConnection::sessionBus());
    const QDBusMessage reply =
        properties.call(QStringLiteral("GetAll"), interface);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return variantMap(reply.arguments().constFirst());
}

void MprisController::refreshPlayers()
{
    if (!m_enabled) {
        return;
    }
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        clear();
        return;
    }

    const QDBusReply<QStringList> reply = bus->registeredServiceNames();
    if (!reply.isValid()) {
        clear();
        return;
    }

    QString selected;
    QString paused;
    QString fallback;
    for (const QString &service : reply.value()) {
        if (!service.startsWith(QLatin1String(servicePrefix))) {
            continue;
        }
        const QVariantMap properties = propertiesFor(service, QLatin1String(playerInterface));
        if (properties.isEmpty()) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = service;
        }
        const QString status = properties.value(QStringLiteral("PlaybackStatus")).toString();
        if (status == QStringLiteral("Playing")) {
            selected = service;
            break;
        }
        if (status == QStringLiteral("Paused") && paused.isEmpty()) {
            paused = service;
        }
    }

    if (selected.isEmpty()) {
        selected = paused.isEmpty() ? fallback : paused;
    }
    if (selected.isEmpty()) {
        clear();
        return;
    }
    selectPlayer(selected);
}

void MprisController::selectPlayer(const QString &service)
{
    if (m_service != service) {
        if (!m_service.isEmpty()) {
            QDBusConnection::sessionBus().disconnect(
                m_service,
                QLatin1String(objectPath),
                QLatin1String(propertiesInterface),
                QStringLiteral("PropertiesChanged"),
                this,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
            QDBusConnection::sessionBus().disconnect(
                m_service,
                QLatin1String(objectPath),
                QLatin1String(playerInterface),
                QStringLiteral("Seeked"),
                this,
                SLOT(positionSeeked(qlonglong)));
        }
        m_positionUs = 0;
        m_durationUs = 0;
        m_service = service;
        QDBusConnection::sessionBus().connect(
            m_service,
            QLatin1String(objectPath),
            QLatin1String(propertiesInterface),
            QStringLiteral("PropertiesChanged"),
            this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        QDBusConnection::sessionBus().connect(
            m_service,
            QLatin1String(objectPath),
            QLatin1String(playerInterface),
            QStringLiteral("Seeked"),
            this,
            SLOT(positionSeeked(qlonglong)));
    }

    const QVariantMap rootProperties = propertiesFor(m_service, QLatin1String(rootInterface));
    m_identity = rootProperties.value(QStringLiteral("Identity")).toString();

    const QVariantMap playerProperties =
        propertiesFor(m_service, QLatin1String(playerInterface));
    updateFromProperties(playerProperties);
    emit changed();
}

void MprisController::updateFromProperties(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Metadata"))) {
        const QVariantMap metadata = variantMap(
            unwrapped(properties.value(QStringLiteral("Metadata"))));
        m_title = unwrapped(
            metadata.value(QStringLiteral("xesam:title"))).toString();
        m_artist = stringList(unwrapped(
            metadata.value(QStringLiteral("xesam:artist"))))
                           .join(QStringLiteral(", "));
        m_album = unwrapped(
            metadata.value(QStringLiteral("xesam:album"))).toString();
        m_artUrl = unwrapped(
            metadata.value(QStringLiteral("mpris:artUrl"))).toString();
        m_durationUs = qMax<qint64>(0, unwrapped(
            metadata.value(QStringLiteral("mpris:length"))).toLongLong());
        if (!properties.contains(QStringLiteral("Position"))) {
            m_positionUs = 0;
        }
    }
    if (properties.contains(QStringLiteral("Position"))) {
        m_positionUs = qMax<qint64>(0, unwrapped(
            properties.value(QStringLiteral("Position"))).toLongLong());
        if (m_durationUs > 0) {
            m_positionUs = qMin(m_positionUs, m_durationUs);
        }
    }
    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        m_playbackStatus = unwrapped(
            properties.value(QStringLiteral("PlaybackStatus"))).toString();
    }
    if (properties.contains(QStringLiteral("CanGoNext"))) {
        m_canGoNext = unwrapped(
            properties.value(QStringLiteral("CanGoNext"))).toBool();
    }
    if (properties.contains(QStringLiteral("CanGoPrevious"))) {
        m_canGoPrevious = unwrapped(
            properties.value(QStringLiteral("CanGoPrevious"))).toBool();
    }
    if (properties.contains(QStringLiteral("CanControl"))) {
        m_canControl = unwrapped(
            properties.value(QStringLiteral("CanControl"))).toBool();
    }

    if (playing()) {
        m_positionTimer.start();
    } else {
        m_positionTimer.stop();
    }
    emit changed();
}

void MprisController::clear()
{
    if (!m_service.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            m_service,
            QLatin1String(objectPath),
            QLatin1String(propertiesInterface),
            QStringLiteral("PropertiesChanged"),
            this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        QDBusConnection::sessionBus().disconnect(
            m_service,
            QLatin1String(objectPath),
            QLatin1String(playerInterface),
            QStringLiteral("Seeked"),
            this,
            SLOT(positionSeeked(qlonglong)));
    }
    m_service.clear();
    m_identity.clear();
    m_playbackStatus.clear();
    m_title.clear();
    m_artist.clear();
    m_album.clear();
    m_artUrl.clear();
    m_positionUs = 0;
    m_durationUs = 0;
    m_positionTimer.stop();
    m_canGoNext = false;
    m_canGoPrevious = false;
    m_canControl = false;
    emit changed();
}

void MprisController::callPlayer(const QString &method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        m_service,
        QLatin1String(objectPath),
        QLatin1String(playerInterface),
        method);
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}
