#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include "database/DatabaseManager.hpp"

class UserController : public QObject {
    Q_OBJECT
public:
    explicit UserController(DatabaseManager *dbManager, QObject *parent = nullptr);

    QJsonArray getAllUsers();
    QJsonObject getUserById(int userId);
    bool createUser(const QJsonObject &userData);
    bool updateUser(int userId, const QJsonObject &userData);
    bool deleteUser(int userId);

private:
    DatabaseManager *m_dbManager;
};
