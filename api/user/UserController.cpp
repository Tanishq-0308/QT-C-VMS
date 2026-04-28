#include "UserController.hpp"

UserController::UserController(DatabaseManager *dbManager, QObject *parent)
    : QObject(parent), m_dbManager(dbManager) {}

QJsonArray UserController::getAllUsers() {
    QString queryStr = "SELECT * FROM users";
    return m_dbManager->selectQuery(queryStr);
}

QJsonObject UserController::getUserById(int userId) {
    QString queryStr = "SELECT * FROM users WHERE id = ?";
    QJsonArray result = m_dbManager->selectQuery(queryStr, {userId});
    if (!result.isEmpty()) {
        return result.first().toObject();
    }
    return QJsonObject();
}

bool UserController::createUser(const QJsonObject &userData) {
    QString queryStr = "INSERT INTO users (name, email) VALUES (?, ?)";
    QVariantList bindValues = {userData["name"].toString(), userData["email"].toString()};
    return m_dbManager->executeQuery(queryStr, bindValues);
}

bool UserController::updateUser(int userId, const QJsonObject &userData) {
    QString queryStr = "UPDATE users SET name = ?, email = ? WHERE id = ?";
    QVariantList bindValues = {userData["name"].toString(), userData["email"].toString(), userId};
    return m_dbManager->executeQuery(queryStr, bindValues);
}

bool UserController::deleteUser(int userId) {
    QString queryStr = "DELETE FROM users WHERE id = ?";
    return m_dbManager->executeQuery(queryStr, {userId});
}
