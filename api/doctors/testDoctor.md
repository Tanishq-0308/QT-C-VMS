g++ \
  api/doctors/test_doctor.cpp \
  api/doctors/DoctorController.cpp \
  database/DatabaseManager.cpp \
  moc_DoctorController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/doctors -Idatabase -I. \
  -fPIC -std=c++17 -o test_doctor \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)


./test_doctor


moc api/doctors/DoctorController.hpp -o moc_DoctorController.cpp
moc database/DatabaseManager.hpp -o moc_DatabaseManager.cpp
