moc api/surgeries/SurgeryController.hpp -o moc_SurgeryController.cpp

g++ \
  api/surgeries/test_surgery.cpp \
  api/surgeries/SurgeryController.cpp \
  database/DatabaseManager.cpp \
  moc_SurgeryController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/surgeries -Idatabase -I. \
  -fPIC -std=c++17 -o test_surgery \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
