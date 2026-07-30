#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

class QNetworkReply;
class QUrl;

class MprisController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString service READ service NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString playbackStatus READ playbackStatus NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(QString timeText READ timeText NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)
    Q_PROPERTY(bool canControl READ canControl NOTIFY changed)

public:
    explicit MprisController(bool enabled = true, QObject *parent = nullptr);

    bool available() const { return !m_service.isEmpty(); }
    QString service() const { return m_service; }
    QString identity() const { return m_identity; }
    QString playbackStatus() const { return m_playbackStatus; }
    bool playing() const { return m_playbackStatus == QStringLiteral("Playing"); }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artUrl() const { return m_artUrl; }
    QString timeText() const;
    bool canGoNext() const { return m_canGoNext; }
    bool canGoPrevious() const { return m_canGoPrevious; }
    bool canControl() const { return m_canControl; }

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    void setEnabled(bool enabled);

signals:
    void changed();

private slots:
    void serviceOwnerChanged(const QString &name,
                             const QString &oldOwner,
                             const QString &newOwner);
    void propertiesChanged(const QString &interface,
                           const QVariantMap &properties,
                           const QStringList &invalidated);
    void positionSeeked(qlonglong position);

private:
    void scheduleRefresh();
    void refreshPlayers();
    void finishRefresh(quint64 generation);
    void finishPlayerScan(quint64 generation);
    void selectPlayer(const QString &service, const QVariantMap &playerProperties);
    void updateFromProperties(const QVariantMap &properties);
    void updateArtwork(const QVariant &value);
    void startArtworkRequest(const QUrl &url, quint64 generation, int redirects);
    void cancelArtworkRequest();
    QString cacheArtwork(const QByteArray &data);
    void clear();
    void callPlayer(const QString &method);

    QString m_service;
    QString m_identity;
    QString m_playbackStatus;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_artUrl;
    qint64 m_positionUs = 0;
    qint64 m_durationUs = 0;
    QTimer m_positionTimer;
    QTimer m_refreshTimer;
    QTimer m_artTimeoutTimer;
    QNetworkAccessManager m_networkAccessManager;
    QPointer<QNetworkReply> m_artReply;
    QTemporaryDir m_artCacheDirectory;
    QByteArray m_artDownload;
    QString m_artMetadataUrl;
    QStringList m_artCacheFiles;
    quint64 m_artRequestGeneration = 0;
    quint64 m_artCacheSequence = 0;
    QStringList m_scanServices;
    QHash<QString, QVariantMap> m_scanProperties;
    int m_pendingPlayerQueries = 0;
    quint64 m_refreshGeneration = 0;
    quint64 m_selectionGeneration = 0;
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
    bool m_canControl = false;
    bool m_enabled = false;
    bool m_refreshInProgress = false;
    bool m_refreshQueued = false;
};
