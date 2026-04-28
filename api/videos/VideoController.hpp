#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class VideoController : public QObject {
    Q_OBJECT
public:
    explicit VideoController(DatabaseManager* db, QObject *parent = nullptr);

    bool addVideo(int surgeryId, const QJsonObject &data, QJsonObject &response);
    bool editVideo(int id, const QJsonObject &data, QJsonObject &response);
    QJsonArray getVideosBySurgeryId(int surgeryId);
    QJsonObject getVideoById(int id);
    bool deleteVideo(int id);

private:
    DatabaseManager* m_db;
};