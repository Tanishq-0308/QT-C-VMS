#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>

#include "CompanyController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const QString &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    CompanyController companyApi(&db);

    // ➕ Add company
    QJsonObject newCompany;
    newCompany["name"] = "Brainwave Medical Systems";
    newCompany["address"] = "123 Innovation Street";
    newCompany["phone"] = "0123456789";

    QJsonObject addResponse;
    if (companyApi.addCompany(newCompany, addResponse)) {
        addResponse["message"] = "Company added successfully";
        printJsonObject("✅ Company added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add company:", addResponse);
    }

    // ✏️ Edit company ID 1
    QJsonObject editCompany;
    editCompany["name"] = "Brainwave Health Pvt Ltd";
    editCompany["address"] = "456 Technology Avenue";
    editCompany["phone"] = "9876543210";

    QJsonObject editResponse;
    if (companyApi.editCompany(1, editCompany, editResponse)) {
        editResponse["id"] = 1;
        editResponse["message"] = "Company updated successfully";
        printJsonObject("✏️ Company edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit company:", editResponse);
    }

    // 🔍 Get company by ID
    QJsonObject companyById = companyApi.getCompanyById(1);
    if (!companyById.isEmpty()) {
        printJsonObject("🔍 Company with ID 1:", companyById);
    } else {
        qWarning() << "❌ Company with ID 1 not found.";
    }

    // 🔍 Get first company
    QJsonObject firstCompany = companyApi.getFirstCompany();
    if (!firstCompany.isEmpty()) {
        printJsonObject("🔍 First company record:", firstCompany);
    } else {
        qWarning() << "❌ No company records found.";
    }

    return 0;
}
