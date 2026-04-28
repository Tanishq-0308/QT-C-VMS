moc api/patients/PatientController.hpp -o moc_PatientController.cpp

g++ \
  api/patients/test_patient.cpp \
  api/patients/PatientController.cpp \
  database/DatabaseManager.cpp \
  moc_PatientController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/patients -Idatabase -I. \
  -fPIC -std=c++17 -o test_patient_db \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
