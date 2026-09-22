#include <QApplication>
#include "mainwindow.hpp"
#include "database/DatabaseManager.hpp"
#include <QFile>
#include <QTextStream>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QProcess>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QElapsedTimer>
#include "DeckLinkAPI.h"
#include "com_ptr.h"

#include <QStandardPaths>
#include <QDir>
#include <QScreen>

// QString getWritableDatabasePath() {
//     QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
//     QDir().mkpath(dataDir); // Make sure directory exists

//     QString dbPath = dataDir + "/sqlite.db";
//     QString bundledDb = QCoreApplication::applicationDirPath() + "/sqlite.db";

//     if (!QFile::exists(dbPath)) {
//         if (QFile::exists(bundledDb)) {
//             QFile::copy(bundledDb, dbPath);
//             QFile::setPermissions(dbPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
//             qInfo() << "✅ Copied bundled DB to:" << dbPath;
//         } else {
//             // 🔧 Create an empty database file directly using a temporary connection
//             QSqlDatabase tempDb = QSqlDatabase::addDatabase("QSQLITE", "temp_connection");
//             tempDb.setDatabaseName(dbPath);
//             if (tempDb.open()) {
//                 qInfo() << "✅ Created new empty DB at:" << dbPath;
//                 tempDb.close();
//             } else {
//                 qCritical() << "❌ Failed to create empty DB:" << tempDb.lastError().text();
//             }
//             QSqlDatabase::removeDatabase("temp_connection");
//         }
//     }

//     return dbPath;
// }




QString loadMergedStyleSheets(const QStringList &files) {
    QString merged;
    for (const QString &file : files) {
        QFile f(file);
        if (f.open(QFile::ReadOnly)) {
            merged += f.readAll() + "\n";
            f.close();
        } else {
            qWarning() << "⚠️ Could not open QSS file:" << file;
        }
    }
    return merged;
}

// Split a migration script into statements. A ';' ends a statement only when it
// is outside '...', "..." and `...` literals, outside -- and /* */ comments, and
// not inside a CREATE TRIGGER ... BEGIN ... END body. Comments are dropped.
static QStringList splitSqlStatements(const QString& sql) {
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

bool runMigrations(const QString& path) {
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

// ✅ Wait until Flask API is ready
bool waitForFlaskReady(int timeoutMs = 5000) {
    QNetworkAccessManager manager;
    QUrl url("http://127.0.0.1:8001/test-camera-connection");
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QNetworkRequest request(url);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            reply->deleteLater();
            return true;
        }
        reply->deleteLater();
        QThread::msleep(300);
    }
    return false;
}



int main(int argc, char *argv[]) {
    // 🔧 Initialize CUDA and CUVID context lock


    QApplication app(argc, argv);

    qRegisterMetaType<com_ptr<IDeckLinkVideoFrame>>("com_ptr<IDeckLinkVideoFrame>");
        QString dbPath = QCoreApplication::applicationDirPath() + "/../sqlite.db";
    DatabaseManager db;
    if (!db.openDatabase(dbPath)) {
        return -1;
    }


    // 🎨 Load style sheets
    QStringList qssFiles = {
        ":/assets/styles/dashboard.qss",
        ":/assets/styles/patient.qss",
        ":/assets/styles/surgerydetails.qss",
        ":/assets/styles/AddPatient.qss",
        ":/assets/styles/login.qss",
        ":/assets/styles/surgeryrecordingpage.qss"
        // Add more QSS files here if needed
    };
    app.setStyleSheet(loadMergedStyleSheets(qssFiles));

    // 🛠️ Run migration script before anything else
    QString sqlPath = QCoreApplication::applicationDirPath() + "/../database/migrations/init.sql";
    if (!runMigrations(sqlPath)) {
        return -1;
    }

    // 🚀 Start Flask API in background (detached)
    QString flaskDir = QCoreApplication::applicationDirPath() + "/../flask_zoom_api";
    QString flaskScript = flaskDir + "/app.py";
    bool success = QProcess::startDetached("python3", QStringList() << flaskScript, flaskDir);
    if (!success) {
        qCritical() << "❌ Failed to start Flask server in detached mode!";
        return -1;
    }

    // if (!waitForFlaskReady()) {
    //     qCritical() << "❌ Flask server did not respond to test connection!";
    //     return -1;
    // }

    qInfo() << "✅ Flask API started and ready!";

    MainWindow w;
    w.showFullScreen(); 
    // w.setFixedSize(1920,1080);
    // w.show();  // Optional: windowed mode for debugging
    // w.setWindowFlags(Qt::FramelessWindowHint);
    // QScreen *screen = QGuiApplication::primaryScreen();
    // qreal scale = screen->devicePixelRatio();

    //w.showFullScreen(); 
    // or
    // w.setFixedSize(1920,1080);
    // w.show();


    int exitCode = app.exec();
    

    return exitCode;
}
