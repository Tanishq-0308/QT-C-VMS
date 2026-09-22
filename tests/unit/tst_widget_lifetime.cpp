// tst_widget_lifetime (offscreen): object-tree growth and RSS for dialogs and
// pages that need neither DeckLink nor GL.
//
//  * generic_* : create -> show -> close -> delete, 100x, under a parent;
//                parent's child count must return to baseline.
//  * path_*    : drive the real production code paths that open dialogs with
//                exec() (closed via ts::ModalCloser) 100x, and count the dialog
//                objects left parented to the page afterwards.
//
// Uses a COPY of the real sqlite.db in a QTemporaryDir as the default connection.

#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <functional>

#include "common/TestSupport.hpp"
#include "common/ModalCloser.hpp"

#include "ui/AddPatientDialog/AddPatientDialog.hpp"
#include "ui/EditPatientDialog/EditPatientDialog.hpp"
#include "ui/AddSurgeryDialog/AddSurgeryDialog.hpp"
#include "ui/EditSurgeryDialog/EditSurgeryDialog.hpp"
#include "ui/AddCommentDialog/AddCommentDialog.h"
#include "ui/patient/PatientPage.hpp"
#include "ui/SurgeryDetails/SurgeryDetailsPage.hpp"
#include "ui/Settings/SettingsPage.hpp"
#include "ui/SurgeryRecordPage/SurgeryRecordingPage.hpp"
#ifdef HAVE_POPPLER
#include "ui/PdfViewerPage/PdfViewerPage.hpp"
#endif

static const int kIterations = 100;
static const char* kPatientId = "P1001";
static const int kSurgeryId = 1;

using Factory = std::function<QWidget*(QWidget* parent)>;
Q_DECLARE_METATYPE(Factory)

