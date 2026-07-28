#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

class AuthBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int attempts READ attempts NOTIFY attemptsChanged)

public:
    explicit AuthBackend(const QString &username, QObject *parent = nullptr);
    ~AuthBackend() override;

    bool processing() const { return m_processing.load(); }
    QString error() const { return m_error; }
    int attempts() const { return m_attempts; }

    Q_INVOKABLE void authenticate(const QString &password);
    Q_INVOKABLE void reset();

signals:
    void processingChanged();
    void errorChanged();
    void attemptsChanged();
    void authenticationSucceeded();

private:
    void finishAuthentication(bool success, const QString &error);

    QString m_username;
    QString m_error;
    std::atomic_bool m_processing = false;
    int m_attempts = 0;
    std::thread m_worker;
};
