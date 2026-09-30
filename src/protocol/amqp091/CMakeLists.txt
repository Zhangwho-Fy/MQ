add_library(amqp091_protocol STATIC
    frame_codec.cpp
    fields.cpp
    connection_methods.cpp
    connection_session.cpp
    session_connection.cpp
    session_channel.cpp
    session_exchange.cpp
    session_queue.cpp
    session_basic.cpp
    session_confirm.cpp
    channel_methods.cpp
    exchange_methods.cpp
    queue_methods.cpp
    basic_methods.cpp
    confirm_methods.cpp
    wire_reader.cpp
    wire_writer.cpp
)
target_include_directories(amqp091_protocol PUBLIC "${MQ_INCLUDE_DIR}")
target_compile_features(amqp091_protocol PUBLIC cxx_std_17)
target_compile_options(amqp091_protocol PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_link_libraries(amqp091_protocol PUBLIC mq_broker)
