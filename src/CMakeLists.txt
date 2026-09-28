add_subdirectory(broker)
add_subdirectory(protocol/amqp091)
add_subdirectory(cli)
add_subdirectory(transport)

add_executable(amqp_server amqp_server_main.cpp)
set_target_properties(amqp_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(amqp_server PRIVATE
    mq_compile_options
    mq_transport
    mq_cli
)
