// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux Latinoamericana S.C. <hello@nxos.org>

#include "ScreenRegistry.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QVariant>

ScreenRegistry::ScreenRegistry(QObject *parent)
    : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        qInfo() << "Output added:" << screen->name();
        emit screensChanged();
        emit screenAdded(screen);
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        qInfo() << "Output removed:" << screen->name();
        emit screenRemoved(screen);
        emit screensChanged();
    });
}

QVariantList ScreenRegistry::screens() const
{
    QVariantList result;
    for (QScreen *screen : QGuiApplication::screens()) {
        result.append(QVariant::fromValue(screen));
    }
    return result;
}
