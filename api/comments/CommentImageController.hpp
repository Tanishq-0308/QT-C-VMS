#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantList>
#include "database/DatabaseManager.hpp"

class CommentImageController : public QObject {
    Q_OBJECT
public:
    explicit CommentImageController(DatabaseManager* db, QObject *parent = nullptr);

    QJsonArray getCommentsByImageId(int imageId);
    bool addComment(int imageId, const QJsonObject &data, QJsonObject &response);
    bool editComment(int commentId, const QJsonObject &data, QJsonObject &response);
    bool deleteComment(int commentId);

private:
    DatabaseManager* m_db;
};