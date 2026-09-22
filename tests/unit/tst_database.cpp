// tst_database: migrations (runMigrations replica from main.cpp), the
// `settings` singleton row, SettingsPage save SQL, Qt-vs-Flask settings read,
// and every api/* controller's SQL against the real schema.
//
// All DB work happens on files inside a QTemporaryDir: either a fresh DB built
// from database/migrations/init.sql or a *copy* of the real sqlite.db.

#include <QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>

#include "common/TestSupport.hpp"
#include "common/ModalCloser.hpp"

#include "database/DatabaseManager.hpp"
#include "api/doctors/DoctorController.hpp"
#include "api/patients/PatientController.hpp"
#include "api/surgeries/SurgeryController.hpp"
#include "api/images/ImageController.hpp"
#include "api/videos/VideoController.hpp"
#include "api/comments/CommentImageController.hpp"
#include "api/comments/CommentVideoController.hpp"
#include "api/company_setting/CompanyController.hpp"
#include "api/user/UserController.hpp"
#include "api/camera_zoom/CameraZoomController.hpp"
#include "ui/Settings/SettingsPage.hpp"

class TstDatabase : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir* m_dir = nullptr;
    DatabaseManager* m_db = nullptr;

    // Open `path` as the default connection through the production DatabaseManager.
    bool openDb(const QString& path)
    {
        closeDb();
        m_db = new DatabaseManager;
        return m_db->openDatabase(path);
    }
    void closeDb()
    {
        delete m_db;
        m_db = nullptr;
        QSqlDatabase::removeDatabase(QLatin1String(QSqlDatabase::defaultConnection));
    }
    QString freshDbPath(const QString& name) { return m_dir->filePath(name); }

    // Result bookkeeping for controller checks.
    struct Checks {
        QStringList failures;
        ts::MessageCapture* cap;
        void check(const QString& what, bool ok) {
            const QStringList errs = ts::MessageCapture::failures(cap->take());
            if (!ok || !errs.isEmpty())
                failures << QString("%1 -> %2%3").arg(what, ok ? "returned OK" : "FAILED",
                                                      errs.isEmpty() ? QString() : " | " + errs.join(" / "));
        }
    };

