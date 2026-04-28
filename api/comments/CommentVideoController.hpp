#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class CommentVideoController : public QObject {
    Q_OBJECT
public:
    explicit CommentVideoController(DatabaseManager* db, QObject *parent = nullptr);

    QJsonArray getCommentsByVideoId(int videoId);
    bool addComment(int videoId, const QJsonObject &data, QJsonObject &response);
    bool editComment(int commentId, const QJsonObject &data, QJsonObject &response);
    bool deleteComment(int commentId);

private:
    DatabaseManager* m_db;
};
