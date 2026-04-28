#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>

#include "CommentImageController.hpp"
#include "database/DatabaseManager.hpp"

void printJsonObject(const QString &title, const QJsonObject &obj) {
    qDebug() << title;
    for (const QString &key : obj.keys()) {
        qDebug() << "  -" << key << ":" << obj.value(key).toVariant().toString();
    }
}

void printCommentList(const QJsonArray &list) {
    qDebug() << "💬 Comment List:";
    for (const QJsonValue &val : list) {
        QJsonObject c = val.toObject();
        qDebug() << "  💬 ID:" << c["id"].toInt()
                 << "| Image ID:" << c["image_id"].toInt()
                 << "| Text:" << c["text"].toString();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    DatabaseManager db;
    if (!db.openDatabase("sqlite.db")) {
        qCritical() << "❌ Failed to open database.";
        return -1;
    }

    CommentImageController commentApi(&db);

    // ➕ Add comment to image ID 1
    QJsonObject newComment;
    newComment["text"] = "This is a sample comment.";

    QJsonObject addResponse;
    if (commentApi.addComment(1, newComment, addResponse)) {
        addResponse["message"] = "Comment added successfully";
        printJsonObject("✅ Comment added:", addResponse);
    } else {
        printJsonObject("❌ Failed to add comment:", addResponse);
    }

    // ✏️ Edit comment ID 1
    QJsonObject editComment;
    editComment["text"] = "This comment has been edited.";

    QJsonObject editResponse;
    if (commentApi.editComment(1, editComment, editResponse)) {
        editResponse["id"] = 1;
        editResponse["message"] = "Comment updated successfully";
        printJsonObject("✏️ Comment edited:", editResponse);
    } else {
        printJsonObject("❌ Failed to edit comment:", editResponse);
    }

    // 📋 Get all comments for image ID 1
    QJsonArray commentList = commentApi.getCommentsByImageId(1);
    if (!commentList.isEmpty()) {
        printCommentList(commentList);
    } else {
        qWarning() << "❌ No comments found for image ID 1.";
    }

    // ❌ Delete comment ID 1
    if (commentApi.deleteComment(1)) {
        qDebug() << "🗑️ Comment ID 1 deleted successfully.";
    } else {
        qWarning() << "❌ Failed to delete comment ID 1.";
    }

    return 0;
}
