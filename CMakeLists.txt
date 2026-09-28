cmake_minimum_required(VERSION 3.16)

project(FyMQ VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MQ_BUILD_TESTS "Build unit and protocol tests" ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
find_path(MQ_BOOST_INCLUDE_DIR boost/operators.hpp REQUIRED)

set(MQ_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(MQ_THIRD_DIR "${PROJECT_SOURCE_DIR}/mqthird")
set(MQ_THIRD_INCLUDE_DIR "${MQ_THIRD_DIR}/include")
set(MQ_THIRD_LIBRARY_DIR "${MQ_THIRD_DIR}/lib")

find_library(MQ_MUDUO_NET muduo_net PATHS "${MQ_THIRD_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
find_library(MQ_MUDUO_BASE muduo_base PATHS "${MQ_THIRD_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
find_library(MQ_MUDUO_HTTP muduo_http PATHS "${MQ_THIRD_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)

add_library(mq_compile_options INTERFACE)
target_compile_options(mq_compile_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_include_directories(mq_compile_options INTERFACE
    "${PROJECT_SOURCE_DIR}"
    "${MQ_THIRD_INCLUDE_DIR}"
    "${MQ_BOOST_INCLUDE_DIR}"
)

add_library(mq_muduo INTERFACE)
target_include_directories(mq_muduo INTERFACE "${MQ_THIRD_INCLUDE_DIR}")
target_link_libraries(mq_muduo INTERFACE
    "${MQ_MUDUO_NET}"
    "${MQ_MUDUO_BASE}"
    "${MQ_MUDUO_HTTP}"
    Threads::Threads
    ZLIB::ZLIB
)

add_subdirectory(src)

if(MQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
