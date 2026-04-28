
#include "CommentImageController.hpp"

CommentImageController::CommentImageController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

QJsonArray CommentImageController::getCommentsByImageId(int imageId) {
    return m_db->selectQuery("SELECT * FROM comments_image WHERE image_id = ?", {imageId});
}

bool CommentImageController::addComment(int imageId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO comments_image (image_id, text) VALUES (?, ?)";
    QVariantList v = {imageId, data["text"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool CommentImageController::editComment(int commentId, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE comments_image SET text = ? WHERE id = ?";
    QVariantList v = {data["text"].toString(), commentId};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool CommentImageController::deleteComment(int commentId) {
    return m_db->executeQuery("DELETE FROM comments_image WHERE id = ?", {commentId});
}