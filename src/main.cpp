#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <KLocalizedContext>
#include <KLocalizedString>

#include <cstdio>
#include <syslog.h>

#include "backend/AuthBackend.h"
#include "backend/CurrentUser.h"
#include "backend/MprisController.h"
#include "backend/ScreenRegistry.h"
#include "backend/SessionLock.h"
#include "backend/SystemBattery.h"
#include "backend/SystemMonitor.h"

namespace {
int syslogPriority(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return LOG_DEBUG;
    case QtInfoMsg: return LOG_INFO;
    case QtWarningMsg: return LOG_WARNING;
    case QtCriticalMsg: return LOG_ERR;
    case QtFatalMsg: return LOG_CRIT;
    }
    return LOG_NOTICE;
}

void syslogMessageHandler(QtMsgType type,
                          const QMessageLogContext &context,
                          const QString &message)
{
    const QByteArray text = message.toLocal8Bit();
    const QByteArray category = QByteArray(context.category ? context.category : "default");
    if (context.file) {
        ::syslog(syslogPriority(type), "%s: %s (%s:%u)",
                 category.constData(), text.constData(), context.file, context.line);
    } else {
        ::syslog(syslogPriority(type), "%s: %s", category.constData(), text.constData());
    }
    std::fprintf(stderr, "desklock[%s]: %s\n", category.constData(), text.constData());
}

class SyslogLogger
{
public:
    SyslogLogger()
    {
        ::openlog("desklock", LOG_PID | LOG_NDELAY, LOG_USER);
        qInstallMessageHandler(syslogMessageHandler);
    }

    ~SyslogLogger()
    {
        qInstallMessageHandler(nullptr);
        ::closelog();
    }
};

bool ensureConfigurationFile(const QString &path)
{
    if (QFileInfo::exists(path)) {
        return true;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QFile defaults(QStringLiteral(":/defaults/desklock.conf"));
    if (!defaults.open(QIODevice::ReadOnly)) {
        return false;
    }

    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly)
        || destination.write(defaults.readAll()) < 0
        || !destination.commit()) {
        return false;
    }

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
}

