#pragma once

#include <QObject>
#include "database/DatabaseManager.hpp"
#include "api/user/UserController.hpp"
// Include other controllers as needed

class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController();

    void initializeModules();

private:
    DatabaseManager *m_dbManager;
    UserController *m_userController;
    // Other controllers
};
