cmake_minimum_required(VERSION 3.16)

project(FyMQ VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MQ_BUILD_TESTS "Build unit and protocol tests" ON)
option(MQ_BUILD_LEGACY "Build the legacy Protobuf broker under legacy/" OFF)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
find_path(MQ_BOOST_INCLUDE_DIR boost/operators.hpp REQUIRED)

set(MQ_THIRD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mqthird")
set(MQ_THIRD_INCLUDE_DIR "${MQ_THIRD_DIR}/include")
set(MQ_THIRD_LIBRARY_DIR "${MQ_THIRD_DIR}/lib")

find_library(MQ_MUDUO_NET muduo_net PATHS "${MQ_THIRD_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
find_library(MQ_MUDUO_BASE muduo_base PATHS "${MQ_THIRD_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)

add_library(mq_compile_options INTERFACE)
target_compile_options(mq_compile_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_include_directories(mq_compile_options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${MQ_THIRD_INCLUDE_DIR}"
    "${MQ_BOOST_INCLUDE_DIR}"
)

add_library(mq_muduo INTERFACE)
target_include_directories(mq_muduo INTERFACE "${MQ_THIRD_INCLUDE_DIR}")
target_link_libraries(mq_muduo INTERFACE
    "${MQ_MUDUO_NET}"
    "${MQ_MUDUO_BASE}"
    Threads::Threads
    ZLIB::ZLIB
)

add_library(amqp091_protocol STATIC
    src/protocol/amqp091/frame_codec.cpp
    src/protocol/amqp091/fields.cpp
    src/protocol/amqp091/connection_methods.cpp
    src/protocol/amqp091/connection_session.cpp
    src/protocol/amqp091/channel_methods.cpp
    src/protocol/amqp091/exchange_methods.cpp
    src/protocol/amqp091/queue_methods.cpp
    src/protocol/amqp091/basic_methods.cpp
    src/protocol/amqp091/wire_reader.cpp
    src/protocol/amqp091/wire_writer.cpp
)
target_include_directories(amqp091_protocol PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(amqp091_protocol PUBLIC cxx_std_17)
target_compile_options(amqp091_protocol PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

add_library(mq_broker STATIC
    src/broker/virtual_host.cpp
)
target_include_directories(mq_broker PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(mq_broker PUBLIC cxx_std_17)
target_compile_options(mq_broker PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_link_libraries(mq_broker PUBLIC SQLite::SQLite3)

target_link_libraries(amqp091_protocol PUBLIC mq_broker)

add_executable(amqp_server
    src/amqp_server_main.cpp
    src/transport/amqp_connection.cpp
)
target_link_libraries(amqp_server PRIVATE
    mq_compile_options
    amqp091_protocol
    mq_muduo
)

if(MQ_BUILD_LEGACY)
    find_package(Protobuf 3.10 REQUIRED)
    find_package(SQLite3 REQUIRED)

    protobuf_generate_cpp(MQ_PROTO_SRCS MQ_PROTO_HDRS
        "${CMAKE_CURRENT_SOURCE_DIR}/legacy/mqcommon/mq_msg.proto"
        "${CMAKE_CURRENT_SOURCE_DIR}/legacy/mqcommon/mq_proto.proto"
    )
    add_library(mqcommon_proto STATIC ${MQ_PROTO_SRCS} ${MQ_PROTO_HDRS})
    target_link_libraries(mqcommon_proto PUBLIC protobuf::libprotobuf)
    target_include_directories(mqcommon_proto PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_link_libraries(mqcommon_proto PRIVATE mq_compile_options)

    add_executable(mqserver
        legacy/mqserver/mq_server.cc
        "${MQ_THIRD_INCLUDE_DIR}/muduo/proto/codec.cc"
    )
    target_link_libraries(mqserver PRIVATE
        mq_compile_options
        mqcommon_proto
        mq_muduo
        SQLite::SQLite3
    )

    foreach(client_target IN ITEMS publish_client consume_client)
        add_executable(${client_target}
            "legacy/mqclient/${client_target}.cc"
            "${MQ_THIRD_INCLUDE_DIR}/muduo/proto/codec.cc"
        )
        target_link_libraries(${client_target} PRIVATE
            mq_compile_options
            mqcommon_proto
            mq_muduo
        )
    endforeach()
endif()

if(MQ_BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_executable(frame_codec_test tests/protocol/frame_codec_test.cpp)
        target_link_libraries(frame_codec_test PRIVATE amqp091_protocol GTest::gtest)
        target_compile_features(frame_codec_test PRIVATE cxx_std_17)
        add_test(NAME frame_codec_test COMMAND frame_codec_test)

        add_executable(wire_types_test tests/protocol/wire_types_test.cpp)
        target_link_libraries(wire_types_test PRIVATE amqp091_protocol GTest::gtest)
        target_compile_features(wire_types_test PRIVATE cxx_std_17)
        add_test(NAME wire_types_test COMMAND wire_types_test)

        add_executable(fields_test tests/protocol/fields_test.cpp)
        target_link_libraries(fields_test PRIVATE amqp091_protocol GTest::gtest)
        target_compile_features(fields_test PRIVATE cxx_std_17)
        add_test(NAME fields_test COMMAND fields_test)

        add_executable(connection_methods_test
            tests/protocol/connection_methods_test.cpp)
        target_link_libraries(connection_methods_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(connection_methods_test PRIVATE cxx_std_17)
        add_test(NAME connection_methods_test COMMAND connection_methods_test)

        add_executable(connection_session_test
            tests/protocol/connection_session_test.cpp)
        target_link_libraries(connection_session_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(connection_session_test PRIVATE cxx_std_17)
        add_test(NAME connection_session_test COMMAND connection_session_test)

        add_executable(channel_methods_test
            tests/protocol/channel_methods_test.cpp)
        target_link_libraries(channel_methods_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(channel_methods_test PRIVATE cxx_std_17)
        add_test(NAME channel_methods_test COMMAND channel_methods_test)

        add_executable(exchange_methods_test
            tests/protocol/exchange_methods_test.cpp)
        target_link_libraries(exchange_methods_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(exchange_methods_test PRIVATE cxx_std_17)
        add_test(NAME exchange_methods_test COMMAND exchange_methods_test)

        add_executable(queue_methods_test
            tests/protocol/queue_methods_test.cpp)
        target_link_libraries(queue_methods_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(queue_methods_test PRIVATE cxx_std_17)
        add_test(NAME queue_methods_test COMMAND queue_methods_test)

        add_executable(basic_methods_test tests/protocol/basic_methods_test.cpp)
        target_link_libraries(basic_methods_test PRIVATE
            amqp091_protocol GTest::gtest)
        target_compile_features(basic_methods_test PRIVATE cxx_std_17)
        add_test(NAME basic_methods_test COMMAND basic_methods_test)

        add_executable(virtual_host_test tests/broker/virtual_host_test.cpp)
        target_link_libraries(virtual_host_test PRIVATE mq_broker GTest::gtest)
        target_compile_features(virtual_host_test PRIVATE cxx_std_17)
        add_test(NAME virtual_host_test COMMAND virtual_host_test)

        if(MQ_BUILD_LEGACY)
            set(MQ_GTEST_SOURCES
                mq_bindingtest
                mq_consumertest
                mq_exchangetest
                mq_hosttest
                mq_msgtest
                mq_queuetest
                mq_routetest
            )
            foreach(test_target IN LISTS MQ_GTEST_SOURCES)
                add_executable(${test_target}
                    "legacy/mqtest/${test_target}.cpp")
                target_link_libraries(${test_target} PRIVATE
                    mq_compile_options
                    mqcommon_proto
                    mq_muduo
                    SQLite::SQLite3
                    GTest::gtest
                )
                add_test(NAME ${test_target} COMMAND ${test_target})
            endforeach()

            add_executable(mq_filetest legacy/mqtest/mq_filetest.cpp)
            target_link_libraries(mq_filetest PRIVATE mq_compile_options)
            add_test(NAME mq_filetest COMMAND mq_filetest)
            set_tests_properties(${MQ_GTEST_SOURCES} mq_filetest PROPERTIES
                WORKING_DIRECTORY
                    "${CMAKE_CURRENT_SOURCE_DIR}/legacy/mqtest"
            )
        endif()
    else()
        message(WARNING "GTest was not found; protocol tests are skipped")
    endif()
endif()
