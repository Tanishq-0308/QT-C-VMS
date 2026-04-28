#include "ImageController.hpp"

ImageController::ImageController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

QJsonArray ImageController::getImagesBySurgeryId(int surgeryId) {
    return m_db->selectQuery("SELECT * FROM images WHERE surgery_id = ?", {surgeryId});
}

bool ImageController::addImage(int surgeryId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO images (surgery_id, path, title) VALUES (?, ?, ?)";
    QVariantList v = {surgeryId, data["path"].toString(), data["title"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool ImageController::editImage(int imageId, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE images SET path = ?, title = ? WHERE id = ?";
    QVariantList v = {data["path"].toString(), data["title"].toString(), imageId};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}