private slots:
    void initTestCase()
    {
        QVERIFY(QSqlDatabase::isDriverAvailable("QSQLITE"));
        QVERIFY2(QFile::exists(ts::initSqlPath()), qPrintable(ts::initSqlPath()));
    }
    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
    }
    void cleanup()
    {
        closeDb();
        delete m_dir;
        m_dir = nullptr;
    }

    // ------------------------------------------------------------------ 1
    void migrations_tenLaunches_seedRowsStaySingleton_data()
    {
        QTest::addColumn<QString>("table");
        for (const char* t : {"doctors", "patients", "surgeries", "company", "comments_image",
                              "comments_video", "users", "settings"})
            QTest::newRow(t) << QString(t);
    }
    void migrations_tenLaunches_seedRowsStaySingleton()
    {
        QFETCH(QString, table);
        QVERIFY(openDb(freshDbPath("fresh.db")));
        for (int launch = 1; launch <= 10; ++launch)
            QVERIFY2(ts::runMigrations(ts::initSqlPath()), qPrintable(QString("launch %1").arg(launch)));
        const int n = ts::countRows(table);
        QVERIFY2(n == 1, qPrintable(QString("table '%1' has %2 rows after 10 simulated launches (expected 1)")
                                    .arg(table).arg(n)));
    }

    // ------------------------------------------------------------------ 2
    void realDbCopy_settingsRowCount()
    {
        const QString copy = ts::copyRealDb(*m_dir);
        QVERIFY2(!copy.isEmpty(), "could not copy sqlite.db");
        QVERIFY(openDb(copy));
        const int before = ts::countRows("settings");
        // The row the app showed before the fix (SELECT ... LIMIT 1 -> lowest id).
        QSqlQuery shown;
        QVERIFY(shown.exec("SELECT * FROM settings ORDER BY id LIMIT 1") && shown.next());
        const QSqlRecord shownRec = shown.record();
        shown.finish();
        QVERIFY(ts::runMigrations(ts::initSqlPath()));   // one more launch, on the COPY
        const int after = ts::countRows("settings");
        qInfo("real sqlite.db (copy): settings rows = %d; after one more simulated launch = %d", before, after);
        // `before` reflects the DB file on disk, which only changes when the
        // (fixed) migration runs, so only the post-launch count is asserted.
        QVERIFY2(after == 1,
                 qPrintable(QString("settings rows in copy of real sqlite.db = %1, after one more launch = %2 "
                                    "(expected 1 after launch)").arg(before).arg(after)));
        QSqlQuery kept;
        QVERIFY(kept.exec("SELECT * FROM settings") && kept.next());
        for (int i = 0; i < shownRec.count(); ++i)
            QVERIFY2(kept.value(i) == shownRec.value(i),
                     qPrintable(QString("surviving settings row differs from the row shown before (lowest id) in "
                                        "column %1: '%2' vs '%3'").arg(shownRec.fieldName(i),
                                        kept.value(i).toString(), shownRec.value(i).toString())));
    }

    // ------------------------------------------------------------------ 3
    void settingsPage_save_updatesOnlyOneRow()
    {
        QVERIFY(openDb(freshDbPath("settings.db")));
        QVERIFY(ts::runMigrations(ts::initSqlPath()));
        QSqlQuery q;
        // Simulate the duplicate rows left behind by pre-fix launches (the fixed
        // migration no longer creates them).
        for (int i = 0; i < 2; ++i)
            QVERIFY(q.exec("INSERT INTO settings (hospital_name, software_name) VALUES ('dup', 'dup')"));
        QCOMPARE(ts::countRows("settings"), 3);
        // Make rows distinguishable.
        QVERIFY(q.exec("UPDATE settings SET rtsp_link = 'rtsp://row' || id, hospital_name = 'H' || id"));

        {
            SettingsPage page;             // loads "SELECT * FROM settings ORDER BY id LIMIT 1" (row 1)
            ts::ModalCloser closer;        // closes the "Settings saved" QMessageBox
            QVERIFY(QMetaObject::invokeMethod(&page, "saveSettingsToDatabase", Qt::DirectConnection));
            qInfo() << "modal dialogs closed during save:" << closer.closed;
        }

        QVERIFY(q.exec("SELECT id, rtsp_link, hospital_name FROM settings ORDER BY id"));
        QStringList clobbered;
        while (q.next()) {
            const int id = q.value(0).toInt();
            const QString rtsp = q.value(1).toString();
            if (id != 1 && rtsp != QString("rtsp://row%1").arg(id))
                clobbered << QString("id=%1 rtsp_link='%2' hospital_name='%3'")
                                 .arg(id).arg(rtsp, q.value(2).toString());
        }
        QVERIFY2(clobbered.isEmpty(),
                 qPrintable("SettingsPage::saveSettingsToDatabase UPDATE has no WHERE clause; rows other than "
                            "the loaded one were overwritten: " + clobbered.join("; ")));
    }

    // ------------------------------------------------------------------ 4
    void qtVsFlask_settingsRead_afterSaveAndRelaunch()
    {
        QVERIFY(openDb(freshDbPath("diverge.db")));
        QVERIFY(ts::runMigrations(ts::initSqlPath()));           // launch 1

        {   // User edits the (visible) hospital name in Settings and saves.
            SettingsPage page;
            QLineEdit* name = nullptr;
            for (QLineEdit* e : page.findChildren<QLineEdit*>())
                if (e->text() == "Brainwave") name = e;
            QVERIFY2(name, "could not locate hospital-name QLineEdit in SettingsPage");
            name->setText("User Saved Hospital");
            ts::ModalCloser closer;
            QVERIFY(QMetaObject::invokeMethod(&page, "saveSettingsToDatabase", Qt::DirectConnection));
        }
        // rtsp_link changed on the row the Qt app reads.  (Not possible via the UI:
        // SettingsPage's rtspLinkEdit is hidden and never added to a layout, so
        // it is not reachable/visible.)
        QSqlQuery u;
        QVERIFY(u.exec("UPDATE settings SET rtsp_link = 'rtsp://user-configured/stream' WHERE id = 1"));

        QVERIFY(ts::runMigrations(ts::initSqlPath()));           // launch 2 (pre-fix: added a default row)

        QSqlQuery qt;   // SettingsPage.cpp / mainwindow.cpp / HomePage.cpp form
        QVERIFY(qt.exec("SELECT id, rtsp_link, hospital_name FROM settings ORDER BY id LIMIT 1") && qt.next());
        QSqlQuery fl;   // flask_zoom_api/app.py get_rtsp_url_from_db() row selection
        QVERIFY(fl.exec("SELECT id, rtsp_link, hospital_name FROM settings ORDER BY id LIMIT 1") && fl.next());
        const QString qtVal = QString("id=%1 rtsp='%2' hospital='%3'")
                                  .arg(qt.value(0).toInt()).arg(qt.value(1).toString(), qt.value(2).toString());
        const QString flVal = QString("id=%1 rtsp='%2' hospital='%3'")
                                  .arg(fl.value(0).toInt()).arg(fl.value(1).toString(), fl.value(2).toString());
        qInfo() << "Qt reads:" << qtVal << " Flask reads:" << flVal;
        QVERIFY2(qt.value(1).toString() == fl.value(1).toString(),
                 qPrintable(QString("after save + relaunch the Qt app and Flask API read different settings rows/"
                                    "rtsp_link: Qt {%1} vs Flask {%2}").arg(qtVal, flVal)));
    }

    void qtVsFlask_settingsRead_realDbCopy()
    {
        const QString copy = ts::copyRealDb(*m_dir);
        QVERIFY(!copy.isEmpty());
        QVERIFY(openDb(copy));
        QSqlQuery qt, fl;
        QVERIFY(qt.exec("SELECT id, rtsp_link FROM settings ORDER BY id LIMIT 1") && qt.next());
        QVERIFY(fl.exec("SELECT id, rtsp_link FROM settings ORDER BY id LIMIT 1") && fl.next());
        qInfo("real DB copy: Qt reads row id=%d, Flask reads row id=%d; rtsp equal=%s",
              qt.value(0).toInt(), fl.value(0).toInt(),
              qt.value(1).toString() == fl.value(1).toString() ? "yes" : "no");
        QVERIFY2(qt.value(0).toInt() == fl.value(0).toInt(),
                 qPrintable(QString("Qt reads settings id=%1, Flask reads id=%2 (different rows)")
                            .arg(qt.value(0).toInt()).arg(fl.value(0).toInt())));
    }

    // ------------------------------------------------------------------ 5
    void migrationSplit_semicolonInsideStatement_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::addColumn<QString>("expected");
        QTest::newRow("string literal")
            << "CREATE TABLE t(v TEXT);\nINSERT INTO t(v) VALUES ('a;b');\n" << "a;b";
        QTest::newRow("trigger body")
            << "CREATE TABLE t(v TEXT);\n"
               "CREATE TRIGGER trg AFTER INSERT ON t BEGIN UPDATE t SET v = v || '!'; END;\n"
               "INSERT INTO t(v) VALUES ('x');\n" << "x!";
        QTest::newRow("comments and escaped quote")
            << "-- header; comment\nCREATE TABLE t(v TEXT); /* block; comment */\n"
               "INSERT INTO t(v) VALUES ('it''s;ok'); -- trailing; comment\n" << "it's;ok";
        QTest::newRow("trigger body with CASE ... END")
            << "CREATE TABLE t(v TEXT);\n"
               "CREATE TRIGGER trg AFTER INSERT ON t BEGIN\n"
               "  UPDATE t SET v = CASE WHEN v = 'x' THEN 'y;' ELSE v END;\n"
               "END;\n"
               "INSERT INTO t(v) VALUES ('x');" << "y;";
    }
    void migrationSplit_semicolonInsideStatement()
    {
        QFETCH(QString, sql);
        QFETCH(QString, expected);
        QVERIFY(openDb(freshDbPath("split.db")));
        const QString file = m_dir->filePath("m.sql");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(sql.toUtf8());
        f.close();
        const bool ok = ts::runMigrations(file);
        QVERIFY2(ok, "runMigrations() (main.cpp) failed: naive sql.split(';') cut a statement containing ';'");
        QSqlQuery q;
        QVERIFY(q.exec("SELECT v FROM t") && q.next());
        QCOMPARE(q.value(0).toString(), expected);
    }

    // ------------------------------------------------------------------ 6a DatabaseManager error reporting
    void databaseManager_reportsRealSqliteError()
    {
        QVERIFY(openDb(freshDbPath("dm.db")));
        QVERIFY(ts::runMigrations(ts::initSqlPath()));
        ts::MessageCapture cap;
        const bool ok = m_db->executeQuery("INSERT INTO no_such_table (a) VALUES (?)", {1});
        const QStringList msgs = cap.take();
        QVERIFY(!ok);
        const QString joined = msgs.join(" / ");
        QVERIFY2(joined.contains("no such table", Qt::CaseInsensitive),
                 qPrintable("DatabaseManager::executeQuery ignores QSqlQuery::prepare() result; logged error is: "
                            + joined));
    }

    // Prepare each controller's SQL (copied verbatim from api/*/*.cpp) directly
    // against the schema produced by init.sql to obtain the real sqlite error.
    void controllerSql_prepare_data()
    {
        QTest::addColumn<QString>("sql");
        auto row = [](const char* tag, const char* sql) { QTest::newRow(tag) << QString(sql); };
        row("PatientController::addPatient",
            "INSERT INTO patients (first_name, last_name, patient_id, mobile, dob, gender, address) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
        row("PatientController::editPatient",
            "UPDATE patients SET first_name=?, last_name=?, mobile=?, dob=?, gender=?, address=? WHERE id=?");
        row("SurgeryController::addSurgery",
            "INSERT INTO surgeries (patient_id, surgeon_name, surgery_type, body_part, additional_surgeon, "
            "anesthesiologist, surgery_date, operation_theatre_number, surgical_history) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        row("SurgeryController::editSurgery",
            "UPDATE surgeries SET surgeon_name=?, surgery_type=?, body_part=?, additional_surgeon=?, "
            "anesthesiologist=?, surgery_date=?, operation_theatre_number=?, surgical_history=? WHERE id=?");
        row("ImageController::getImagesBySurgeryId", "SELECT * FROM snapshots WHERE surgery_id = ?");
        row("ImageController::addImage",         "INSERT INTO snapshots (patient_id, surgery_id, file_path, title) VALUES (?, ?, ?, ?)");
        row("ImageController::editImage",        "UPDATE snapshots SET file_path = ?, title = ? WHERE id = ?");
        row("VideoController::addVideo",         "INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
        row("VideoController::editVideo",        "UPDATE recordings SET file_path=? WHERE id=?");
        row("VideoController::getVideosBySurgeryId", "SELECT * FROM recordings WHERE surgery_id = ?");
        row("VideoController::getVideoById",     "SELECT * FROM recordings WHERE id = ?");
        row("VideoController::deleteVideo",      "DELETE FROM recordings WHERE id = ?");
        row("CameraZoomController::getCameraSettings",
            "SELECT camera_ip, camera_type, user, password, video_streaming FROM company ORDER BY id ASC LIMIT 1");
    }
    void controllerSql_prepare()
    {
        QFETCH(QString, sql);
        QVERIFY(openDb(freshDbPath("prep.db")));
        QVERIFY(ts::runMigrations(ts::initSqlPath()));
        QSqlQuery q;
        const bool ok = q.prepare(sql);
        QVERIFY2(ok, qPrintable(QString("sqlite rejects SQL: %1 -> %2").arg(sql, q.lastError().databaseText())));
    }

    // ------------------------------------------------------------------ 6 controllers
    void controllers_data()
    {
        QTest::addColumn<QString>("controller");
        for (const char* c : {"Doctor", "Patient", "Surgery", "Image", "Video", "CommentImage",
                              "CommentVideo", "Company", "User", "CameraZoom"})
            QTest::newRow(c) << QString(c);
    }
    void controllers()
    {
        QFETCH(QString, controller);
        QVERIFY(openDb(freshDbPath("ctrl.db")));
        QVERIFY(ts::runMigrations(ts::initSqlPath()));
        // Seed rows the controllers read by id
        QSqlQuery s;
        QVERIFY(s.exec("INSERT INTO snapshots (patient_id, surgery_id, file_path, title) VALUES ('P1001', 1, '/x.png', 't')"));
        QVERIFY(s.exec("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES ('P1001', 1, '/x.mp4')"));

        ts::MessageCapture cap;
        Checks c{{}, &cap};
        QJsonObject r;

        if (controller == "Doctor") {
            DoctorController d(m_db);
            c.check("addDoctor", d.addDoctor({{"name", "Dr A"}, {"specialization", "S"}, {"contact", "1"}}, r));
            c.check("editDoctor(1)", d.editDoctor(1, {{"name", "Dr B"}, {"specialization", "S"}, {"contact", "2"}}, r));
            c.check("getAllDoctors non-empty", !d.getAllDoctors().isEmpty());
        } else if (controller == "Patient") {
            PatientController p(m_db);
            c.check("addPatient", p.addPatient({{"first_name", "Bob"}, {"last_name", "B"}, {"patient_id", "P2001"},
                                                {"mobile", "1"}, {"dob", "1990-01-01"}, {"gender", "Male"},
                                                {"address", "A"}}, r));
            c.check("editPatient(1)", p.editPatient(1, {{"first_name", "Bob"}, {"last_name", "B"}, {"mobile", "1"},
                                                        {"dob", "1990-01-01"}, {"gender", "Male"},
                                                        {"address", "A"}}, r));
            c.check("getPatientById(1) non-empty", !p.getPatientById(1).isEmpty());
            c.check("getPatientList non-empty", !p.getPatientList().isEmpty());
        } else if (controller == "Surgery") {
            SurgeryController sc(m_db);
            c.check("addSurgery(P1001)", sc.addSurgery("P1001", {{"surgeon_name", "Dr A"}, {"surgery_type", "Appendectomy"},
                                                                  {"surgery_date", "2025-01-01"}}, r));
            c.check("editSurgery(1)", sc.editSurgery(1, {{"surgeon_name", "Dr A"}, {"surgery_type", "X"},
                                                         {"surgery_date", "2025-01-02"}}, r));
            c.check("getSurgeriesByPatientId(\"P1001\") returns seeded surgery (patient_id='P1001')",
                    !sc.getSurgeriesByPatientId("P1001").isEmpty());
            c.check("getSurgeryById(1) non-empty", !sc.getSurgeryById(1).isEmpty());
        } else if (controller == "Image") {
            ImageController ic(m_db);
            c.check("getImagesBySurgeryId(1) non-empty (1 snapshot seeded)", !ic.getImagesBySurgeryId(1).isEmpty());
            c.check("addImage", ic.addImage(1, {{"patient_id", "P1001"}, {"path", "/a.png"}, {"title", "t"}}, r));
            c.check("editImage(1)", ic.editImage(1, {{"path", "/b.png"}, {"title", "t2"}}, r));
        } else if (controller == "Video") {
            VideoController vc(m_db);
            c.check("addVideo", vc.addVideo(1, {{"patient_id", "P1001"}, {"path", "/a.mp4"}}, r));
            c.check("editVideo(1)", vc.editVideo(1, {{"path", "/b.mp4"}}, r));
            c.check("getVideosBySurgeryId(1) non-empty (1 recording seeded)", !vc.getVideosBySurgeryId(1).isEmpty());
            c.check("getVideoById(1) non-empty", !vc.getVideoById(1).isEmpty());
            c.check("deleteVideo(1)", vc.deleteVideo(1));
        } else if (controller == "CommentImage") {
            CommentImageController cc(m_db);
            c.check("addComment", cc.addComment(1, {{"text", "hi"}}, r));
            c.check("getCommentsByImageId(1) non-empty", !cc.getCommentsByImageId(1).isEmpty());
            c.check("editComment(1)", cc.editComment(1, {{"text", "edited"}}, r));
            c.check("deleteComment(1)", cc.deleteComment(1));
        } else if (controller == "CommentVideo") {
            CommentVideoController cc(m_db);
            c.check("addComment", cc.addComment(1, {{"text", "hi"}}, r));
            c.check("getCommentsByVideoId(1) non-empty", !cc.getCommentsByVideoId(1).isEmpty());
            c.check("editComment(1)", cc.editComment(1, {{"text", "edited"}}, r));
            c.check("deleteComment(1)", cc.deleteComment(1));
        } else if (controller == "Company") {
            CompanyController cc(m_db);
            c.check("getCompanyById(1) non-empty", !cc.getCompanyById(1).isEmpty());
            c.check("getFirstCompany non-empty", !cc.getFirstCompany().isEmpty());
            c.check("addCompany", cc.addCompany({{"name", "N"}, {"address", "A"}, {"phone", "P"}}, r));
            c.check("editCompany(1)", cc.editCompany(1, {{"name", "N2"}, {"address", "A"}, {"phone", "P"}}, r));
        } else if (controller == "User") {
            UserController uc(m_db);
            c.check("getAllUsers non-empty", !uc.getAllUsers().isEmpty());
            c.check("getUserById(1) non-empty", !uc.getUserById(1).isEmpty());
            c.check("createUser", uc.createUser({{"name", "U"}, {"email", "u@x"}}));
            c.check("updateUser(1)", uc.updateUser(1, {{"name", "U2"}, {"email", "u2@x"}}));
            c.check("deleteUser(2)", uc.deleteUser(2));
        } else if (controller == "CameraZoom") {
            // Only the DB read; moveCamera() does network I/O + sleep(1).
            CameraZoomController cz(m_db);
            c.check("getCameraSettings non-empty", !cz.getCameraSettings().isEmpty());
        }

        QVERIFY2(c.failures.isEmpty(),
                 qPrintable(controller + "Controller SQL vs real schema: " + c.failures.join(" || ")));
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("offscreen")
                                                                     : qgetenv("QT_QPA_PLATFORM"));
    QApplication app(argc, argv);
    TstDatabase t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_database.moc"
