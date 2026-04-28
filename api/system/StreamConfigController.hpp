#pragma once
#include <QObject>
#include <QJsonObject>

class StreamConfigController : public QObject {
    Q_OBJECT
public:
    explicit StreamConfigController(QObject *parent = nullptr);

    QJsonObject updateUrl(const QString &url);
};