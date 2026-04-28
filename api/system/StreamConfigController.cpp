
#include "StreamConfigController.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <QProcess>

StreamConfigController::StreamConfigController(QObject *parent) : QObject(parent) {}

QJsonObject StreamConfigController::updateUrl(const QString &url) {
    QJsonObject response;
    if (url.isEmpty()) {
        response["error"] = "URL is required";
        return response;
    }

    QString configFilePath = "/home/brainwave/app/stream/config.json";
    QFile file(configFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        response["error"] = "Configuration file not found";
        return response;
    }

    QByteArray content = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content, &err);
    if (err.error != QJsonParseError::NoError) {
        response["error"] = "Invalid JSON format";
        return response;
    }

    QJsonObject obj = doc.object();
    obj["streams"].toObject()["vms"].toObject()["url"] = url;
    QFile writeFile(configFilePath);
    if (!writeFile.open(QIODevice::WriteOnly)) {
        response["error"] = "Failed to write config file";
        return response;
    }

    writeFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    writeFile.close();

    QProcess::startDetached("sh", {"-c", "echo 1234 | sudo -S supervisorctl restart go-server"});

    response["message"] = "URL updated and service restart initiated";
    return response;
}
