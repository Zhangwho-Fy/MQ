add_library(mq_transport STATIC
    amqp_connection.cpp
)
target_include_directories(mq_transport PUBLIC "${MQ_INCLUDE_DIR}")
target_compile_features(mq_transport PUBLIC cxx_std_17)
target_link_libraries(mq_transport PRIVATE mq_compile_options)
target_link_libraries(mq_transport PUBLIC
    amqp091_protocol
    mq_muduo
)
