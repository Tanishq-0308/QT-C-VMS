moc api/images/ImageController.hpp -o moc_ImageController.cpp

g++ \
  api/images/test_image.cpp \
  api/images/ImageController.cpp \
  database/DatabaseManager.cpp \
  moc_ImageController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/images -Idatabase -I. \
  -fPIC -std=c++17 -o test_image \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
