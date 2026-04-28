# Regenerate moc
moc ui/dashboard/DashboardPage.hpp -o moc_DashboardPage.cpp

# Compile

g++ ui/dashboard/test_dashboard.cpp \
    ui/dashboard/DashboardPage.cpp \
    moc_DashboardPage.cpp \
    -Iui/dashboard -fPIC -std=c++17 -o test_dashboard \
    $(pkg-config --cflags --libs Qt5Widgets Qt5Multimedia Qt5MultimediaWidgets)
