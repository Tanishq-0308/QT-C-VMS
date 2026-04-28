moc api/user/UserController.hpp -o moc_UserController.cpp

g++ \
  api/user/test_user.cpp \
  api/user/UserController.cpp \
  database/DatabaseManager.cpp \
  moc_UserController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/user -Idatabase -I. \
  -fPIC -std=c++17 -o test_user \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
