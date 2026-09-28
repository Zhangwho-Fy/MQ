add_library(mq_cli STATIC
    options.cpp
    users.cpp
)
target_include_directories(mq_cli PUBLIC "${MQ_INCLUDE_DIR}")
target_compile_features(mq_cli PUBLIC cxx_std_17)
target_compile_options(mq_cli PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)
