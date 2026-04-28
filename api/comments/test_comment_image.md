moc api/comments/CommentImageController.hpp -o moc_CommentImageController.cpp

g++ \
  api/comments/test_comment_image.cpp \
  api/comments/CommentImageController.cpp \
  database/DatabaseManager.cpp \
  moc_CommentImageController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/comments -Idatabase -I. \
  -fPIC -std=c++17 -o test_comment_image \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