namespace {

void pump(int ms = 0)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QPushButton* findButton(QWidget* root, const QString& text)
{
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

struct Sample { int children; long rssKiB; QMap<QString, int> classes; };
Sample sample(QObject* o)
{
    Sample s{ o->findChildren<QObject*>().size(), ts::rssKiB(), {} };
    for (QObject* c : o->findChildren<QObject*>()) s.classes[QString::fromLatin1(c->metaObject()->className())]++;
    return s;
}
QString classDiff(const Sample& a, const Sample& b)
{
    QStringList out;
    QSet<QString> keys;
    for (auto it = a.classes.begin(); it != a.classes.end(); ++it) keys.insert(it.key());
    for (auto it = b.classes.begin(); it != b.classes.end(); ++it) keys.insert(it.key());
    for (const QString& k : keys) {
        const int d = b.classes.value(k) - a.classes.value(k);
        if (d) out << QString("%1%2 %3").arg(d > 0 ? "+" : "").arg(d).arg(k);
    }
    out.sort();
    return out.join(", ");
}

} // namespace

class TstWidgetLifetime : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QSqlDatabase m_db;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString copy = ts::copyRealDb(m_dir);
        QVERIFY2(!copy.isEmpty(), "could not copy sqlite.db");
        m_db = QSqlDatabase::addDatabase("QSQLITE");
        m_db.setDatabaseName(copy);
        QVERIFY(m_db.open());
        QSqlQuery q;
        QVERIFY(q.exec(QString("SELECT COUNT(*) FROM patients WHERE patient_id='%1'").arg(kPatientId)) && q.next());
        if (q.value(0).toInt() == 0)
            QSKIP("patient P1001 missing from sqlite.db copy");
        QVERIFY(q.exec(QString("SELECT COUNT(*) FROM surgeries WHERE id=%1").arg(kSurgeryId)) && q.next());
        if (q.value(0).toInt() == 0)
            QSKIP("surgery id 1 missing from sqlite.db copy");
        qInfo("platform=%s  db copy=%s", qPrintable(QGuiApplication::platformName()), qPrintable(copy));
    }

    // -------------------------------------------------------------- generic
    void generic_createShowCloseDelete_data()
    {
        QTest::addColumn<Factory>("factory");
        QTest::newRow("AddPatientDialog")  << Factory([](QWidget* p) { return new AddPatientDialog(p); });
        QTest::newRow("EditPatientDialog") << Factory([](QWidget* p) { return new EditPatientDialog(kPatientId, p); });
        QTest::newRow("AddSurgeryDialog")  << Factory([](QWidget* p) { return new AddSurgeryDialog(kPatientId, p); });
        QTest::newRow("EditSurgeryDialog") << Factory([](QWidget* p) { return new EditSurgeryDialog(kSurgeryId, p); });
        QTest::newRow("AddCommentDialog")  << Factory([](QWidget* p) { return new AddCommentDialog(p); });
        QTest::newRow("PatientPage")       << Factory([](QWidget* p) { return new PatientPage(p); });
        QTest::newRow("SurgeryDetailsPage")<< Factory([](QWidget* p) {
            auto* w = new SurgeryDetailsPage(p); w->loadPatientData(kPatientId); return w; });
        QTest::newRow("SettingsPage")      << Factory([](QWidget* p) { return new SettingsPage(p); });
        QTest::newRow("SurgeryRecordingPage") << Factory([](QWidget* p) {
            return new SurgeryRecordingPage(kPatientId, kSurgeryId, p); });
#ifdef HAVE_POPPLER
        QTest::newRow("PdfViewerPage")     << Factory([](QWidget* p) { return new PdfViewerPage(p); });
#endif
    }
    void generic_createShowCloseDelete()
    {
        QFETCH(Factory, factory);
        QWidget parent;
        parent.resize(1280, 800);
        parent.show();
        ts::ModalCloser closer;       // in case a constructor pops a QMessageBox
        { QWidget* w = factory(&parent); w->show(); pump(60); w->close(); delete w; pump(); }  // warm-up
        const Sample before = sample(&parent);
        for (int i = 0; i < kIterations; ++i) {
            QWidget* w = factory(&parent);
            w->show();
            pump(i % 10 == 0 ? 60 : 0);   // let the 50 ms showEvent timers fire sometimes
            w->close();
            delete w;
            pump();
        }
        QTest::qWait(120);
        pump();
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        qInfo("%s: parent children %d -> %d, RSS %ld KiB -> %ld KiB (delta %+ld KiB over %d iterations)",
              QTest::currentDataTag(), before.children, after.children, before.rssKiB, after.rssKiB,
              after.rssKiB - before.rssKiB, kIterations);
        if (!closer.closed.isEmpty())
            qInfo() << "modal widgets auto-closed:" << closer.closed.mid(0, 5) << "total" << closer.closed.size();
        QCOMPARE(after.children, before.children);
    }

    // -------------------------------------------------------------- production paths
    void path_SurgeryDetailsPage_editPatient()
    {
        QWidget parent;
        parent.show();
        auto* page = new SurgeryDetailsPage(&parent);
        page->loadPatientData(kPatientId);
        page->show();
        QTest::qWait(150); pump();
        QPushButton* btn = findButton(page, "Edit Patient");
        QVERIFY2(btn, "Edit Patient button not found");
        const Sample before = sample(&parent);
        const int dlgBefore = page->findChildren<EditPatientDialog*>().size();
        {
            ts::ModalCloser closer;
            for (int i = 0; i < kIterations; ++i) { btn->click(); pump(); }
            QVERIFY2(closer.closed.size() >= kIterations,
                     qPrintable(QString("expected %1 modal closes, got %2").arg(kIterations).arg(closer.closed.size())));
        }
        pump(50);
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        const int dlgAfter = page->findChildren<EditPatientDialog*>().size();
        qInfo("SurgeryDetailsPage 'Edit Patient' x%d: EditPatientDialog children %d -> %d; parent tree %d -> %d objects; "
              "RSS %ld -> %ld KiB (delta %+ld)", kIterations, dlgBefore, dlgAfter, before.children, after.children,
              before.rssKiB, after.rssKiB, after.rssKiB - before.rssKiB);
        QVERIFY2(dlgAfter == dlgBefore,
                 qPrintable(QString("%1 EditPatientDialog objects remain parented to SurgeryDetailsPage after %2 "
                                    "open/close cycles (new EditPatientDialog(..., this) never deleted); "
                                    "object tree grew by %3")
                            .arg(dlgAfter - dlgBefore).arg(kIterations).arg(after.children - before.children)));
        delete page;
    }

    void path_SurgeryDetailsPage_newSurgery()
    {
        QWidget parent;
        parent.show();
        auto* page = new SurgeryDetailsPage(&parent);
        page->loadPatientData(kPatientId);
        page->show();
        QTest::qWait(150); pump();
        QPushButton* btn = findButton(page, "+ New Surgery");
        QVERIFY(btn);
        { ts::ModalCloser closer; btn->click(); pump(50); }   // warm-up (one-time lazy objects)
        const Sample before = sample(&parent);
        { ts::ModalCloser closer; for (int i = 0; i < kIterations; ++i) { btn->click(); pump(); } }
        pump(50);
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        qInfo("SurgeryDetailsPage '+ New Surgery' x%d: parent tree %d -> %d; RSS delta %+ld KiB",
              kIterations, before.children, after.children, after.rssKiB - before.rssKiB);
        QCOMPARE(after.children, before.children);
        delete page;
    }

    void path_PatientPage_addPatient()
    {
        QWidget parent;
        parent.show();
        auto* page = new PatientPage(&parent);
        page->show();
        QTest::qWait(150); pump();
        QPushButton* btn = nullptr;
        for (QPushButton* b : page->findChildren<QPushButton*>())
            if (b->text().contains("Add", Qt::CaseInsensitive)) { btn = b; break; }
        if (!btn) QSKIP("PatientPage add button not found by text");
        { ts::ModalCloser closer; btn->click(); pump(50); }   // warm-up (one-time lazy objects)
        const Sample before = sample(&parent);
        { ts::ModalCloser closer; for (int i = 0; i < kIterations; ++i) { btn->click(); pump(); } }
        pump(50);
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        qInfo("PatientPage '%s' x%d: parent tree %d -> %d; RSS delta %+ld KiB", qPrintable(btn->text()),
              kIterations, before.children, after.children, after.rssKiB - before.rssKiB);
        QCOMPARE(after.children, before.children);
        delete page;
    }

    void path_SurgeryRecordingPage_editSurgery()
    {
        QWidget parent;
        parent.show();
        auto* page = new SurgeryRecordingPage(kPatientId, kSurgeryId, &parent);
        page->show();
        QTest::qWait(150); pump();
        QPushButton* btn = findButton(page, "Edit Surgery");
        QVERIFY(btn);
        const Sample before = sample(&parent);
        const int dlgBefore = page->findChildren<EditSurgeryDialog*>().size();
        { ts::ModalCloser closer; for (int i = 0; i < kIterations; ++i) { btn->click(); pump(); } }
        pump(50);
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        const int dlgAfter = page->findChildren<EditSurgeryDialog*>().size();
        qInfo("SurgeryRecordingPage 'Edit Surgery' x%d: EditSurgeryDialog children %d -> %d; parent tree %d -> %d; "
              "RSS delta %+ld KiB", kIterations, dlgBefore, dlgAfter, before.children, after.children,
              after.rssKiB - before.rssKiB);
        QVERIFY2(dlgAfter == dlgBefore,
                 qPrintable(QString("%1 EditSurgeryDialog objects remain parented to SurgeryRecordingPage after %2 "
                                    "cycles (new EditSurgeryDialog(m_surgeryId, this) never deleted); tree grew by %3")
                            .arg(dlgAfter - dlgBefore).arg(kIterations).arg(after.children - before.children)));
        delete page;
    }

    void path_SurgeryRecordingPage_preview_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("iterations");
        QTest::newRow("image (.png)") << m_dir.filePath("does_not_exist.png") << kIterations;
        QTest::newRow("video (.mp4)") << m_dir.filePath("does_not_exist.mp4") << kIterations;
    }
    void path_SurgeryRecordingPage_preview()
    {
        QFETCH(QString, file);
        QFETCH(int, iterations);
        QWidget parent;
        parent.show();
        auto* page = new SurgeryRecordingPage(kPatientId, kSurgeryId, &parent);
        page->show();
        QTest::qWait(150); pump();
        const Sample before = sample(&parent);
        const int dlgBefore = page->findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly).size();
        {
            ts::ModalCloser closer;
            for (int i = 0; i < iterations; ++i) {
                QVERIFY(QMetaObject::invokeMethod(page, "handleThumbnailClick", Qt::DirectConnection,
                                                  Q_ARG(QString, file), Q_ARG(int, 1)));
                pump();
            }
        }
        QTest::qWait(100);
        pump();
        const Sample after = sample(&parent);
        qInfo("  object-tree class delta: %s", qPrintable(classDiff(before, after)));
        const int dlgAfter = page->findChildren<QDialog*>(QString(), Qt::FindDirectChildrenOnly).size();
        qInfo("SurgeryRecordingPage::handleThumbnailClick(%s) x%d: QDialog children %d -> %d; parent tree %d -> %d; "
              "RSS %ld -> %ld KiB (delta %+ld)", qPrintable(QFileInfo(file).suffix()), iterations, dlgBefore, dlgAfter,
              before.children, after.children, before.rssKiB, after.rssKiB, after.rssKiB - before.rssKiB);
        QVERIFY2(dlgAfter == dlgBefore,
                 qPrintable(QString("%1 preview QDialog objects remain parented to SurgeryRecordingPage after %2 "
                                    "previews (new QDialog(this) never deleted); object tree grew by %3")
                            .arg(dlgAfter - dlgBefore).arg(iterations).arg(after.children - before.children)));
        delete page;
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TstWidgetLifetime t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_widget_lifetime.moc"
