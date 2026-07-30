#include "AuthBackend.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>
#include <utility>

namespace {
struct ConversationData {
    QByteArray username;
    QByteArray password;
};

void secureClear(QByteArray &value)
{
    volatile char *bytes = value.data();
    for (qsizetype i = 0; i < value.size(); ++i) {
        bytes[i] = '\0';
    }
    value.clear();
}

int pamConversation(int count,
                    const pam_message **messages,
                    pam_response **responses,
                    void *data)
{
    if (count <= 0 || !messages || !responses || !data) {
        return PAM_CONV_ERR;
    }

    auto *conversation = static_cast<ConversationData *>(data);
    auto *result = static_cast<pam_response *>(
        std::calloc(static_cast<size_t>(count), sizeof(pam_response)));
    if (!result) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < count; ++i) {
        switch (messages[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
            result[i].resp = ::strdup(conversation->password.constData());
            if (!result[i].resp) {
                for (int j = 0; j < i; ++j) {
                    std::free(result[j].resp);
                }
                std::free(result);
                return PAM_BUF_ERR;
            }
            break;
        case PAM_PROMPT_ECHO_ON:
            result[i].resp = ::strdup(conversation->username.constData());
            if (!result[i].resp) {
                for (int j = 0; j < i; ++j) {
                    std::free(result[j].resp);
                }
                std::free(result);
                return PAM_BUF_ERR;
            }
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            result[i].resp = nullptr;
            break;
        default:
            for (int j = 0; j < i; ++j) {
                std::free(result[j].resp);
            }
            std::free(result);
            return PAM_CONV_ERR;
        }
    }

    *responses = result;
    return PAM_SUCCESS;
}
}

AuthBackend::AuthBackend(const QString &username, QObject *parent)
    : QObject(parent)
    , m_username(username)
{
}

AuthBackend::~AuthBackend()
{
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void AuthBackend::authenticate(const QString &password)
{
    if (m_processing.exchange(true)) {
        return;
    }

    qInfo() << "Starting PAM authentication attempt" << (m_attempts + 1);
    m_error.clear();
    emit errorChanged();
    emit processingChanged();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    const QByteArray username = m_username.toLocal8Bit();
    QByteArray secret = password.toUtf8();

    m_worker = std::thread([this, username, secret = std::move(secret)]() mutable {
        ConversationData conversation{username, std::move(secret)};
        pam_conv callback{pamConversation, &conversation};
        pam_handle_t *handle = nullptr;

        int result = pam_start("desklock", username.constData(), &callback, &handle);
        if (result == PAM_SUCCESS) {
            result = pam_authenticate(handle, PAM_SILENT);
        }
        if (result == PAM_SUCCESS) {
            result = pam_acct_mgmt(handle, PAM_SILENT);
        }

        QString message;
        if (result != PAM_SUCCESS) {
            const char *pamMessage = handle ? pam_strerror(handle, result) : "PAM initialization failed";
            message = QString::fromLocal8Bit(pamMessage);
        }

        secureClear(conversation.password);
        if (handle) {
            pam_end(handle, result);
        }

        const bool success = result == PAM_SUCCESS;
        QMetaObject::invokeMethod(
            this,
            [this, success, message]() { finishAuthentication(success, message); },
            Qt::QueuedConnection);
    });
}

void AuthBackend::reset()
{
    if (processing()) {
        return;
    }
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
    if (m_attempts != 0) {
        m_attempts = 0;
        emit attemptsChanged();
    }
}

void AuthBackend::finishAuthentication(bool success, const QString &error)
{
    m_processing.store(false);
    emit processingChanged();

    if (success) {
        qInfo() << "PAM authentication succeeded";
        m_error.clear();
        emit errorChanged();
        emit authenticationSucceeded();
        return;
    }

    qWarning() << "PAM authentication failed:" << error;
    ++m_attempts;
    emit attemptsChanged();
    m_error = error.isEmpty() ? tr("Authentication failed") : error;
    emit errorChanged();
}
