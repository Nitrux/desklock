#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class CurrentUser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString username READ username CONSTANT)
    Q_PROPERTY(QString realName READ realName CONSTANT)
    Q_PROPERTY(QString avatar READ avatar CONSTANT)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl CONSTANT)

public:
    explicit CurrentUser(const QString &avatarOverride = {}, QObject *parent = nullptr);

    QString username() const { return m_username; }
    QString realName() const { return m_realName; }
    QString avatar() const { return m_avatar; }
    QUrl avatarUrl() const
    {
        return m_avatar.startsWith(QStringLiteral("qrc:"))
            ? QUrl(m_avatar) : QUrl::fromLocalFile(m_avatar);
    }

private:
    QString m_username;
    QString m_realName;
    QString m_avatar;
};
