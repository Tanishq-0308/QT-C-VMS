cmake_minimum_required(VERSION 3.14)

# Standalone GPU recording test project. Compiles the production sources unmodified:
#   ui/Recording/VideoRecorder.cpp  +  cuda/frame_convert.cu
#   cmake -S tests/unit_gpu -B build-tests-gpu        [-DSANITIZE=address|thread]
# CUDA 13 nvcc, sm_86 (RTX A400). CUDA must be set before project().
set(CMAKE_CUDA_COMPILER /usr/local/cuda/bin/nvcc)
set(CMAKE_CUDA_ARCHITECTURES 86)
project(tst_videorecorder_gpu LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(SANITIZE "" CACHE STRING "address | thread | empty")

find_package(Qt5 REQUIRED COMPONENTS Core Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED libavformat libavcodec libavutil)

add_executable(tst_videorecorder
  tst_videorecorder.cpp
  gpu_stall.cu
  ${REPO_ROOT}/ui/Recording/VideoRecorder.cpp
  ${REPO_ROOT}/ui/Recording/VideoRecorder.hpp
  ${REPO_ROOT}/cuda/frame_convert.cu
)
target_include_directories(tst_videorecorder PRIVATE
  ${REPO_ROOT} ${REPO_ROOT}/include ${REPO_ROOT}/decklink
  /usr/local/cuda/include ${AV_INCLUDE_DIRS})
target_link_directories(tst_videorecorder PRIVATE /usr/local/cuda/targets/x86_64-linux/lib ${AV_LIBRARY_DIRS})
target_link_libraries(tst_videorecorder PRIVATE Qt5::Core Qt5::Test ${AV_LIBRARIES} cuda cudart pthread)

target_compile_options(tst_videorecorder PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-g -fno-omit-frame-pointer>)
if(SANITIZE STREQUAL "address")
  target_compile_options(tst_videorecorder PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address>)
  target_link_options(tst_videorecorder PRIVATE -fsanitize=address)
elseif(SANITIZE STREQUAL "thread")
  target_compile_options(tst_videorecorder PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=thread -O1>)
  target_link_options(tst_videorecorder PRIVATE -fsanitize=thread)
endif()
target_compile_definitions(tst_videorecorder PRIVATE SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
