#include "SessionLock.h"

#include "ext-session-lock-v1-client-protocol.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>

#include <qpa/qplatformnativeinterface.h>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <algorithm>
#include <cstring>

namespace {
const wl_registry_listener registryListener{
    SessionLock::registryGlobal,
    SessionLock::registryGlobalRemove,
};

const ext_session_lock_v1_listener lockListener{
    SessionLock::lockLocked,
    SessionLock::lockFinished,
};

const ext_session_lock_surface_v1_listener surfaceListener{
    SessionLock::surfaceConfigure,
};

const wl_callback_listener syncListener{
    SessionLock::syncDone,
};
}

class DesklockShellSurface final : public QtWaylandClient::QWaylandShellSurface
{
public:
    DesklockShellSurface(QtWaylandClient::QWaylandWindow *window,
                         SessionLock::LockSurface *lockSurface)
        : QWaylandShellSurface(window)
        , m_lockSurface(lockSurface)
    {
    }

    ~DesklockShellSurface() override
    {
        if (!m_lockSurface) {
            return;
        }

        SessionLock *owner = m_lockSurface->owner;
        m_lockSurface->shellSurface = nullptr;
        if (owner) {
            owner->destroySurface(m_lockSurface);
        }
    }

    bool isExposed() const override
    {
        return m_configured;
    }

    bool commitSurfaceRole() const override
    {
        // ext-session-lock-v1 sends configure immediately and forbids the
        // initial null-buffer commit used to bootstrap other shell protocols.
        return false;
    }

    void applyConfigure() override
    {
        resizeFromApplyConfigure(m_pendingSize);
    }

    void configure(uint32_t serial, uint32_t width, uint32_t height)
    {
        if (!m_lockSurface || !m_lockSurface->lockSurface) {
            qCritical() << "Received a configure for an inactive lock surface";
            return;
        }

        ext_session_lock_surface_v1_ack_configure(
            m_lockSurface->lockSurface, serial);
        m_lockSurface->configured = true;
        m_pendingSize = QSize(static_cast<int>(width), static_cast<int>(height));

        const QString outputName = m_lockSurface->screen
            ? m_lockSurface->screen->name()
            : QStringLiteral("<removed>");
        qInfo() << "Lock surface configured"
                << outputName
                << m_pendingSize << "serial" << serial;

        if (!m_configured) {
            m_configured = true;
            applyConfigure();
            window()->updateExposure();
        } else {
            // Qt coordinates this with its render thread so the next commit
            // has the exact dimensions associated with the acknowledged serial.
            window()->applyConfigureWhenPossible();
        }
    }

    void clearLockSurface()
    {
        m_lockSurface = nullptr;
    }

private:
    SessionLock::LockSurface *m_lockSurface = nullptr;
    QSize m_pendingSize;
    bool m_configured = false;
};

class DesklockShellIntegration final : public QtWaylandClient::QWaylandShellIntegration
{
public:
    explicit DesklockShellIntegration(SessionLock *owner)
        : m_owner(owner)
    {
    }

    bool initialize(QtWaylandClient::QWaylandDisplay *) override
    {
        return m_owner != nullptr;
    }

    QtWaylandClient::QWaylandShellSurface *createShellSurface(
        QtWaylandClient::QWaylandWindow *window) override
    {
        SessionLock::LockSurface *lockSurface = m_owner->createSurface(window);
        if (!lockSurface) {
            qCritical() << "Failed to create the ext-session-lock surface role";
            return nullptr;
        }

        auto *shellSurface = new DesklockShellSurface(window, lockSurface);
        lockSurface->shellSurface = shellSurface;
        return shellSurface;
    }

private:
    SessionLock *m_owner = nullptr;
};

SessionLock::SessionLock(QObject *parent)
    : QObject(parent)
    , m_shellIntegration(std::make_unique<DesklockShellIntegration>(this))
{
}

SessionLock::~SessionLock()
{
    while (!m_surfaces.isEmpty()) {
        destroySurface(m_surfaces.constLast());
    }

    if (m_unlockSync) {
        wl_callback_destroy(m_unlockSync);
    }
    if (m_lock) {
        if (m_locked) {
            // A locked client must never send destroy. Discard only the local
            // proxy so the compositor remains securely locked on abnormal exit.
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(m_lock));
        } else {
            ext_session_lock_v1_destroy(m_lock);
        }
    }
    if (m_manager) {
        ext_session_lock_manager_v1_destroy(m_manager);
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
}

