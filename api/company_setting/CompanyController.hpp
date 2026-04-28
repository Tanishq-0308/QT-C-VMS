#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class CompanyController : public QObject {
    Q_OBJECT
public:
    explicit CompanyController(DatabaseManager* db, QObject *parent = nullptr);

    QJsonObject getCompanyById(int id);
    QJsonObject getFirstCompany();
    bool addCompany(const QJsonObject &data, QJsonObject &response);
    bool editCompany(int companyId, const QJsonObject &data, QJsonObject &response);

private:
    DatabaseManager* m_db;
};
