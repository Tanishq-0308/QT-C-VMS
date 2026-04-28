// ui/dashboard/test_dashboard.cpp
#include <QApplication>
#include "HomePage.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    HomePage home;
    home.show();

    return app.exec();
}
