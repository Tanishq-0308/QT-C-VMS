// ui/dashboard/test_dashboard.cpp
#include <QApplication>
#include "DashboardPage.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    DashboardPage dashboard;
    dashboard.show();

    return app.exec();
}
