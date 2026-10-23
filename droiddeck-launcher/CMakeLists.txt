cmake_minimum_required(VERSION 3.22)
project(kyty_droiddeck_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(kyty_droiddeck_launcher main.cpp)
target_link_libraries(kyty_droiddeck_launcher PRIVATE Qt6::Widgets)
