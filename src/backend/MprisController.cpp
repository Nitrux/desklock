#include "MprisController.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace {
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto rootInterface = "org.mpris.MediaPlayer2";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto servicePrefix = "org.mpris.MediaPlayer2.";
constexpr int dbusTimeoutMs = 1500;
constexpr int artTransferTimeoutMs = 3000;
constexpr int artTotalTimeoutMs = 5000;
constexpr int maximumArtRedirects = 3;
constexpr int maximumArtDimension = 4096;
constexpr qint64 maximumArtPixels = 16 * 1024 * 1024;
constexpr int cachedArtDimension = 512;
constexpr int imageAllocationLimitMb = 64;
constexpr int maximumPlayerCount = 32;
constexpr int maximumTextLength = 256;
constexpr int maximumArtistLength = 512;
constexpr int maximumArtUrlLength = 2048;
constexpr qint64 maximumArtFileSize = 16 * 1024 * 1024;
constexpr qint64 maximumDurationUs = 7LL * 24 * 60 * 60 * 1000000;

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

QString boundedString(const QVariant &value, int maximumLength = maximumTextLength)
{
    return unwrapped(value).toString().left(maximumLength);
}

QString boundedArtists(const QVariant &value)
{
    QStringList artists = stringList(unwrapped(value));
    if (artists.size() > 16) {
        artists = artists.mid(0, 16);
    }
    for (QString &artist : artists) {
        artist = artist.left(maximumTextLength);
    }
    return artists.join(QStringLiteral(", ")).left(maximumArtistLength);
}

QString safeLocalArtUrl(const QUrl &url)
{
    if (!url.isValid() || !url.isLocalFile()) {
        return {};
    }

    const QFileInfo file(url.toLocalFile());
    if (!file.isAbsolute() || !file.isFile() || !file.isReadable()
        || file.size() < 0 || file.size() > maximumArtFileSize) {
        return {};
    }
    return QUrl::fromLocalFile(file.absoluteFilePath()).toString();
}

QUrl safeHttpsArtUrl(const QUrl &candidate)
{
    if (!candidate.isValid()
        || candidate.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || candidate.host().isEmpty()
        || !candidate.userName().isEmpty()
        || !candidate.password().isEmpty()
        || (candidate.port() != -1 && candidate.port() != 443)) {
        return {};
    }

    QUrl normalized = candidate;
    normalized.setScheme(QStringLiteral("https"));
    normalized.setFragment({});
    return normalized;
}

bool supportedImageMimeType(const QString &header)
{
    const QString mime = header.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    return mime == QStringLiteral("image/jpeg")
        || mime == QStringLiteral("image/png")
        || mime == QStringLiteral("image/webp")
        || mime == QStringLiteral("image/avif");
}

QDBusPendingCall getAllProperties(const QString &service, const QString &interface)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        service,
        QLatin1String(objectPath),
        QLatin1String(propertiesInterface),
        QStringLiteral("GetAll"));
    message << interface;
    return QDBusConnection::sessionBus().asyncCall(message, dbusTimeoutMs);
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
    QImageReader::setAllocationLimit(imageAllocationLimitMb);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(50);
    m_artTimeoutTimer.setSingleShot(true);
    m_artTimeoutTimer.setInterval(artTotalTimeoutMs);
    connect(&m_artTimeoutTimer, &QTimer::timeout,
            this, &MprisController::cancelArtworkRequest);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MprisController::refreshPlayers);
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
        scheduleRefresh();
    } else {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("NameOwnerChanged"),
            this,
            SLOT(serviceOwnerChanged(QString,QString,QString)));
        m_refreshTimer.stop();
        ++m_refreshGeneration;
        ++m_selectionGeneration;
        m_refreshInProgress = false;
        m_refreshQueued = false;
        m_scanServices.clear();
        m_scanProperties.clear();
        m_pendingPlayerQueries = 0;
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
    m_positionUs = qBound<qint64>(0, position, maximumDurationUs);
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
        scheduleRefresh();
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
        scheduleRefresh();
    }
}

void MprisController::scheduleRefresh()
{
    if (!m_enabled) {
        return;
    }
    if (m_refreshInProgress) {
        m_refreshQueued = true;
        return;
    }
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void MprisController::refreshPlayers()
{
    if (!m_enabled) {
        return;
    }
    if (m_refreshInProgress) {
        m_refreshQueued = true;
        return;
    }

    m_refreshInProgress = true;
    m_refreshQueued = false;
    const quint64 generation = ++m_refreshGeneration;

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("ListNames"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, dbusTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<QStringList> reply = *finished;
        finished->deleteLater();
        if (!m_enabled || generation != m_refreshGeneration) {
            return;
        }
        if (reply.isError()) {
            clear();
            finishRefresh(generation);
            return;
        }

        QStringList services;
        for (const QString &service : reply.value()) {
            if (service.startsWith(QLatin1String(servicePrefix))) {
                services.append(service);
                if (services.size() >= maximumPlayerCount) {
                    break;
                }
            }
        }
        if (services.isEmpty()) {
            clear();
            finishRefresh(generation);
            return;
        }

        m_scanServices = services;
        m_scanProperties.clear();
        m_pendingPlayerQueries = services.size();
        for (const QString &service : services) {
            auto *propertiesWatcher = new QDBusPendingCallWatcher(
                getAllProperties(service, QLatin1String(playerInterface)), this);
            connect(propertiesWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, generation, service](QDBusPendingCallWatcher *propertiesFinished) {
                const QDBusPendingReply<QVariantMap> propertiesReply = *propertiesFinished;
                propertiesFinished->deleteLater();
                if (!m_enabled || generation != m_refreshGeneration) {
                    return;
                }
                if (!propertiesReply.isError() && !propertiesReply.value().isEmpty()) {
                    m_scanProperties.insert(service, propertiesReply.value());
                }
                if (--m_pendingPlayerQueries == 0) {
                    finishPlayerScan(generation);
                }
            });
        }
    });
}

