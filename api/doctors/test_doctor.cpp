#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "DoctorController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const auto &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printDoctorList(const QJsonArray &array) {
    qDebug() << "📋 Doctor List:";
    for (const QJsonValue &value : array) {
        QJsonObject doc = value.toObject();
        qDebug() << "  👨‍⚕️ ID:" << doc["id"].toInt()
                 << "| Name:" << doc["name"].toString()
                 << "| Specialization:" << doc["specialization"].toString()
                 << "| Contact:" << doc["contact"].toString();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // 🔌 Connect to SQLite
    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    // 🧑‍⚕️ Init controller
    DoctorController doctorApi(&db);

    // 🔍 List doctors
    QJsonArray doctorList = doctorApi.getAllDoctors();
    printDoctorList(doctorList);

    // ➕ Add a doctor
    QJsonObject newDoctor;
    newDoctor["name"] = "Dr. Ada Lovelace";
    newDoctor["specialization"] = "Neurology";
    newDoctor["contact"] = "9999999999";

    QJsonObject addResponse;
    if (doctorApi.addDoctor(newDoctor, addResponse)) {
        printJsonObject("✅ Doctor added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add doctor:", addResponse);
    }

    // ✏️ Edit the doctor (assume ID = 1)
    QJsonObject editDoctor;
    editDoctor["name"] = "Dr. Ada Lovelace, MD";
    editDoctor["specialization"] = "Neurosurgery";
    editDoctor["contact"] = "9876543210";

    QJsonObject editResponse;
    if (doctorApi.editDoctor(1, editDoctor, editResponse)) {
        printJsonObject("✏️ Doctor edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit doctor:", editResponse);
    }

    return 0;
}
