#include "PatientController.hpp"

PatientController::PatientController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

bool PatientController::addPatient(const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO patients (name, age, gender, contact) VALUES (?, ?, ?, ?)";
    QVariantList v = {data["name"].toString(), data["age"].toInt(), data["gender"].toString(), data["contact"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool PatientController::editPatient(int id, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE patients SET name=?, age=?, gender=?, contact=? WHERE id=?";
    QVariantList v = {data["name"].toString(), data["age"].toInt(), data["gender"].toString(), data["contact"].toString(), id};
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