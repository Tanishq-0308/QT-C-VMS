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
    for (const QString& stmt : sql.split(';', Qt::SkipEmptyParts)) {
        QString trimmed = stmt.trimmed();
        if (!trimmed.isEmpty()) {
            if (!query.exec(trimmed)) {
                qCritical() << "❌ SQL Error:" << query.lastError().text();
                return false;
            }
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
