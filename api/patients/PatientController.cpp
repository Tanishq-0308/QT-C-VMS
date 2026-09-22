#include "PatientController.hpp"

PatientController::PatientController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

// Keys match the patients columns in database/migrations/init.sql:
// first_name, last_name, patient_id (e.g. "P1001"), mobile, dob, gender, address.
bool PatientController::addPatient(const QJsonObject &data, QJsonObject &response) {
    if (data["first_name"].toString().isEmpty() || data["last_name"].toString().isEmpty() ||
        data["patient_id"].toString().isEmpty()) {
        response["error"] = "first_name, last_name and patient_id are required";
        return false;
    }
    QString q = "INSERT INTO patients (first_name, last_name, patient_id, mobile, dob, gender, address) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)";
    QVariantList v = {data["first_name"].toString(), data["last_name"].toString(), data["patient_id"].toString(),
                      data["mobile"].toString(), data["dob"].toString(), data["gender"].toString(),
                      data["address"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool PatientController::editPatient(int id, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE patients SET first_name=?, last_name=?, mobile=?, dob=?, gender=?, address=? WHERE id=?";
    QVariantList v = {data["first_name"].toString(), data["last_name"].toString(), data["mobile"].toString(),
                      data["dob"].toString(), data["gender"].toString(), data["address"].toString(), id};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

QJsonObject PatientController::getPatientById(int id) {
    auto result = m_db->selectQuery("SELECT * FROM patients WHERE id = ?", {id});
    return result.isEmpty() ? QJsonObject() : result.first().toObject();
}

QJsonArray PatientController::getPatientList() {
    return m_db->selectQuery("SELECT * FROM patients");
}
