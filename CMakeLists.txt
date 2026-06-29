cmake_minimum_required(VERSION 3.16)
project(LoRaTemperature VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_COMPONENTS Core Gui Widgets SerialPort SerialBus Charts Test)
find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

qt_standard_project_setup()

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
