#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "../../database/DatabaseManager.hpp"

class PatientController : public QObject {
    Q_OBJECT
public:
    explicit PatientController(DatabaseManager* db, QObject *parent = nullptr);

    bool addPatient(const QJsonObject &data, QJsonObject &response);
    bool editPatient(int id, const QJsonObject &data, QJsonObject &response);
    QJsonObject getPatientById(int id);
    QJsonArray getPatientList();

private:
    DatabaseManager* m_db;
};
