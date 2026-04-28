#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include "database/DatabaseManager.hpp"

class DoctorController : public QObject {
    Q_OBJECT  // ✅ MUST be present!
public:
    explicit DoctorController(DatabaseManager* db, QObject* parent = nullptr);
    ~DoctorController();

    QJsonArray getAllDoctors();
    bool addDoctor(const QJsonObject &data, QJsonObject &response);
    bool editDoctor(int doctorId, const QJsonObject &data, QJsonObject &response);

private:
    DatabaseManager* m_db;
};
