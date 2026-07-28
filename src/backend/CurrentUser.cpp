#include "CurrentUser.h"

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
        m_avatar = QStringLiteral("qrc:/icons/user-avatar.svg");
        return;
    }

    m_username = QString::fromLocal8Bit(entry->pw_name);
    m_realName = QString::fromLocal8Bit(entry->pw_gecos).section(',', 0, 0);
    if (m_realName.isEmpty()) {
        m_realName = m_username;
    }

    const QString home = QString::fromLocal8Bit(entry->pw_dir);
    QString configured = avatarOverride.trimmed();
    configured.replace(QStringLiteral("%u"), m_username);
    configured.replace(QStringLiteral("%h"), home);

    const QStringList candidates{
        configured,
        home + QStringLiteral("/.face"),
        home + QStringLiteral("/.face.icon"),
        QStringLiteral("/var/lib/AccountsService/icons/") + m_username,
    };
    for (const QString &candidate : candidates) {
        if (usableImage(candidate)) {
            m_avatar = candidate;
            return;
        }
    }

    m_avatar = QStringLiteral("qrc:/icons/user-avatar.svg");
}
