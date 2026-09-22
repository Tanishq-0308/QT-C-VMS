#include "SurgeryController.hpp"

SurgeryController::SurgeryController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

// patientId is the text patients.patient_id (e.g. "P1001"), which is what
// surgeries.patient_id stores. Data keys match the surgeries columns in init.sql.
bool SurgeryController::addSurgery(const QString &patientId, const QJsonObject &data, QJsonObject &response) {
    if (patientId.isEmpty() || data["surgeon_name"].toString().isEmpty() ||
        data["surgery_type"].toString().isEmpty() || data["surgery_date"].toString().isEmpty()) {
        response["error"] = "patient_id, surgeon_name, surgery_type and surgery_date are required";
        return false;
    }
    QString q = "INSERT INTO surgeries (patient_id, surgeon_name, surgery_type, body_part, additional_surgeon, "
                "anesthesiologist, surgery_date, operation_theatre_number, surgical_history) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    QVariantList v = {patientId, data["surgeon_name"].toString(), data["surgery_type"].toString(),
                      data["body_part"].toString(), data["additional_surgeon"].toString(),
                      data["anesthesiologist"].toString(), data["surgery_date"].toString(),
                      data["operation_theatre_number"].toString(), data["surgical_history"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool SurgeryController::editSurgery(int id, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE surgeries SET surgeon_name=?, surgery_type=?, body_part=?, additional_surgeon=?, "
                "anesthesiologist=?, surgery_date=?, operation_theatre_number=?, surgical_history=? WHERE id=?";
    QVariantList v = {data["surgeon_name"].toString(), data["surgery_type"].toString(),
                      data["body_part"].toString(), data["additional_surgeon"].toString(),
                      data["anesthesiologist"].toString(), data["surgery_date"].toString(),
                      data["operation_theatre_number"].toString(), data["surgical_history"].toString(), id};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

QJsonArray SurgeryController::getSurgeriesByPatientId(const QString &patientId) {
    return m_db->selectQuery("SELECT * FROM surgeries WHERE patient_id = ?", {patientId});
}

QJsonObject SurgeryController::getSurgeryById(int id) {
    auto r = m_db->selectQuery("SELECT * FROM surgeries WHERE id = ?", {id});
    return r.isEmpty() ? QJsonObject() : r.first().toObject();
}
