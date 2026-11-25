// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include "AuthBackend.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>

#include <security/pam_appl.h>

#include <functional>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {
struct ConversationData {
    std::function<char *(const QString &, bool)> ask;
    AuthBackend *backend;
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
        case PAM_PROMPT_ECHO_ON:
            result[i].resp = conversation->ask(QString::fromLocal8Bit(messages[i]->msg),
                                               messages[i]->msg_style == PAM_PROMPT_ECHO_OFF);
            if (!result[i].resp) {
                for (int j = 0; j < i; ++j) {
                    std::free(result[j].resp);
                }
                std::free(result);
                return PAM_BUF_ERR;
            }
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO: {
            const QString prompt = QString::fromLocal8Bit(messages[i]->msg);
            QMetaObject::invokeMethod(
                conversation->backend,
                "setPrompt",
                Qt::QueuedConnection,
                Q_ARG(QString, prompt));
            result[i].resp = nullptr;
            break;
        }
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
    cancel();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void AuthBackend::respond(const QString &answer)
{
    if (!m_needsInput || m_cancelled)
        return;
    m_needsInput = false;
    Q_EMIT promptChanged();
    std::lock_guard lock(m_mutex);
    m_answer = answer.toUtf8();
    m_answered = true;
    m_ready.notify_one();
}

void AuthBackend::cancel()
{
    std::lock_guard lock(m_mutex);
    m_cancelled = true;
    m_needsInput = false;
    m_ready.notify_one();
}

void AuthBackend::start()
{
    if (m_processing.exchange(true)) {
        return;
    }

    qInfo() << "Starting PAM authentication attempt" << (m_attempts + 1);
    m_error.clear();
    Q_EMIT errorChanged();
    setPrompt(QString());
    Q_EMIT processingChanged();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    const QByteArray username = m_username.toLocal8Bit();
    m_cancelled = false;

    m_worker = std::thread([this, username]() {
        auto ask = [this](const QString &prompt, bool secret) -> char * {
            std::unique_lock lock(m_mutex);
            m_answered = false;
            QMetaObject::invokeMethod(this, [this, prompt, secret] {
                if (!m_cancelled) {
                    m_secret = secret;
                    m_needsInput = true;
                    m_prompt = prompt;
                    Q_EMIT promptChanged();
                }
            }, Qt::QueuedConnection);
            m_ready.wait(lock, [this] { return m_cancelled || m_answered; });
            char *answer = m_cancelled ? nullptr : ::strdup(m_answer.constData());
            secureClear(m_answer);
            return answer;
        };
        ConversationData conversation{ask, this};
        pam_conv callback{pamConversation, &conversation};
        pam_handle_t *handle = nullptr;

        int result = pam_start("desklock", username.constData(), &callback, &handle);
        if (result == PAM_SUCCESS) {
            result = pam_authenticate(handle, PAM_DISALLOW_NULL_AUTHTOK);
        }
        if (result == PAM_SUCCESS) {
            result = pam_acct_mgmt(handle, PAM_SILENT);
        }

        QString message;
        if (result != PAM_SUCCESS) {
            const char *pamMessage = handle ? pam_strerror(handle, result) : "PAM initialization failed";
            message = QString::fromLocal8Bit(pamMessage);
        }

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
        Q_EMIT errorChanged();
    }
    setPrompt(QString());
    if (m_attempts != 0) {
        m_attempts = 0;
        Q_EMIT attemptsChanged();
    }
}

void AuthBackend::setPrompt(const QString &prompt)
{
    if (m_prompt == prompt) {
        return;
    }
    m_prompt = prompt;
    Q_EMIT promptChanged();
}

void AuthBackend::finishAuthentication(bool success, const QString &error)
{
    m_processing.store(false);
    Q_EMIT processingChanged();
    setPrompt(QString());

    if (m_cancelled)
        return;

    if (success) {
        qInfo() << "PAM authentication succeeded";
        m_error.clear();
        Q_EMIT errorChanged();
        Q_EMIT authenticationSucceeded();
        return;
    }

    qWarning() << "PAM authentication failed:" << error;
    ++m_attempts;
    Q_EMIT attemptsChanged();
    m_error = error.isEmpty() ? tr("Authentication failed") : error;
    Q_EMIT errorChanged();
}
