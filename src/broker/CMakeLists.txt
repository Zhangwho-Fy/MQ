add_library(mq_broker STATIC
    virtual_host.cpp
    broker.cpp
    password.cpp
)
target_include_directories(mq_broker PUBLIC "${MQ_INCLUDE_DIR}")
target_compile_features(mq_broker PUBLIC cxx_std_17)
target_compile_options(mq_broker PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
target_link_libraries(mq_broker PUBLIC SQLite::SQLite3)
target_link_libraries(mq_broker PUBLIC ${CMAKE_DL_LIBS})
