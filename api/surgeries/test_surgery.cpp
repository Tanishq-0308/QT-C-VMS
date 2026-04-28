#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

#include "SurgeryController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const auto &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printSurgeryList(const QJsonArray &list) {
    qDebug() << "📋 Surgery List:";
    for (const QJsonValue &val : list) {
        QJsonObject s = val.toObject();
        qDebug() << "  🏥 ID:" << s["id"].toInt()
                 << "| Name:" << s["name"].toString()
                 << "| Date:" << s["date"].toString()
                 << "| Patient ID:" << s["patient_id"].toInt();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    SurgeryController surgeryApi(&db);

    // 🏥 Add surgery to patient ID 1
    QJsonObject newSurgery;
    newSurgery["name"] = "Appendectomy";
    newSurgery["date"] = "2025-05-23";

    QJsonObject addResponse;
    if (surgeryApi.addSurgery(1, newSurgery, addResponse)) {
        addResponse["message"] = "Surgery added successfully";
        printJsonObject("✅ Surgery added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add surgery:", addResponse);
    }

    // 📝 Edit surgery ID 1
    QJsonObject editData;
    editData["name"] = "Appendectomy (Revised)";
    editData["date"] = "2025-05-24";

    QJsonObject editResponse;
    if (surgeryApi.editSurgery(1, editData, editResponse)) {
        editResponse["id"] = 1;
        editResponse["message"] = "Surgery updated successfully";
        printJsonObject("✏️ Surgery edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit surgery:", editResponse);
    }

    // 🔍 Get all surgeries for a given patient
    int patientId = 1;
    QJsonArray surgeryList = surgeryApi.getSurgeriesByPatientId(patientId);
    if (!surgeryList.isEmpty()) {
        qDebug() << QString("📋 Surgeries for Patient ID %1:").arg(patientId);
        printSurgeryList(surgeryList);
    } else {
        qWarning() << QString("❌ No surgeries found for Patient ID %1.").arg(patientId);
    }

    // 🔍 Get surgery by specific surgery ID
    int surgeryId = 1;
    QJsonObject surgeryById = surgeryApi.getSurgeryById(surgeryId);
    if (!surgeryById.isEmpty()) {
        QString title = QString("🔍 Surgery found with ID %1:").arg(surgeryId);
        printJsonObject(title, surgeryById);
    } else {
        qWarning() << QString("❌ No surgery found with ID %1.").arg(surgeryId);
    }


    return 0;
}
