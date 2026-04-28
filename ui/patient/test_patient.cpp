// ui/patient/test_patient.cpp
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "PatientPage.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QFile file(":assets/styles/patient.qss");
    if (file.open(QFile::ReadOnly)) {
        QString styleSheet = QTextStream(&file).readAll();
        app.setStyleSheet(styleSheet);
    }

    PatientPage page;
    page.resize(1024, 600);
    page.show();

    return app.exec();
}