bool SessionLock::initialize()
{
    if (m_display) {
        return supported();
    }

    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        setError(tr("Desklock requires a Wayland session"));
        return false;
    }

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native) {
        setError(tr("Unable to access Qt Wayland integration"));
        return false;
    }

    m_display = static_cast<wl_display *>(
        native->nativeResourceForIntegration("wl_display"));
    if (!m_display) {
        setError(tr("Unable to access the Wayland display"));
        return false;
    }

    qInfo() << "Discovering ext-session-lock-v1";
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registryListener, this);
    if (wl_display_roundtrip(m_display) < 0 || !m_manager) {
        setError(tr("The compositor does not support ext-session-lock-v1"));
        return false;
    }

    m_lock = ext_session_lock_manager_v1_lock(m_manager);
    if (!m_lock) {
        setError(tr("Unable to allocate the session lock object"));
        return false;
    }

    ext_session_lock_v1_add_listener(m_lock, &lockListener, this);
    if (wl_display_roundtrip(m_display) < 0) {
        setError(tr("The Wayland connection failed while requesting the session lock"));
        return false;
    }
    if (!m_lock) {
        return false;
    }

    wl_display_flush(m_display);
    qInfo() << "Session lock requested";
    emit supportedChanged();
    return true;
}

bool SessionLock::attachWindow(QWindow *window, QScreen *screen)
{
    if (!window || !screen || !m_lock || findSurface(window)) {
        qWarning() << "Rejected invalid or duplicate lock-window attachment";
        return false;
    }
    if (window->isVisible()) {
        setError(tr("Lock windows must be attached before becoming visible"));
        return false;
    }
    if (window->handle()) {
        setError(tr("Lock windows must not acquire a native Wayland surface before attachment"));
        return false;
    }

    window->setScreen(screen);
    window->setFlags(Qt::FramelessWindowHint);
    m_pendingScreens.insert(window, screen);
    window->create();

    auto *waylandWindow =
        dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    if (!waylandWindow) {
        m_pendingScreens.remove(window);
        setError(tr("Unable to access the Qt Wayland window"));
        return false;
    }

    if (!m_shellIntegrationInitialized) {
        m_shellIntegrationInitialized =
            m_shellIntegration->initialize(waylandWindow->display());
        if (!m_shellIntegrationInitialized) {
            m_pendingScreens.remove(window);
            setError(tr("Unable to initialize the session-lock shell integration"));
            return false;
        }
    }

    // This must happen before show(): QWaylandWindow creates the shell role
    // while transitioning to visible, before it is allowed to commit a buffer.
    waylandWindow->setShellIntegration(m_shellIntegration.get());
    window->show();
    m_pendingScreens.remove(window);

    if (!findSurface(window)) {
        window->hide();
        setError(tr("Unable to assign the session-lock role for %1").arg(screen->name()));
        return false;
    }

    qInfo() << "Attached lock window to output" << screen->name();
    wl_display_flush(m_display);
    return true;
}

SessionLock::LockSurface *SessionLock::createSurface(
    QtWaylandClient::QWaylandWindow *waylandWindow)
{
    if (!waylandWindow || !m_lock) {
        return nullptr;
    }

    QWindow *window = waylandWindow->window();
    QScreen *screen = m_pendingScreens.value(window);
    if (!window || !screen) {
        qCritical() << "No pending output for a new lock surface";
        return nullptr;
    }

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    auto *waylandSurface = waylandWindow->wlSurface();
    auto *output = native
        ? static_cast<wl_output *>(native->nativeResourceForScreen("output", screen))
        : nullptr;
    if (!waylandSurface || !output) {
        qCritical() << "Missing wl_surface or wl_output for" << screen->name();
        return nullptr;
    }

    auto *surface = new LockSurface;
    surface->owner = this;
    surface->window = window;
    surface->screen = screen;
    surface->waylandSurface = waylandSurface;
    surface->output = output;
    surface->lockSurface =
        ext_session_lock_v1_get_lock_surface(m_lock, waylandSurface, output);
    if (!surface->lockSurface) {
        delete surface;
        return nullptr;
    }

    ext_session_lock_surface_v1_add_listener(
        surface->lockSurface, &surfaceListener, surface);
    m_surfaces.append(surface);
    qInfo() << "Created protocol lock surface for" << screen->name();
    return surface;
}

