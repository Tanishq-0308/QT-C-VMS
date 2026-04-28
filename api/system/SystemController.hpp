#pragma once
#include <QObject>
#include <QJsonObject>

class SystemController : public QObject {
    Q_OBJECT
public:
    explicit SystemController(QObject *parent = nullptr);

    QJsonObject shutdown(const QString &auth_key);
    QJsonObject restart(const QString &auth_key);
};
