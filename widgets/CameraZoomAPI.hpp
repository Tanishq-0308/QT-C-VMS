#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class CameraZoomAPI : public QObject {
    Q_OBJECT
public:
    explicit CameraZoomAPI(QObject* parent = nullptr);
    void moveCamera(const QString& direction, const QString& model);

signals:
    void zoomCompleted();
    void zoomFailed(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* networkManager;
};
