cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME NGPrinter)
project(${PROJECT_NAME} VERSION 1.1.0 LANGUAGES C CXX)

# 本地默认路径；CI 可用 -DCMAKE_PREFIX_PATH=... 覆盖
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/Qt/Qt5.6.3/5.6.3/mingw49_32")
endif()
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(QT_RESOURCE_FILES src/resources.qrc)

set(POPPLER_PATH ${PROJECT_SOURCE_DIR}/libs/Poppler)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport)

qt5_add_resources(RESOURCE_SOURCES ${QT_RESOURCE_FILES})

include_directories(
    ${POPPLER_PATH}/include
)

link_directories(
    ${POPPLER_PATH}/bin
)

add_executable(${PROJECT_NAME} WIN32
    src/httplib.h
    src/main.cpp
    src/app.rc
    ${QT_RESOURCE_FILES}
)

target_link_libraries(${PROJECT_NAME} 
    Qt5::Widgets
    Qt5::PrintSupport
    ws2_32
    libpoppler
    libpoppler-qt5
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_PREFIX_PATH}/bin/windeployqt.exe" "--compiler-runtime" "--release"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.exe"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${POPPLER_PATH}/bin" "${CMAKE_CURRENT_BINARY_DIR}/"
)