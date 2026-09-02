// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include "CurrentUser.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
bool usableImage(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        return false;
    }
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    return reader.canRead();
}
}

CurrentUser::CurrentUser(const QString &avatarOverride, QObject *parent)
    : QObject(parent)
{
    const passwd *entry = getpwuid(getuid());
    if (!entry) {
        m_username = QString::fromLocal8Bit(qgetenv("USER"));
        m_realName = m_username;
        m_home = QDir::homePath();
    } else {
        m_username = QString::fromLocal8Bit(entry->pw_name);
        m_realName = QString::fromLocal8Bit(entry->pw_gecos).section(',', 0, 0);
        if (m_realName.isEmpty()) {
            m_realName = m_username;
        }
        m_home = QString::fromLocal8Bit(entry->pw_dir);
    }

    setAvatarOverride(avatarOverride);
}

void CurrentUser::setAvatarOverride(const QString &avatarOverride)
{
    QString configured = avatarOverride.trimmed();
    configured.replace(QStringLiteral("%u"), m_username);
    configured.replace(QStringLiteral("%h"), m_home);

    const QStringList candidates{
        configured,
        m_home + QStringLiteral("/.face"),
        m_home + QStringLiteral("/.face.icon"),
        QStringLiteral("/var/lib/AccountsService/icons/") + m_username,
    };

    QString avatar = QStringLiteral("qrc:/icons/user-avatar.svg");
    for (const QString &candidate : candidates) {
        if (usableImage(candidate)) {
            avatar = candidate;
            break;
        }
    }

    if (m_avatar == avatar) {
        return;
    }
    m_avatar = avatar;
    emit avatarChanged();
}
