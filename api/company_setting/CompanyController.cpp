#include "CompanyController.hpp"

CompanyController::CompanyController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

QJsonObject CompanyController::getCompanyById(int id) {
    auto r = m_db->selectQuery("SELECT * FROM company WHERE id = ?", {id});
    return r.isEmpty() ? QJsonObject() : r.first().toObject();
}

QJsonObject CompanyController::getFirstCompany() {
    auto r = m_db->selectQuery("SELECT * FROM company ORDER BY id ASC LIMIT 1");
    return r.isEmpty() ? QJsonObject() : r.first().toObject();
}

bool CompanyController::addCompany(const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO company (name, address, phone) VALUES (?, ?, ?)";
    QVariantList v = {
        data["name"].toString(),
        data["address"].toString(),
        data["phone"].toString()
    };
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool CompanyController::editCompany(int companyId, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE company SET name = ?, address = ?, phone = ? WHERE id = ?";
    QVariantList v = {
        data["name"].toString(),
        data["address"].toString(),
        data["phone"].toString(),
        companyId
    };
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}