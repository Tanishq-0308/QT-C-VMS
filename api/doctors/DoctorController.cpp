#include "DoctorController.hpp"

DoctorController::DoctorController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

DoctorController::~DoctorController() {
    // No dynamic memory to clean up
}

bool DoctorController::addDoctor(const QJsonObject &data, QJsonObject &response) {
    QString query = "INSERT INTO doctors (name, specialization, contact) VALUES (?, ?, ?)";
    QVariantList bindValues = {
        data["name"].toString(),
        data["specialization"].toString(),
        data["contact"].toString()
    };

    if (m_db->executeQuery(query, bindValues)) {
        // You can optionally fetch last insert ID (if implemented in your DB manager)
        response = data;
        response["message"] = "Doctor added successfully";
        return true;
    }

    response["error"] = "Failed to add doctor";
    return false;
}

bool DoctorController::editDoctor(int doctorId, const QJsonObject &data, QJsonObject &response) {
    QString query = "UPDATE doctors SET name = ?, specialization = ?, contact = ? WHERE id = ?";
    QVariantList bindValues = {
        data["name"].toString(),
        data["specialization"].toString(),
        data["contact"].toString(),
        QVariant(doctorId)
    };

    if (m_db->executeQuery(query, bindValues)) {
        response = data;
        response["id"] = doctorId;
        response["message"] = "Doctor updated successfully";
        return true;
    }

    response["error"] = "Failed to update doctor";
    return false;
}

QJsonArray DoctorController::getAllDoctors() {
    return m_db->selectQuery("SELECT * FROM doctors");
}
