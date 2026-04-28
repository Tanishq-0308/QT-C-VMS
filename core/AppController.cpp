#include "core/AppController.hpp"
#include <QDebug>

AppController::AppController(QObject *parent) : QObject(parent) {
    m_dbManager = new DatabaseManager(this);  // QObject-based ownership

    if (!m_dbManager->openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open SQLite DB!";
    } else {
        qDebug() << "✅ Database opened successfully.";
    }

    // Initialize controllers
    initializeModules();
}

AppController::~AppController() {
    // Qt parent-child memory management handles deletion
}

void AppController::initializeModules() {
    m_userController = new UserController(m_dbManager, this);
    // Add others similarly:
    // m_patientController = new PatientController(m_dbManager, this);
    // m_doctorController = new DoctorController(m_dbManager, this);
}
