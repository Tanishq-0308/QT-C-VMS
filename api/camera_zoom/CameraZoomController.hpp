#pragma once
#include <QObject>
#include <QJsonObject>
#include "database/DatabaseManager.hpp"

class CameraZoomController : public QObject {
    Q_OBJECT
public:
    explicit CameraZoomController(DatabaseManager* db, QObject *parent = nullptr);

    QJsonObject getCameraSettings();
    QJsonObject moveCamera(const QString &direction, float speed);

private:
    DatabaseManager* m_db;
};