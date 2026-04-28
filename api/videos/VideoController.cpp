#include "VideoController.hpp"

VideoController::VideoController(DatabaseManager* db, QObject *parent) : QObject(parent), m_db(db) {}

bool VideoController::addVideo(int surgeryId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO videos (surgery_id, path, title) VALUES (?, ?, ?)";
    QVariantList v = {surgeryId, data["path"].toString(), data["title"].toString()};

    if (m_db->executeQuery(q, v)) {
        // Fetch the last inserted ID
        QJsonArray idResult = m_db->selectQuery("SELECT last_insert_rowid()");
        if (!idResult.isEmpty()) {
            int newId = idResult.first().toObject().value("last_insert_rowid()").toInt();
            response = data;
            response["id"] = newId;
            response["message"] = "Video added successfully";
        } else {
            response["error"] = "Failed to fetch inserted video ID";
        }
        return true;
    }

    response["error"] = "Video insertion failed";
    return false;
}


bool VideoController::editVideo(int id, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE videos SET path=?, title=? WHERE id=?";
    QVariantList v = {data["path"].toString(), data["title"].toString(), id};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

QJsonArray VideoController::getVideosBySurgeryId(int surgeryId) {
    return m_db->selectQuery("SELECT * FROM videos WHERE surgery_id = ?", {surgeryId});
}

QJsonObject VideoController::getVideoById(int id) {
    auto r = m_db->selectQuery("SELECT * FROM videos WHERE id = ?", {id});
    return r.isEmpty() ? QJsonObject() : r.first().toObject();
}

bool VideoController::deleteVideo(int id) {
    return m_db->executeQuery("DELETE FROM videos WHERE id = ?", {id});
}
