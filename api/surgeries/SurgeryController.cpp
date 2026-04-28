#include "SurgeryController.hpp"

SurgeryController::SurgeryController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

bool SurgeryController::addSurgery(int patientId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO surgeries (patient_id, name, date) VALUES (?, ?, ?)";
    QVariantList v = {patientId, data["name"].toString(), data["date"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool SurgeryController::editSurgery(int id, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE surgeries SET name=?, date=? WHERE id=?";
    QVariantList v = {data["name"].toString(), data["date"].toString(), id};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

QJsonArray SurgeryController::getSurgeriesByPatientId(int patientId) {
    return m_db->selectQuery("SELECT * FROM surgeries WHERE patient_id = ?", {patientId});
}

QJsonObject SurgeryController::getSurgeryById(int id) {
    auto r = m_db->selectQuery("SELECT * FROM surgeries WHERE id = ?", {id});
    return r.isEmpty() ? QJsonObject() : r.first().toObject();
}
