// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#pragma once

#include <QObject>
#include <QString>

#include <QByteArray>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <thread>

class AuthBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(bool secret READ secret NOTIFY promptChanged)
    Q_PROPERTY(bool needsInput READ needsInput NOTIFY promptChanged)
    Q_PROPERTY(int attempts READ attempts NOTIFY attemptsChanged)

public:
    explicit AuthBackend(const QString &username, QObject *parent = nullptr);
    ~AuthBackend() override;

    bool processing() const { return m_processing.load(); }
    QString error() const { return m_error; }
    bool needsInput() const { return m_needsInput; }
    bool secret() const { return m_secret; }
    QString prompt() const { return m_prompt; }
    int attempts() const { return m_attempts; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void respond(const QString &answer);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void processingChanged();
    void errorChanged();
    void promptChanged();
    void attemptsChanged();
    void authenticationSucceeded();

private:
    Q_SLOT void setPrompt(const QString &prompt);
    void finishAuthentication(bool success, const QString &error);

    bool m_secret = true;
    bool m_needsInput = false;
    std::atomic_bool m_cancelled = false;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    QByteArray m_answer;
    bool m_answered = false;
    QString m_username;
    QString m_error;
    QString m_prompt;
    std::atomic_bool m_processing = false;
    int m_attempts = 0;
    std::thread m_worker;
};
