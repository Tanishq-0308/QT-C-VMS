cmake_minimum_required(VERSION 3.14)
project(medical_qt_app_unit_tests LANGUAGES CXX)

# Standalone test project. Configure with:
#   cmake -S tests/unit -B build-tests
#   cmake -S tests/unit -B build-tests-asan -DUNIT_SANITIZE=ON
# Compiles only the production sources each test needs; production files are
# referenced read-only from APP_DIR.

get_filename_component(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(UNIT_SANITIZE "Build with -fsanitize=address,undefined" OFF)
if(UNIT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
endif()

# Same Qt modules / pkg-config packages as the main CMakeLists.txt (subset needed)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Sql Network Multimedia MultimediaWidgets Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER poppler-qt5)

# Same DeckLink API macro as the main build
add_definitions(-DBLACKMAGIC_DECKLINK_API_VERSION=0x00000003)
add_compile_definitions(APP_SOURCE_DIR="${APP_DIR}")

set(COMMON_INCLUDES
    ${APP_DIR}
    ${APP_DIR}/include
    ${APP_DIR}/decklink
    ${APP_DIR}/database
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()

set(TEST_ENV
    "QT_QPA_PLATFORM=offscreen"
    "ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:malloc_context_size=40:abort_on_error=0:halt_on_error=0:print_suppressions=0"
    "UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0"
    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0:report_objects=0"
    "QT_LOGGING_RULES=qt.qpa.*=false"
)

function(unit_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${name}.cpp ${T_SOURCES})
    target_include_directories(${name} PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(${name} PRIVATE Qt5::Test ${T_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TEST_ENV}" TIMEOUT 600)
endfunction()

# ---------------------------------------------------------------- tst_comptr
unit_test(tst_comptr
    SOURCES
        ${APP_DIR}/decklink/com_ptr.h
        ${APP_DIR}/decklink/DeckLinkInputDevice.h
        ${APP_DIR}/decklink/DeckLinkInputDevice.cpp
    LIBS Qt5::Core Qt5::Widgets)

# ---------------------------------------------------------------- tst_database
file(GLOB API_CONTROLLER_SOURCES
    ${APP_DIR}/api/*/*Controller.cpp
    ${APP_DIR}/api/*/*Controller.hpp)
unit_test(tst_database
    SOURCES
        common/TestSupport.hpp
        common/ModalCloser.hpp
        ${APP_DIR}/database/DatabaseManager.cpp
        ${APP_DIR}/database/DatabaseManager.hpp
        ${API_CONTROLLER_SOURCES}
        ${APP_DIR}/ui/Settings/SettingsPage.cpp
        ${APP_DIR}/ui/Settings/SettingsPage.hpp
    LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql Qt5::Network)

# ---------------------------------------------------------------- tst_widget_lifetime
set(WIDGET_SOURCES
    common/TestSupport.hpp
    common/ModalCloser.hpp
    ${APP_DIR}/core/ResponsiveWidget.hpp
    ${APP_DIR}/core/ResponsiveWidget.cpp
    ${APP_DIR}/core/UIScale.hpp
    ${APP_DIR}/ui/AddPatientDialog/AddPatientDialog.cpp
    ${APP_DIR}/ui/AddPatientDialog/AddPatientDialog.hpp
    ${APP_DIR}/ui/EditPatientDialog/EditPatientDialog.cpp
    ${APP_DIR}/ui/EditPatientDialog/EditPatientDialog.hpp
    ${APP_DIR}/ui/AddSurgeryDialog/AddSurgeryDialog.cpp
    ${APP_DIR}/ui/AddSurgeryDialog/AddSurgeryDialog.hpp
    ${APP_DIR}/ui/EditSurgeryDialog/EditSurgeryDialog.cpp
    ${APP_DIR}/ui/EditSurgeryDialog/EditSurgeryDialog.hpp
    ${APP_DIR}/ui/AddCommentDialog/AddCommentDialog.cpp
    ${APP_DIR}/ui/AddCommentDialog/AddCommentDialog.h
    ${APP_DIR}/ui/patient/PatientPage.cpp
    ${APP_DIR}/ui/patient/PatientPage.hpp
    ${APP_DIR}/ui/SurgeryDetails/SurgeryDetailsPage.cpp
    ${APP_DIR}/ui/SurgeryDetails/SurgeryDetailsPage.hpp
    ${APP_DIR}/ui/Settings/SettingsPage.cpp
    ${APP_DIR}/ui/Settings/SettingsPage.hpp
    ${APP_DIR}/ui/SurgeryRecordPage/SurgeryRecordingPage.cpp
    ${APP_DIR}/ui/SurgeryRecordPage/SurgeryRecordingPage.hpp
    ${APP_DIR}/ui/SurgeryRecordPage/ClickableSlider.hpp
    ${APP_DIR}/ui/ClickableLabel/ClickableLabel.cpp
    ${APP_DIR}/ui/ClickableLabel/ClickableLabel.hpp
    ${APP_DIR}/assets/resources.qrc
)
set(WIDGET_LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql Qt5::Network Qt5::Multimedia Qt5::MultimediaWidgets)
if(POPPLER_FOUND)
    list(APPEND WIDGET_SOURCES
        ${APP_DIR}/ui/PdfViewerPage/PdfViewerPage.cpp
        ${APP_DIR}/ui/PdfViewerPage/PdfViewerPage.hpp)
    list(APPEND WIDGET_LIBS ${POPPLER_LINK_LIBRARIES})
endif()
unit_test(tst_widget_lifetime SOURCES ${WIDGET_SOURCES} LIBS ${WIDGET_LIBS})
if(POPPLER_FOUND)
    target_compile_definitions(tst_widget_lifetime PRIVATE HAVE_POPPLER=1)
    target_include_directories(tst_widget_lifetime PRIVATE ${POPPLER_INCLUDE_DIRS})
endif()
