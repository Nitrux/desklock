// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class CurrentUser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString username READ username CONSTANT)
    Q_PROPERTY(QString realName READ realName CONSTANT)
    Q_PROPERTY(QString avatar READ avatar NOTIFY avatarChanged)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY avatarChanged)

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
    void setAvatarOverride(const QString &avatarOverride);

signals:
    void avatarChanged();

private:
    QString m_username;
    QString m_realName;
    QString m_home;
    QString m_avatar;
};
