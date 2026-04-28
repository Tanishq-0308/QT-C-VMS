moc ui/patient/PatientPage.hpp -o moc_PatientPage.cpp


<!-- g++ ui/patient/test_patient.cpp \
    ui/patient/PatientPage.cpp \
    moc_PatientPage.cpp \
    -Iui/patient -Iassets/styles -fPIC -std=c++17 -o test_patient \
    $(pkg-config --cflags --libs Qt5Widgets) -->

g++ ui/patient/test_patient.cpp \
    ui/patient/PatientPage.cpp \
    moc_PatientPage.cpp \
    -Iui/patient -Iassets/styles -fPIC -std=c++17 -o test_patient \
    $(pkg-config --cflags --libs Qt5Widgets Qt5Network Qt5Sql)
