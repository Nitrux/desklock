#pragma once

#include <QObject>
#include <QScreen>
#include <QVariantList>

class ScreenRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

public:
    explicit ScreenRegistry(QObject *parent = nullptr);

    QVariantList screens() const;

signals:
    void screensChanged();
    void screenAdded(QScreen *screen);
    void screenRemoved(QScreen *screen);
};