void SessionLock::detachWindow(QWindow *window)
{
    LockSurface *surface = findSurface(window);
    if (!surface) {
        return;
    }

    qInfo() << "Detaching lock window from output"
            << (surface->screen ? surface->screen->name() : QStringLiteral("<removed>"));
    destroySurface(surface);
}

void SessionLock::unlock()
{
    if (!m_lock) {
        qWarning() << "Ignoring unlock without an active lock object";
        return;
    }
    if (!m_locked) {
        qInfo() << "Unlock requested before compositor confirmation; deferring";
        m_unlockPending = true;
        return;
    }

    qInfo() << "Sending unlock_and_destroy";
    ext_session_lock_v1_unlock_and_destroy(m_lock);
    m_lock = nullptr;
    emit supportedChanged();
    m_locked = false;
    emit lockedChanged();

    while (!m_surfaces.isEmpty()) {
        destroySurface(m_surfaces.constLast());
    }

    m_unlockSync = wl_display_sync(m_display);
    wl_callback_add_listener(m_unlockSync, &syncListener, this);
    wl_display_flush(m_display);
}

void SessionLock::registryGlobal(void *data,
                                 wl_registry *registry,
                                 uint32_t name,
                                 const char *interface,
                                 uint32_t version)
{
    auto *self = static_cast<SessionLock *>(data);
    if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        self->m_manager = static_cast<ext_session_lock_manager_v1 *>(
            wl_registry_bind(registry,
                             name,
                             &ext_session_lock_manager_v1_interface,
                             std::min(version, 1U)));
        qInfo() << "Bound ext-session-lock-v1 manager version"
                << std::min(version, 1U);
    }
}

void SessionLock::registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

void SessionLock::lockLocked(void *data, ext_session_lock_v1 *)
{
    auto *self = static_cast<SessionLock *>(data);
    self->m_locked = true;
    qInfo() << "Compositor confirmed the session is securely locked";
    emit self->lockedChanged();
    if (self->m_unlockPending) {
        self->m_unlockPending = false;
        self->unlock();
    }
}

void SessionLock::lockFinished(void *data, ext_session_lock_v1 *lock)
{
    auto *self = static_cast<SessionLock *>(data);
    qWarning() << "Compositor finished the session lock; locked event received:"
               << self->m_locked;

    if (self->m_locked) {
        self->unlock();
        return;
    }

    ext_session_lock_v1_destroy(lock);
    self->m_lock = nullptr;
    emit self->supportedChanged();
    while (!self->m_surfaces.isEmpty()) {
        self->destroySurface(self->m_surfaces.constLast());
    }
    self->setError(self->tr("The compositor refused the session lock"));
    emit self->lockDenied();
}

void SessionLock::surfaceConfigure(void *data,
                                   ext_session_lock_surface_v1 *,
                                   uint32_t serial,
                                   uint32_t width,
                                   uint32_t height)
{
    auto *surface = static_cast<LockSurface *>(data);
    if (!surface || !surface->shellSurface) {
        qCritical() << "Configure arrived without a Qt shell surface";
        return;
    }
    surface->shellSurface->configure(serial, width, height);
}

void SessionLock::syncDone(void *data, wl_callback *callback, uint32_t)
{
    auto *self = static_cast<SessionLock *>(data);
    wl_callback_destroy(callback);
    self->m_unlockSync = nullptr;
    qInfo() << "Compositor processed unlock; exiting";
    emit self->unlocked();
    QCoreApplication::quit();
}

void SessionLock::setError(const QString &message)
{
    if (m_error == message) {
        return;
    }
    m_error = message;
    qCritical().noquote() << message;
    emit errorChanged();
}

void SessionLock::destroySurface(LockSurface *surface)
{
    if (!surface) {
        return;
    }

    m_surfaces.removeOne(surface);
    if (surface->shellSurface) {
        surface->shellSurface->clearLockSurface();
        surface->shellSurface = nullptr;
    }
    if (surface->lockSurface) {
        ext_session_lock_surface_v1_destroy(surface->lockSurface);
        surface->lockSurface = nullptr;
    }
    delete surface;
}

SessionLock::LockSurface *SessionLock::findSurface(QWindow *window)
{
    const auto iterator = std::find_if(
        m_surfaces.cbegin(),
        m_surfaces.cend(),
        [window](const LockSurface *surface) { return surface->window == window; });
    return iterator == m_surfaces.cend() ? nullptr : *iterator;
}
