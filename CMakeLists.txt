cmake_minimum_required(VERSION 3.14)
project(medical_qt_app)

# Default to an optimised build; an empty build type compiles without optimisation
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)

# ✅ Enable Qt MOC, RCC, and UIC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---- CUDA Toolkit ----
if(NOT DEFINED CUDA_TOOLKIT_ROOT_DIR)
    set(CUDA_TOOLKIT_ROOT_DIR "/usr/local/cuda")
endif()

# ✅ Enable CUDA (RTX A400 = Ampere, compute capability 8.6).
# Compile kernels with the same toolkit whose headers and libcudart are used below; the distro
# nvcc in /usr/bin is a different (older) version.
if(NOT DEFINED CMAKE_CUDA_COMPILER AND EXISTS "${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc")
    set(CMAKE_CUDA_COMPILER "${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc")
endif()
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
endif()
enable_language(CUDA)

include_directories(${CUDA_TOOLKIT_ROOT_DIR}/include)
link_directories(${CUDA_TOOLKIT_ROOT_DIR}/targets/x86_64-linux/lib)

# ---- NVIDIA Video Codec SDK ----
set(NVCODEC_PATH "/home/brainwave/sdk/Video_Codec_SDK_13.0.19/Interface")
include_directories(${NVCODEC_PATH})

file(COPY ${CMAKE_SOURCE_DIR}/assets/logo.png DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# ✅ Find Qt5 modules
# find_package(Qt5 REQUIRED COMPONENTS Widgets Sql Network Multimedia MultimediaWidgets OpenGL)
find_package(Qt5 REQUIRED COMPONENTS Widgets Sql Network Multimedia MultimediaWidgets OpenGL Concurrent)

find_package(OpenGL REQUIRED)  # 🔥 Required for raw gl* calls


# Find Poppler-Qt5
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED poppler-qt5)

# Include directories for Poppler
include_directories(${POPPLER_INCLUDE_DIRS})
link_directories(${POPPLER_LIBRARY_DIRS})


# ✅ Find OpenCV via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCV REQUIRED opencv4)

include_directories(${OPENCV_INCLUDE_DIRS})
link_directories(${OPENCV_LIBRARY_DIRS})


# ✅ Find FFmpeg libraries via PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED libavformat libavcodec libavutil libswscale)
include_directories(${AV_INCLUDE_DIRS})
link_directories(${AV_LIBRARY_DIRS})

# ✅ Add Qt resource file
set(RESOURCE_FILES
    assets/resources.qrc
)

# ✅ Collect all source files (excluding test_*.cpp)
file(GLOB_RECURSE SRC_FILES
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/widgets/*
    ${CMAKE_SOURCE_DIR}/mainwindow.cpp
    ${CMAKE_SOURCE_DIR}/mainwindow.hpp
    ${CMAKE_SOURCE_DIR}/core/*.cpp
    ${CMAKE_SOURCE_DIR}/core/*.hpp
    ${CMAKE_SOURCE_DIR}/database/*.cpp
    ${CMAKE_SOURCE_DIR}/database/*.hpp
    ${CMAKE_SOURCE_DIR}/decklink/*.cpp
    ${CMAKE_SOURCE_DIR}/decklink/*.h
    ${CMAKE_SOURCE_DIR}/include/DeckLinkAPIDispatch.cpp
    ${CMAKE_SOURCE_DIR}/cuda/frame_convert.cu
    ${CMAKE_SOURCE_DIR}/ui/**/*.cpp
    ${CMAKE_SOURCE_DIR}/ui/**/*.hpp
    ${CMAKE_SOURCE_DIR}/api/**/*.cpp
    ${CMAKE_SOURCE_DIR}/api/**/*.hpp
)

foreach(file ${SRC_FILES})
    if("${file}" MATCHES ".*/test_.*\\.cpp$")
        list(REMOVE_ITEM SRC_FILES ${file})
    endif()
endforeach()

# Define DeckLink API version macro
add_definitions(-DBLACKMAGIC_DECKLINK_API_VERSION=0x00000003)



# ✅ Define executable
add_executable(${PROJECT_NAME}
    ${SRC_FILES}
    ${RESOURCE_FILES}
)

# ✅ Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/*.cpp
    ${CMAKE_SOURCE_DIR}/core/*.hpp
    ${CMAKE_SOURCE_DIR}/database
    ${CMAKE_SOURCE_DIR}/decklink
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ui/login
    ${CMAKE_SOURCE_DIR}/ui/patient
    ${CMAKE_SOURCE_DIR}/ui/Recording
    ${CMAKE_SOURCE_DIR}/ui/snapshot
    ${CMAKE_SOURCE_DIR}/ui/playback
    ${CMAKE_SOURCE_DIR}/ui/liveview
    ${CMAKE_SOURCE_DIR}/api/doctors
    ${CMAKE_SOURCE_DIR}/api/patients
    ${CMAKE_SOURCE_DIR}/api/surgeries
    ${CMAKE_SOURCE_DIR}/api/images
    ${CMAKE_SOURCE_DIR}/api/videos
    ${CMAKE_SOURCE_DIR}/api/comments
    ${CMAKE_SOURCE_DIR}/api/company_setting
    ${CMAKE_SOURCE_DIR}/api/system
    ${CMAKE_SOURCE_DIR}/api/user
    ${CMAKE_SOURCE_DIR}/api/camera_zoom
    ${PROJECT_SOURCE_DIR}/widgets
    ${CMAKE_SOURCE_DIR}/ui/dashboard
    ${OpenCV_INCLUDE_DIRS}
)

# Link DeckLink libraries
link_directories(
    /home/brainwave/Downloads/desktopvideo-14.4.1a4-x86_64/usr/lib/
    /usr/lib
)

# ✅ Link libraries
target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Sql
    Qt5::Network
    Qt5::OpenGL
    Qt5::Concurrent 
    OpenGL::GL
    DeckLinkAPI
    DeckLinkPreviewAPI
    Qt5::Widgets
    Qt5::Multimedia
    Qt5::MultimediaWidgets
    ${AV_LIBRARIES}
    cuda
    cudart
    nvcuvid
    ${OpenCV_LIBS}
    ${OPENCV_LIBRARIES}
    ${POPPLER_LIBRARIES}
)




# add_executable(nv12_to_mp4_test
#     widgets/nv12_to_mp4_test.cpp
#     cuda/nv12_to_rgba.cu
# )

# target_include_directories(nv12_to_mp4_test PRIVATE
#     ${CMAKE_SOURCE_DIR}
#     ${CUDA_TOOLKIT_ROOT_DIR}/include
#     ${NVCODEC_PATH}
#     ${AV_INCLUDE_DIRS}
# )

# target_link_libraries(nv12_to_mp4_test
#     Qt5::Core
#     Qt5::Gui
#     Qt5::Widgets
#     ${AV_LIBRARIES}
    # cuda
    # cudart
    # nvcuvid
# )

