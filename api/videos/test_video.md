moc api/videos/VideoController.hpp -o moc_VideoController.cpp

g++ \
  api/videos/test_video.cpp \
  api/videos/VideoController.cpp \
  database/DatabaseManager.cpp \
  moc_VideoController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/videos -Idatabase -I. \
  -fPIC -std=c++17 -o test_video \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
