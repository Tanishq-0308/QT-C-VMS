#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

#include "ImageController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const auto &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printImageList(const QJsonArray &list) {
    qDebug() << "🖼️ Image List:";
    for (const QJsonValue &val : list) {
        QJsonObject img = val.toObject();
        qDebug() << "  🖼️ ID:" << img["id"].toInt()
                 << "| Title:" << img["title"].toString()
                 << "| Path:" << img["path"].toString()
                 << "| Surgery ID:" << img["surgery_id"].toInt();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    ImageController imageApi(&db);

    // ➕ Add image to surgery ID 1
    QJsonObject newImage;
    newImage["path"] = "/images/surgery1_image1.png";
    newImage["title"] = "Initial Scan";

    QJsonObject addResponse;
    if (imageApi.addImage(1, newImage, addResponse)) {
        addResponse["message"] = "Image added successfully";
        printJsonObject("✅ Image added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add image:", addResponse);
    }

    // ✏️ Edit image ID 1
    QJsonObject editImage;
    editImage["path"] = "/images/surgery1_image1_updated.png";
    editImage["title"] = "Updated Scan";

    QJsonObject editResponse;
    if (imageApi.editImage(1, editImage, editResponse)) {
        editResponse["id"] = 1;
        editResponse["message"] = "Image updated successfully";
        printJsonObject("✏️ Image edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit image:", editResponse);
    }

    // 📋 Get all images by surgery ID
    int surgeryId = 1;
    QJsonArray imageList = imageApi.getImagesBySurgeryId(surgeryId);
    if (!imageList.isEmpty()) {
        qDebug() << QString("🖼️ Images for Surgery ID %1:").arg(surgeryId);
        printImageList(imageList);
    } else {
        qWarning() << QString("❌ No images found for Surgery ID %1.").arg(surgeryId);
    }

    return 0;
}