int main(int argc, char *argv[])
{
    KLocalizedString::setApplicationDomain("desklock");
    SyslogLogger logger;
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("org.mauikit.style"));
    application.setQuitOnLastWindowClosed(false);
    application.setApplicationName(QStringLiteral("desklock"));
    application.setApplicationDisplayName(QStringLiteral("Desklock"));
    application.setApplicationVersion(QStringLiteral("0.1.0"));
    qInfo() << "Desklock starting" << application.applicationVersion();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Secure QML session locker for Wayland"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QString defaultConfigPath =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/desklock/desklock.conf");
    const QCommandLineOption configOption(
        {QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Path to the Desklock configuration file"),
        QStringLiteral("path"),
        defaultConfigPath);
    parser.addOption(configOption);
    parser.process(application);

    const QString configPath = QFileInfo(parser.value(configOption)).absoluteFilePath();
    qInfo() << "Using configuration" << configPath;
    if (!ensureConfigurationFile(configPath)) {
        qCritical() << "Unable to create Desklock configuration:" << configPath;
        return 1;
    }

    CurrentUser currentUser;
    qInfo() << "Locking session for user" << currentUser.username()
            << "on" << QGuiApplication::screens().size() << "output(s)";
    AuthBackend authentication(currentUser.username());
    SystemBattery battery(30000, false);
    SystemMonitor systemMonitor(3000, false);
    MprisController mpris(false);
    SessionLock sessionLock(&authentication);
    ScreenRegistry screenRegistry;

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    context->setContextObject(new KLocalizedContext(&engine));
    context->setContextProperty(QStringLiteral("CurrentUser"), &currentUser);
    context->setContextProperty(QStringLiteral("Authentication"), &authentication);
    context->setContextProperty(QStringLiteral("Battery"), &battery);
    context->setContextProperty(QStringLiteral("SystemMonitor"), &systemMonitor);
    context->setContextProperty(QStringLiteral("Mpris"), &mpris);
    context->setContextProperty(QStringLiteral("SessionLock"), &sessionLock);
    context->setContextProperty(QStringLiteral("ScreenRegistry"), &screenRegistry);

    bool configurationLoaded = false;
    bool cursorHidden = false;
    const auto applyConfiguration = [&]() -> bool {
        QSettings config(configPath, QSettings::IniFormat);
        config.sync();
        if (config.status() != QSettings::NoError) {
            qWarning() << "Unable to reload configuration" << configPath
                       << "status" << config.status();
            return false;
        }

        QString timeFormat = config.value(
            QStringLiteral("Clock/TimeFormat"), QStringLiteral("hh:mm")).toString();
        if (timeFormat.isEmpty()) {
            timeFormat = QStringLiteral("hh:mm");
        }
        QString dateFormat = config.value(
            QStringLiteral("Clock/DateFormat"),
            QStringLiteral("dddd, dd MMMM yyyy")).toString();
        if (dateFormat.isEmpty()) {
            dateFormat = QStringLiteral("dddd, dd MMMM yyyy");
        }

        const bool showBattery =
            config.value(QStringLiteral("Battery/Enabled"), true).toBool();
        const bool debugBattery =
            config.value(QStringLiteral("Debug/debugBattery"), false).toBool();
        const bool showSystemMonitor =
            config.value(QStringLiteral("SystemMonitor/Enabled"), true).toBool();
        const bool showMediaControls =
            config.value(QStringLiteral("Media/Enabled"), true).toBool();
        const QString iconMode = config.value(
            QStringLiteral("Appearance/IconMode"), QStringLiteral("system"))
            .toString().trimmed().toLower() == QStringLiteral("nerd")
            ? QStringLiteral("nerd") : QStringLiteral("system");
        const int batteryUpdateInterval = qMax(1000, config.value(
            QStringLiteral("Battery/UpdateInterval"), 30000).toInt());
        const int systemMonitorUpdateInterval = qMax(1000, config.value(
            QStringLiteral("SystemMonitor/UpdateInterval"), 3000).toInt());
        constexpr int fadeInDuration = 350;
        constexpr int fadeOutDuration = 250;
        const bool hideCursor = config.value(
            QStringLiteral("Behavior/HideCursor"), true).toBool();

        currentUser.setAvatarOverride(
            config.value(QStringLiteral("Appearance/AvatarImage")).toString());
        battery.setUpdateInterval(batteryUpdateInterval);
        battery.setDebugBattery(debugBattery);
        battery.setEnabled(showBattery);
        systemMonitor.setUpdateInterval(systemMonitorUpdateInterval);
        systemMonitor.setEnabled(showSystemMonitor);
        mpris.setEnabled(showMediaControls);
        sessionLock.setUnlockDelay(fadeOutDuration);
        if (hideCursor != cursorHidden) {
            if (hideCursor) {
                QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
            } else {
                QGuiApplication::restoreOverrideCursor();
            }
            cursorHidden = hideCursor;
        }

        context->setContextProperty(QStringLiteral("ShowBattery"), showBattery);
        context->setContextProperty(QStringLiteral("IconMode"), iconMode);
        context->setContextProperty(
            QStringLiteral("ShowSystemMonitor"), showSystemMonitor);
        context->setContextProperty(
            QStringLiteral("ShowMediaControls"), showMediaControls);
        context->setContextProperty(
            QStringLiteral("BackgroundImage"),
            config.value(
                QStringLiteral("Appearance/BackgroundImage"),
                QStringLiteral("/usr/share/wallpapers/Blossom/contents/images/4096x2304.png"))
                .toString());
        context->setContextProperty(QStringLiteral("TimeFormat"), timeFormat);
        context->setContextProperty(QStringLiteral("DateFormat"), dateFormat);
        context->setContextProperty(
            QStringLiteral("LowercaseDate"),
            config.value(QStringLiteral("Clock/LowercaseDate"), false).toBool());
        context->setContextProperty(
            QStringLiteral("FadeInDuration"),
            fadeInDuration);
        context->setContextProperty(
            QStringLiteral("FadeOutDuration"),
            fadeOutDuration);
        context->setContextProperty(
            QStringLiteral("BlurEnabled"),
            config.value(QStringLiteral("Appearance/BlurEnabled"), true).toBool());
        context->setContextProperty(
            QStringLiteral("OverlayEnabled"),
            config.value(QStringLiteral("Appearance/OverlayEnabled"), true).toBool());
        context->setContextProperty(
            QStringLiteral("BackgroundBlurRadius"),
            qMax(0, config.value(
                QStringLiteral("Appearance/BackgroundBlurRadius"), 64).toInt()));
        context->setContextProperty(
            QStringLiteral("BackgroundOverlayOpacity"),
            qBound(0.0, config.value(
                QStringLiteral("Appearance/OverlayOpacity"), 0.76)
                .toDouble(), 1.0));

        qInfo() << (configurationLoaded
            ? "Configuration reloaded" : "Configuration loaded") << configPath;
        configurationLoaded = true;
        return true;
    };

    if (!applyConfiguration()) {
        qCritical() << "Unable to load Desklock configuration:" << configPath;
        return 1;
    }
    qInfo() << "Using avatar" << currentUser.avatarUrl();

    QFileSystemWatcher configWatcher;
    const QString configDirectory = QFileInfo(configPath).absolutePath();
    configWatcher.addPath(configDirectory);
    configWatcher.addPath(configPath);

    QTimer configReloadTimer;
    configReloadTimer.setSingleShot(true);
    configReloadTimer.setInterval(200);
    const auto scheduleConfigurationReload = [&configReloadTimer](const QString &) {
        configReloadTimer.start();
    };
    QObject::connect(
        &configWatcher, &QFileSystemWatcher::fileChanged,
        &application, scheduleConfigurationReload);
    QObject::connect(
        &configWatcher, &QFileSystemWatcher::directoryChanged,
        &application, scheduleConfigurationReload);
    QObject::connect(&configReloadTimer, &QTimer::timeout, &application, [&]() {
        const QFileInfo configInfo(configPath);
        if (!configInfo.isFile()) {
            qWarning() << "Configuration file is temporarily unavailable; keeping current settings"
                       << configPath;
            return;
        }
        if (!configWatcher.files().contains(configPath)) {
            configWatcher.addPath(configPath);
        }
        applyConfiguration();
    });

    bool qmlValidationFailed = false;
    QObject::connect(
        &engine,
        &QQmlEngine::warnings,
        &application,
        [&qmlValidationFailed](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                const QString description = warning.description();
                const bool isProgrammingError =
                    description.contains(QStringLiteral("ReferenceError"))
                    || description.contains(QStringLiteral("TypeError"))
                    || description.contains(QStringLiteral("SyntaxError"))
                    || description.contains(QStringLiteral("is not defined"))
                    || description.contains(QStringLiteral("is not a type"))
                    || description.contains(QStringLiteral("unavailable"))
                    || description.contains(QStringLiteral("No such file or directory"))
                    || description.contains(QStringLiteral("Cannot assign"))
                    || description.contains(QStringLiteral("Invalid property"))
                    || description.contains(QStringLiteral("Required property"));
                qWarning().noquote() << "QML warning:" << warning.toString();
                qmlValidationFailed = qmlValidationFailed || isProgrammingError;
            }
        });

    const QUrl mainUrl(QStringLiteral("qrc:/resources/qml/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &application,
        [mainUrl](QObject *object, const QUrl &url) {
            if (!object && url == mainUrl) {
                qCritical("Unable to create the Desklock QML interface");
            }
        },
        Qt::QueuedConnection);
    // Validate the complete QML component tree before touching ext-session-lock-v1.
    // Exiting during lock negotiation is unsafe on compositors with fragile client cleanup.
    engine.load(mainUrl);
    if (engine.rootObjects().isEmpty() || qmlValidationFailed) {
        qCritical("Desklock did not request a session lock because its QML interface failed validation");
        return 2;
    }

    QObject *rootObject = engine.rootObjects().constFirst();
    if (!rootObject->property("interfaceReady").toBool()) {
        qCritical("Desklock did not request a session lock because not all output interfaces were created");
        return 2;
    }

    qInfo("QML interface loaded; requesting the session lock");
    if (!sessionLock.initialize()) {
        qCritical() << sessionLock.error();
        return 1;
    }

    if (!rootObject->property("lockSurfacesReady").toBool()) {
        qCritical("Desklock could not attach a lock surface to every output");
        return 3;
    }

    const int exitCode = application.exec();
    qInfo() << "Desklock exiting with status" << exitCode;
    return exitCode;
}
