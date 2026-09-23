#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class SurgeryController : public QObject {
    Q_OBJECT
public:
    explicit SurgeryController(DatabaseManager* db, QObject *parent = nullptr);

    bool addSurgery(const QString &patientId, const QJsonObject &data, QJsonObject &response);
    bool editSurgery(int id, const QJsonObject &data, QJsonObject &response);
    QJsonArray getSurgeriesByPatientId(const QString &patientId);   // patients.patient_id, e.g. "P1001"
    QJsonObject getSurgeryById(int id);

private:
    DatabaseManager* m_db;
};