#include "CommentVideoController.hpp"

CommentVideoController::CommentVideoController(DatabaseManager* db, QObject *parent)
    : QObject(parent), m_db(db) {}

QJsonArray CommentVideoController::getCommentsByVideoId(int videoId) {
    return m_db->selectQuery("SELECT * FROM comments_video WHERE video_id = ?", {videoId});
}

bool CommentVideoController::addComment(int videoId, const QJsonObject &data, QJsonObject &response) {
    QString q = "INSERT INTO comments_video (video_id, text) VALUES (?, ?)";
    QVariantList v = {videoId, data["text"].toString()};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool CommentVideoController::editComment(int commentId, const QJsonObject &data, QJsonObject &response) {
    QString q = "UPDATE comments_video SET text = ? WHERE id = ?";
    QVariantList v = {data["text"].toString(), commentId};
    if (m_db->executeQuery(q, v)) { response = data; return true; }
    return false;
}

bool CommentVideoController::deleteComment(int commentId) {
    return m_db->executeQuery("DELETE FROM comments_video WHERE id = ?", {commentId});
}
