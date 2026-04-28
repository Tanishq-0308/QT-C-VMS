#include "DatabaseManager.hpp"
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {
    m_database = QSqlDatabase::addDatabase("QSQLITE");
}

DatabaseManager::~DatabaseManager() {
    closeDatabase();
}

bool DatabaseManager::openDatabase(const QString &dbPath) {
    m_database.setDatabaseName(dbPath);
    if (!m_database.open()) {
        qDebug() << "Failed to open database:" << m_database.lastError().text();
        return false;
    }
    return true;
}

void DatabaseManager::closeDatabase() {
    if (m_database.isOpen()) {
        m_database.close();
    }
}

bool DatabaseManager::executeQuery(const QString &queryStr, const QVariantList &bindValues) {
    QSqlQuery query(m_database);
    query.prepare(queryStr);
    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues.at(i));
    }
    if (!query.exec()) {
        qDebug() << "Query execution failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QJsonArray DatabaseManager::selectQuery(const QString &queryStr, const QVariantList &bindValues) {
    QJsonArray resultArray;
    QSqlQuery query(m_database);
    query.prepare(queryStr);
    for (int i = 0; i < bindValues.size(); ++i) {
        query.bindValue(i, bindValues.at(i));
    }
    if (query.exec()) {
        while (query.next()) {
            QJsonObject recordObject;
            QSqlRecord record = query.record();
            for (int i = 0; i < record.count(); ++i) {
                recordObject.insert(record.fieldName(i), QJsonValue::fromVariant(record.value(i)));
            }
            resultArray.append(recordObject);
        }
    } else {
        qDebug() << "Select query failed:" << query.lastError().text();
    }
    return resultArray;
}
