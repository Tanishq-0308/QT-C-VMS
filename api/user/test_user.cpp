#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "UserController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const auto &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printUserList(const QJsonArray &list) {
    qDebug() << "👤 User List:";
    for (const QJsonValue &val : list) {
        QJsonObject u = val.toObject();
        qDebug() << "  👤 ID:" << u["id"].toInt()
                 << "| Name:" << u["name"].toString()
                 << "| Email:" << u["email"].toString();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    UserController userApi(&db);

    // ➕ Create user
    QJsonObject newUser;
    newUser["name"] = "Alice Anderson";
    newUser["email"] = "alice@example.com";

    if (userApi.createUser(newUser)) {
        qDebug() << "✅ User created successfully.";
    } else {
        qWarning() << "❌ Failed to create user.";
    }

    // ✏️ Update user ID 1
    QJsonObject updatedUser;
    updatedUser["name"] = "Alice A.";
    updatedUser["email"] = "alice.a@example.com";

    if (userApi.updateUser(1, updatedUser)) {
        qDebug() << "✏️ User updated successfully.";
    } else {
        qWarning() << "❌ Failed to update user.";
    }

    // 🔍 Get user by ID
    QJsonObject userById = userApi.getUserById(1);
    if (!userById.isEmpty()) {
        printJsonObject("🔍 User by ID (1):", userById);
    } else {
        qWarning() << "❌ User with ID 1 not found.";
    }

    // 📋 Get all users
    QJsonArray users = userApi.getAllUsers();
    if (!users.isEmpty()) {
        printUserList(users);
    } else {
        qWarning() << "❌ No users found.";
    }

    // 🗑️ Delete user ID 1
    if (userApi.deleteUser(1)) {
        qDebug() << "🗑️ User ID 1 deleted successfully.";
    } else {
        qWarning() << "❌ Failed to delete user ID 1.";
    }

    return 0;
}
