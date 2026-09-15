cmake_minimum_required(VERSION 3.25)
project(cortex_common_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(systick_reading_test systick_reading_test.cpp)
target_include_directories(systick_reading_test PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_options(systick_reading_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME systick_reading_test COMMAND systick_reading_test)
