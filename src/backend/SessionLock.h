#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QVector>
#include <QWindow>

#include <memory>

#include <wayland-client.h>

struct ext_session_lock_manager_v1;
struct ext_session_lock_v1;
struct ext_session_lock_surface_v1;
class AuthBackend;

namespace QtWaylandClient {
class QWaylandShellIntegration;
class QWaylandWindow;
}

class DesklockShellIntegration;
class DesklockShellSurface;

class SessionLock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit SessionLock(AuthBackend *authentication, QObject *parent = nullptr);
    ~SessionLock() override;

    bool supported() const { return m_lock != nullptr; }
    bool locked() const { return m_locked; }
    QString error() const { return m_error; }

    bool initialize();
    void setUnlockDelay(int milliseconds);

    Q_INVOKABLE bool attachWindow(QWindow *window, QScreen *screen);
    Q_INVOKABLE void detachWindow(QWindow *window);

signals:
    void supportedChanged();
    void lockedChanged();
    void errorChanged();
    void lockDenied();
    void unlockAuthorized();
    void unlocked();

public:
    static void registryGlobal(void *data,
                               wl_registry *registry,
                               uint32_t name,
                               const char *interface,
                               uint32_t version);
    static void registryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void lockLocked(void *data, ext_session_lock_v1 *lock);
    static void lockFinished(void *data, ext_session_lock_v1 *lock);
    static void surfaceConfigure(void *data,
                                 ext_session_lock_surface_v1 *surface,
                                 uint32_t serial,
                                 uint32_t width,
                                 uint32_t height);
    static void syncDone(void *data, wl_callback *callback, uint32_t serial);

private:
    struct LockSurface {
        SessionLock *owner = nullptr;
        QPointer<QWindow> window;
        QPointer<QScreen> screen;
        wl_surface *waylandSurface = nullptr;
        wl_output *output = nullptr;
        ext_session_lock_surface_v1 *lockSurface = nullptr;
        DesklockShellSurface *shellSurface = nullptr;
        bool configured = false;
    };

    friend class DesklockShellIntegration;
    friend class DesklockShellSurface;

    LockSurface *createSurface(QtWaylandClient::QWaylandWindow *window);
    void authorizeUnlock();
    void unlock();
    void setError(const QString &message);
    void destroySurface(LockSurface *surface);
    LockSurface *findSurface(QWindow *window);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    ext_session_lock_manager_v1 *m_manager = nullptr;
    ext_session_lock_v1 *m_lock = nullptr;
    wl_callback *m_unlockSync = nullptr;
    QVector<LockSurface *> m_surfaces;
    QHash<QWindow *, QPointer<QScreen>> m_pendingScreens;
    std::unique_ptr<QtWaylandClient::QWaylandShellIntegration> m_shellIntegration;
    QString m_error;
    bool m_shellIntegrationInitialized = false;
    bool m_locked = false;
    bool m_unlockPending = false;
    bool m_unlockAuthorized = false;
    int m_unlockDelay = 0;
};
