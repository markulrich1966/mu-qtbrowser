cmake_minimum_required(VERSION 3.16)
project(mu-transparency-test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(mu-transparency-test mu-transparency-test.cpp)
target_link_libraries(mu-transparency-test Qt5::Widgets)
