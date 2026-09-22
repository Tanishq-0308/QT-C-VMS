#include "CameraZoomController.hpp"
#include <QThread>
#include <QDebug>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>


CameraZoomController::CameraZoomController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

QJsonObject CameraZoomController::getCameraSettings() {
    QJsonObject response;
    QJsonArray result = m_db->selectQuery("SELECT camera_ip, camera_type, user, password, video_streaming FROM company ORDER BY id ASC LIMIT 1");
    if (!result.isEmpty()) {
        QJsonObject row = result.first().toObject();
        response["camera_ip"] = row["camera_ip"];
        response["port"] = 80;
        response["username"] = row["user"];
        response["password"] = row["password"];
        response["model"] = row["camera_type"];
    } else {
        // The schema in database/migrations/init.sql has no camera columns
        // (camera_ip, camera_type, user, password, video_streaming) in `company`
        // or any other table, so this read fails until they are added.
        qWarning() << "CameraZoomController: camera settings unavailable (the schema has no "
                      "camera_ip/camera_type/user/password/video_streaming columns in company)";
    }
    return response;
}

QJsonObject CameraZoomController::moveCamera(const QString &direction, float speed) {
    QJsonObject result;
    QJsonObject settings = getCameraSettings();

    if (settings.isEmpty()) {
        result["error"] = "❌ Camera settings not found.";
        return result;
    }

    QString model = settings["model"].toString();
    QString ip = settings["camera_ip"].toString();
    QString username = settings["username"].toString();
    QString password = settings["password"].toString();

    try {
        if (model == "2MP") {
            // 🔁 Build zoom command URL
            QString act = (direction == "zoom_in") ? "zoomin" : "zoomout";
            QString url = QString("http://%1/web/cgi-bin/hi3510/ptzctrl.cgi?-step=0&-act=%2&-speed=%3")
                          .arg(ip, act).arg(speed);

            // 🌐 Send HTTP GET request with Basic Auth
            QNetworkAccessManager manager;
            QNetworkRequest request{QUrl(url)};

            QString credentials = QString("%1:%2").arg(username, password);
            QByteArray headerData = "Basic " + credentials.toUtf8().toBase64();
            request.setRawHeader("Authorization", headerData);

            manager.get(request);

            // ⏳ Wait briefly, then send stop command
            QThread::sleep(1);
            QString stopUrl = QString("http://%1/web/cgi-bin/hi3510/ptzctrl.cgi?-step=0&-act=stop&-speed=%2")
                              .arg(ip).arg(speed);

            QNetworkRequest stopRequest{QUrl(stopUrl)};
            stopRequest.setRawHeader("Authorization", headerData);
            manager.get(stopRequest);

            result["message"] = QString("✅ Camera moved %1 at speed %2").arg(direction).arg(speed);
        } else {
            result["warning"] = "⚠️ ONVIF (8MP) camera control not implemented in Qt.";
        }
    } catch (...) {
        result["error"] = "❌ Camera movement failed due to an unexpected error.";
    }

    return result;
}
