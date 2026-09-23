// Shared helpers for the unit tests. Test-only code; never touches the real
// sqlite.db except to *copy* it (read-only open of the source file).
#pragma once

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMutex>
#include <unistd.h>

#ifndef APP_SOURCE_DIR
#error "APP_SOURCE_DIR must be defined by the build"
#endif

namespace ts {

inline QString appDir() { return QStringLiteral(APP_SOURCE_DIR); }
inline QString realDbPath() { return appDir() + "/sqlite.db"; }
inline QString initSqlPath() { return appDir() + "/database/migrations/init.sql"; }

// Copy the real sqlite.db into dir (QFile::copy opens the source read-only).
inline QString copyRealDb(const QTemporaryDir& dir, const QString& name = "sqlite_copy.db")
{
    const QString dst = dir.filePath(name);
    QFile::remove(dst);
    if (!QFile::copy(realDbPath(), dst))
        return QString();
    QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return dst;
}

// ---------------------------------------------------------------------------
// VERBATIM replica of splitSqlStatements() + runMigrations() from /main.cpp
// (medical_qt_app/main.cpp, functions `static QStringList splitSqlStatements(
// const QString& sql)` and `bool runMigrations(const QString& path)`, located
// right after loadMergedStyleSheets()). Copied unchanged except `static` ->
// `inline` so the test exercises identical logic: split on ';' outside string
// literals, comments and CREATE TRIGGER ... BEGIN ... END bodies, exec each
// statement on the DEFAULT connection, stop at first error.
// ---------------------------------------------------------------------------
// Split a migration script into statements. A ';' ends a statement only when it
// is outside '...', "..." and `...` literals, outside -- and /* */ comments, and
// not inside a CREATE TRIGGER ... BEGIN ... END body. Comments are dropped.
inline QStringList splitSqlStatements(const QString& sql) {
    QStringList statements;
    QString current;
    QString word;
    QStringList leadingWords;   // first words of the current statement
    bool inTrigger = false;
    int blockDepth = 0;         // BEGIN/CASE ... END nesting inside a trigger

    auto endWord = [&]() {
        if (word.isEmpty()) return;
        const QString w = word.toUpper();
        if (leadingWords.size() < 4) {   // CREATE [TEMP|TEMPORARY] TRIGGER
            leadingWords << w;
            if (leadingWords.first() == "CREATE" && w == "TRIGGER") inTrigger = true;
        }
        if (inTrigger) {
            if (w == "BEGIN" || w == "CASE") ++blockDepth;
            else if (w == "END" && blockDepth > 0) --blockDepth;
        }
        word.clear();
    };

    const int n = sql.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = sql.at(i);
        const QChar next = (i + 1 < n) ? sql.at(i + 1) : QChar();

        if (c.isLetterOrNumber() || c == '_') {
            word += c;
            current += c;
            continue;
        }
        endWord();

        if (c == '\'' || c == '"' || c == '`') {
            // Copy the literal verbatim; a doubled quote is an escaped quote.
            int j = i + 1;
            while (j < n) {
                if (sql.at(j) == c) {
                    if (j + 1 < n && sql.at(j + 1) == c) { j += 2; continue; }
                    break;
                }
                ++j;
            }
            current += sql.mid(i, j - i + 1);
            i = j;
            continue;
        }
        if (c == '-' && next == '-') {
            while (i < n && sql.at(i) != '\n') ++i;
            current += '\n';
            continue;
        }
        if (c == '/' && next == '*') {
            const int close = sql.indexOf("*/", i + 2);
            i = (close < 0) ? n : close + 1;
            current += ' ';
            continue;
        }
        if (c == ';' && blockDepth == 0) {
            const QString stmt = current.trimmed();
            if (!stmt.isEmpty()) statements << stmt;
            current.clear();
            leadingWords.clear();
            inTrigger = false;
            continue;
        }
        current += c;
    }
    endWord();
    const QString stmt = current.trimmed();
    if (!stmt.isEmpty()) statements << stmt;
    return statements;
}

inline bool runMigrations(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "❌ Cannot open migration file:" << path;
        return false;
    }

    QTextStream in(&file);
    QString sql = in.readAll();
    file.close();

    QSqlQuery query;
    for (const QString& stmt : splitSqlStatements(sql)) {
        if (!query.exec(stmt)) {
            qCritical() << "❌ SQL Error:" << query.lastError().text();
            return false;
        }
    }
    return true;
}
// --------------------------- end verbatim copy -----------------------------

inline int countRows(const QString& table, const QString& conn = QLatin1String(QSqlDatabase::defaultConnection))
{
    QSqlQuery q(QSqlDatabase::database(conn));
    if (!q.exec(QString("SELECT COUNT(*) FROM %1").arg(table)) || !q.next())
        return -1;
    return q.value(0).toInt();
}

// RSS in KiB from /proc/self/statm (field 2 = resident pages).
inline long rssKiB()
{
    QFile f("/proc/self/statm");
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QList<QByteArray> parts = f.readAll().simplified().split(' ');
    if (parts.size() < 2) return -1;
    return parts.at(1).toLong() * (sysconf(_SC_PAGESIZE) / 1024);
}

// Captures qDebug/qWarning/qCritical output so tests can assert that
// production code did not log a SQL failure (DatabaseManager swallows errors
// and only prints them).
struct MessageCapture {
    static QStringList& buffer() { static QStringList b; return b; }
    static QtMessageHandler& previous() { static QtMessageHandler p = nullptr; return p; }
    static void handler(QtMsgType t, const QMessageLogContext& c, const QString& m) {
        buffer().append(m);
        if (previous()) previous()(t, c, m);
    }
    MessageCapture() { buffer().clear(); previous() = qInstallMessageHandler(&MessageCapture::handler); }
    ~MessageCapture() { qInstallMessageHandler(previous()); }
    QStringList take() { QStringList r = buffer(); buffer().clear(); return r; }
    static QStringList failures(const QStringList& l) {
        QStringList out;
        for (const QString& s : l)
            if (s.contains("failed", Qt::CaseInsensitive) || s.contains("error", Qt::CaseInsensitive))
                out << s;
        return out;
    }
};

} // namespace ts
