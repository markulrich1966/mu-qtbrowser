cmake_minimum_required(VERSION 3.16)
project(mu-qtbrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Version: Default 0.0.8 (wie VERSION ?= 0.0.8 im Makefile),
# ueberschreibbar mit -DMU_VERSION=1.2.3 beim Bauen.
if(NOT MU_VERSION)
  set(MU_VERSION "0.0.8")
endif()
add_compile_definitions(MU_VERSION="${MU_VERSION}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebEngineWidgets WebEngineCore)
find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED x11)

# Main IPC Browser (ohne Tabs)
add_executable(mu-browser main.cpp)
target_link_libraries(mu-browser PRIVATE Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebEngineCore ${X11_LIBRARIES})

# Low-RAM Tabbed Browser (MAX_TABS 10)
add_executable(mu-browser-tabs main_tabs.cpp)
target_link_libraries(mu-browser-tabs PRIVATE Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebEngineCore ${X11_LIBRARIES})

# Unrestricted Desktop Browser (ohne Tab-Limit, mit Passwortspeicherung)
add_executable(mu-browser-desktop main_desktop.cpp)
target_link_libraries(mu-browser-desktop PRIVATE Qt6::Core Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebEngineCore ${X11_LIBRARIES})