void MprisController::finishRefresh(quint64 generation)
{
    if (generation != m_refreshGeneration) {
        return;
    }
    m_refreshInProgress = false;
    m_scanServices.clear();
    m_scanProperties.clear();
    m_pendingPlayerQueries = 0;
    if (m_refreshQueued) {
        m_refreshQueued = false;
        scheduleRefresh();
    }
}

void MprisController::finishPlayerScan(quint64 generation)
{
    if (!m_enabled || generation != m_refreshGeneration) {
        return;
    }

    QString selected;
    QString paused;
    QString fallback;
    for (const QString &service : m_scanServices) {
        const QVariantMap properties = m_scanProperties.value(service);
        if (properties.isEmpty()) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = service;
        }
        const QString status = boundedString(
            properties.value(QStringLiteral("PlaybackStatus")));
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
    } else {
        selectPlayer(selected, m_scanProperties.value(selected));
    }
    finishRefresh(generation);
}

void MprisController::selectPlayer(const QString &service,
                                   const QVariantMap &playerProperties)
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
        m_identity.clear();
        m_playbackStatus.clear();
        m_title.clear();
        m_artist.clear();
        m_album.clear();
        cancelArtworkRequest();
        m_artMetadataUrl.clear();
        m_artUrl.clear();
        m_positionUs = 0;
        m_durationUs = 0;
        m_canGoNext = false;
        m_canGoPrevious = false;
        m_canControl = false;
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

    updateFromProperties(playerProperties);
    const quint64 selectionGeneration = ++m_selectionGeneration;
    auto *rootWatcher = new QDBusPendingCallWatcher(
        getAllProperties(m_service, QLatin1String(rootInterface)), this);
    connect(rootWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, selectionGeneration, service](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<QVariantMap> reply = *finished;
        finished->deleteLater();
        if (!m_enabled || selectionGeneration != m_selectionGeneration
            || service != m_service || reply.isError()) {
            return;
        }
        m_identity = boundedString(
            reply.value().value(QStringLiteral("Identity")));
        emit changed();
    });
}

void MprisController::updateArtwork(const QVariant &value)
{
    const QString candidateText = boundedString(value, maximumArtUrlLength);
    const QUrl candidate(candidateText);
    const QString localUrl = safeLocalArtUrl(candidate);
    const QUrl httpsUrl = safeHttpsArtUrl(candidate);
    const QString metadataUrl = !localUrl.isEmpty()
        ? localUrl
        : (!httpsUrl.isEmpty() ? httpsUrl.toString(QUrl::FullyEncoded) : candidateText);

    if (metadataUrl == m_artMetadataUrl) {
        return;
    }

    cancelArtworkRequest();
    m_artMetadataUrl = metadataUrl;
    m_artUrl.clear();
    if (!localUrl.isEmpty()) {
        m_artUrl = localUrl;
        return;
    }
    if (httpsUrl.isEmpty()) {
        return;
    }

    const quint64 generation = m_artRequestGeneration;
    m_artTimeoutTimer.start();
    startArtworkRequest(httpsUrl, generation, 0);
}

