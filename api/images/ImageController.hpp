#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class ImageController : public QObject {
    Q_OBJECT
public:
    explicit ImageController(DatabaseManager* db, QObject *parent = nullptr);

    QJsonArray getImagesBySurgeryId(int surgeryId);
    bool addImage(int surgeryId, const QJsonObject &data, QJsonObject &response);
    bool editImage(int imageId, const QJsonObject &data, QJsonObject &response);

private:
    DatabaseManager* m_db;
};
