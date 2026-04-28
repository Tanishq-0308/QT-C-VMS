#include "CameraZoomAPI.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QDebug>

CameraZoomAPI::CameraZoomAPI(QObject* parent)
    : QObject(parent), networkManager(new QNetworkAccessManager(this)) {
    connect(networkManager, &QNetworkAccessManager::finished,
            this, &CameraZoomAPI::onReplyFinished);
}

void CameraZoomAPI::moveCamera(const QString& direction, const QString& model) {
    // ✅ Define Flask API endpoint
    QUrl url("http://127.0.0.1:8001/move-camera");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // ✅ Create JSON payload
    QJsonObject json;
    json["direction"] = direction;
    json["model"] = model;

    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
    // qDebug() << "📡 Sending Zoom Request:" << payload;

    networkManager->post(request, payload);
}

void CameraZoomAPI::onReplyFinished(QNetworkReply* reply) {
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        qDebug() << "✅ Zoom Success (" << statusCode << "):" << QString(response);
        emit zoomCompleted();
    } else {
        qWarning() << "❌ Zoom Failed (" << statusCode << "):" << reply->errorString();
        emit zoomFailed(reply->errorString());
    }

    reply->deleteLater();
}
