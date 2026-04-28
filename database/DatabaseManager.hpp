#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    bool openDatabase(const QString &dbPath);
    void closeDatabase();

    bool executeQuery(const QString &queryStr, const QVariantList &bindValues = {});
    QJsonArray selectQuery(const QString &queryStr, const QVariantList &bindValues = {});

private:
    QSqlDatabase m_database;
};
