#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

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
    QVariantMap propertiesFor(const QString &service, const QString &interface) const;
    void refreshPlayers();
    void selectPlayer(const QString &service);
    void updateFromProperties(const QVariantMap &properties);
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
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
    bool m_canControl = false;
};