void MprisController::startArtworkRequest(const QUrl &url,
                                          quint64 generation,
                                          int redirects)
{
    if (!m_enabled || generation != m_artRequestGeneration) {
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(artTransferTimeoutMs);
    request.setRawHeader("Accept", "image/avif,image/webp,image/png,image/jpeg");

    QNetworkReply *reply = m_networkAccessManager.get(request);
    reply->setReadBufferSize(maximumArtFileSize + 1);
    m_artReply = reply;
    m_artDownload.clear();

    connect(reply, &QNetworkReply::sslErrors, reply, [reply]() {
        reply->abort();
    });
    connect(reply, &QNetworkReply::metaDataChanged, this,
            [this, reply, generation]() {
        if (generation != m_artRequestGeneration || reply != m_artReply) {
            return;
        }
        bool validLength = false;
        const qint64 contentLength = reply->header(
            QNetworkRequest::ContentLengthHeader).toLongLong(&validLength);
        if (validLength && contentLength > maximumArtFileSize) {
            reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply, generation]() {
        if (generation != m_artRequestGeneration || reply != m_artReply) {
            return;
        }
        const qint64 remaining = maximumArtFileSize - m_artDownload.size();
        if (reply->bytesAvailable() > remaining) {
            reply->abort();
            return;
        }
        m_artDownload.append(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, redirects]() {
        reply->deleteLater();
        if (generation != m_artRequestGeneration || reply != m_artReply) {
            return;
        }
        m_artReply = nullptr;

        const qint64 remaining = maximumArtFileSize - m_artDownload.size();
        if (reply->bytesAvailable() > remaining) {
            m_artTimeoutTimer.stop();
            m_artDownload.clear();
            return;
        }
        m_artDownload.append(reply->readAll());

        if (reply->error() != QNetworkReply::NoError) {
            m_artTimeoutTimer.stop();
            m_artDownload.clear();
            return;
        }

        const QUrl redirectTarget = reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirectTarget.isEmpty()) {
            if (redirects >= maximumArtRedirects) {
                m_artTimeoutTimer.stop();
                m_artDownload.clear();
                return;
            }
            const QUrl redirectUrl = safeHttpsArtUrl(
                reply->url().resolved(redirectTarget));
            if (redirectUrl.isEmpty()) {
                m_artTimeoutTimer.stop();
                m_artDownload.clear();
                return;
            }
            startArtworkRequest(redirectUrl, generation, redirects + 1);
            return;
        }

        m_artTimeoutTimer.stop();
        if (!supportedImageMimeType(reply->header(
                QNetworkRequest::ContentTypeHeader).toString())
            || m_artDownload.isEmpty()) {
            m_artDownload.clear();
            return;
        }

        const QString cachedUrl = cacheArtwork(m_artDownload);
        m_artDownload.clear();
        if (cachedUrl.isEmpty() || generation != m_artRequestGeneration) {
            return;
        }
        m_artUrl = cachedUrl;
        emit changed();
    });
}

void MprisController::cancelArtworkRequest()
{
    ++m_artRequestGeneration;
    m_artTimeoutTimer.stop();
    m_artDownload.clear();
    if (m_artReply) {
        QNetworkReply *reply = m_artReply;
        m_artReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

QString MprisController::cacheArtwork(const QByteArray &data)
{
    if (!m_artCacheDirectory.isValid()) {
        return {};
    }

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return {};
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    if (format != "jpeg" && format != "jpg" && format != "png"
        && format != "webp" && format != "avif") {
        return {};
    }
    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid()
        || sourceSize.width() > maximumArtDimension
        || sourceSize.height() > maximumArtDimension
        || static_cast<qint64>(sourceSize.width()) * sourceSize.height()
            > maximumArtPixels) {
        return {};
    }
    if (sourceSize.width() > cachedArtDimension
        || sourceSize.height() > cachedArtDimension) {
        QSize scaledSize = sourceSize;
        scaledSize.scale(
            cachedArtDimension, cachedArtDimension, Qt::KeepAspectRatio);
        reader.setScaledSize(scaledSize);
    }

    const QImage image = reader.read();
    if (image.isNull()
        || image.width() > cachedArtDimension
        || image.height() > cachedArtDimension
        || image.sizeInBytes() > maximumArtFileSize) {
        return {};
    }

    const QString path = m_artCacheDirectory.filePath(
        QStringLiteral("artwork-%1.png").arg(++m_artCacheSequence));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || !image.save(&file, "PNG")
        || !file.commit()) {
        file.cancelWriting();
        return {};
    }

    m_artCacheFiles.append(path);
    while (m_artCacheFiles.size() > 4) {
        QFile::remove(m_artCacheFiles.takeFirst());
    }
    return QUrl::fromLocalFile(path).toString();
}

void MprisController::updateFromProperties(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Metadata"))) {
        const QVariantMap metadata = variantMap(
            unwrapped(properties.value(QStringLiteral("Metadata"))));
        m_title = boundedString(
            metadata.value(QStringLiteral("xesam:title")));
        m_artist = boundedArtists(
            metadata.value(QStringLiteral("xesam:artist")));
        m_album = boundedString(
            metadata.value(QStringLiteral("xesam:album")));
        updateArtwork(metadata.value(QStringLiteral("mpris:artUrl")));
        m_durationUs = qBound<qint64>(
            0,
            unwrapped(metadata.value(QStringLiteral("mpris:length"))).toLongLong(),
            maximumDurationUs);
        if (!properties.contains(QStringLiteral("Position"))) {
            m_positionUs = 0;
        }
    }
    if (properties.contains(QStringLiteral("Position"))) {
        m_positionUs = qBound<qint64>(
            0,
            unwrapped(properties.value(QStringLiteral("Position"))).toLongLong(),
            maximumDurationUs);
        if (m_durationUs > 0) {
            m_positionUs = qMin(m_positionUs, m_durationUs);
        }
    }
    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        const QString status = boundedString(
            properties.value(QStringLiteral("PlaybackStatus")), 16);
        m_playbackStatus = status == QStringLiteral("Playing")
                || status == QStringLiteral("Paused")
                || status == QStringLiteral("Stopped")
            ? status : QString();
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
    cancelArtworkRequest();
    m_artMetadataUrl.clear();
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
