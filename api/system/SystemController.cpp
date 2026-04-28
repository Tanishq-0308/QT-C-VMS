#include "SystemController.hpp"
#include <QProcess>

SystemController::SystemController(QObject *parent) : QObject(parent) {}

QJsonObject SystemController::shutdown(const QString &auth_key) {
    QJsonObject response;
    if (auth_key != "12345678") {
        response["error"] = "Unauthorized access";
        return response;
    }
    QProcess::execute("sudo", {"/sbin/shutdown", "-h", "now"});
    response["message"] = "Shutdown initiated successfully.";
    return response;
}

QJsonObject SystemController::restart(const QString &auth_key) {
    QJsonObject response;
    if (auth_key != "12345678") {
        response["error"] = "Unauthorized access";
        return response;
    }
    QProcess::execute("sudo", {"/sbin/reboot"});
    response["message"] = "System restart initiated successfully.";
    return response;
}