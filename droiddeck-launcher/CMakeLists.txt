cmake_minimum_required(VERSION 3.22)
project(kyty_droiddeck_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED_MAIN "${CMAKE_CURRENT_BINARY_DIR}/main_r7.cpp")
add_custom_command(
    OUTPUT "${GENERATED_MAIN}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_main.py" "${GENERATED_MAIN}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/generate_main.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/main_r7.cpp.gz.b64"
    VERBATIM
)

add_executable(kyty_droiddeck_launcher "${GENERATED_MAIN}")
target_link_libraries(kyty_droiddeck_launcher PRIVATE Qt6::Widgets)
