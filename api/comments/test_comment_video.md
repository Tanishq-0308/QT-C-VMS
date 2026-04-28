moc api/comments/CommentVideoController.hpp -o moc_CommentVideoController.cpp

g++ \
  api/comments/test_comment_video.cpp \
  api/comments/CommentVideoController.cpp \
  database/DatabaseManager.cpp \
  moc_CommentVideoController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/comments -Idatabase -I. \
  -fPIC -std=c++17 -o test_comment_video \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
