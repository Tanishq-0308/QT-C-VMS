#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "PatientController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const auto &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printPatientList(const QJsonArray &array) {
    qDebug() << "📋 Patient List:";
    for (const QJsonValue &value : array) {
        QJsonObject p = value.toObject();
        qDebug() << "  🧑 ID:" << p["id"].toInt()
                 << "| Name:" << p["name"].toString()
                 << "| Age:" << p["age"].toInt()
                 << "| Gender:" << p["gender"].toString()
                 << "| Contact:" << p["contact"].toString();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // 🔌 Open SQLite DB
    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    PatientController patientApi(&db);

    // 📋 List existing patients
    QJsonArray patientList = patientApi.getPatientList();
    printPatientList(patientList);

    // ➕ Add patient
    QJsonObject newPatient;
    newPatient["name"] = "Alice Smith";
    newPatient["age"] = 30;
    newPatient["gender"] = "Female";
    newPatient["contact"] = "9998887777";

    QJsonObject addResponse;
    if (patientApi.addPatient(newPatient, addResponse)) {
        addResponse["message"] = "Patient added successfully";
        printJsonObject("✅ Patient added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add patient:", addResponse);
    }

    // ✏️ Edit patient (ID = 1 assumed)
    QJsonObject updatedPatient;
    updatedPatient["name"] = "Alice S.";
    updatedPatient["age"] = 31;
    updatedPatient["gender"] = "Female";
    updatedPatient["contact"] = "9998880000";

    QJsonObject editResponse;
    if (patientApi.editPatient(1, updatedPatient, editResponse)) {
        editResponse["id"] = 1;
        editResponse["message"] = "Patient updated successfully";
        printJsonObject("✏️ Patient edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit patient:", editResponse);
    }

    // 🔍 Get patient by ID
    QJsonObject patientById = patientApi.getPatientById(1);
    if (!patientById.isEmpty()) {
        printJsonObject("🔍 Patient by ID (1):", patientById);
    } else {
        qWarning() << "❌ Patient with ID 1 not found.";
    }

    return 0;
}
