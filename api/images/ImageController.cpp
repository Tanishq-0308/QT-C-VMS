#include "ImageController.hpp"

ImageController::ImageController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

// Images are stored in the `snapshots` table (patient_id, surgery_id, file_path, title).
// Data keys: "path" -> file_path, "title", optional "patient_id".
QJsonArray ImageController::getImagesBySurgeryId(int surgeryId) {
    return m_db->selectQuery("SELECT * FROM snapshots WHERE surgery_id = ?", {surgeryId});
}

bool ImageController::addImage(int surgeryId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO snapshots (patient_id, surgery_id, file_path, title) VALUES (?, ?, ?, ?)";
    QVariantList v = {data["patient_id"].toVariant(), surgeryId, data["path"].toString(), data["title"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool ImageController::editImage(int imageId, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE snapshots SET file_path = ?, title = ? WHERE id = ?";
    QVariantList v = {data["path"].toString(), data["title"].toString(), imageId};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}
