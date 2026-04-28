#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "VideoController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const QString &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant();
    }
}

void printVideoList(const QJsonArray &list) {
    qDebug() << "📼 Video List:";
    for (const QJsonValue &val : list) {
        QJsonObject v = val.toObject();
        qDebug() << QString("  📼 ID: %1 | Surgery ID: %2 | Title: %3 | Path: %4")
                        .arg(v["id"].toInt())
                        .arg(v["surgery_id"].toInt())
                        .arg(v["title"].toString())
                        .arg(v["path"].toString());
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    VideoController videoApi(&db);

    // ➕ Add a video
    QJsonObject newVideo;
    newVideo["path"] = "/videos/appendectomy.mp4";
    newVideo["title"] = "Surgery Recording - Appendectomy";

    QJsonObject addResponse;
    if (videoApi.addVideo(1, newVideo, addResponse)) {
        printJsonObject("✅ Video added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add video:", addResponse);
        return -1;
    }

    // Extract newly inserted ID
    int newVideoId = addResponse["id"].toInt();

    // ✏️ Edit the video
    QJsonObject editVideo;
    editVideo["path"] = "/videos/appendectomy_revised.mp4";
    editVideo["title"] = "Appendectomy (Revised)";

    QJsonObject editResponse;
    if (videoApi.editVideo(newVideoId, editVideo, editResponse)) {
        editResponse["id"] = newVideoId;
        editResponse["message"] = "Video updated successfully";
        printJsonObject("✏️ Video edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit video:", editResponse);
    }

    // 📋 Get all videos for surgery ID 1
    QJsonArray videoList = videoApi.getVideosBySurgeryId(1);
    if (!videoList.isEmpty()) {
        printVideoList(videoList);
    } else {
        qWarning() << "❌ No videos found for surgery ID 1.";
    }

    // 🔍 Get video by ID
    QJsonObject videoById = videoApi.getVideoById(newVideoId);
    if (!videoById.isEmpty()) {
        printJsonObject(QString("🔍 Video found with ID %1:").arg(newVideoId), videoById);
    } else {
        qWarning() << QString("❌ Video with ID %1 not found.").arg(newVideoId);
    }

    // 🗑️ Delete the video
    if (videoApi.deleteVideo(newVideoId)) {
        qDebug() << QString("🗑️ Video ID %1 deleted successfully.").arg(newVideoId);
    } else {
        qWarning() << QString("❌ Failed to delete video ID %1.").arg(newVideoId);
    }

    return 0;
}
