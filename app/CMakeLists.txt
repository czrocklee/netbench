# Receiver targets
set(RECEIVER_SOURCES
    receiver/main.cpp
    receiver/worker.cpp
)

add_executable(uring_receiver ${RECEIVER_SOURCES})
target_compile_definitions(uring_receiver PRIVATE IO_URING_API)
target_link_libraries(uring_receiver PRIVATE uring_lib utility_lib CLI11::CLI11)

add_executable(asio_receiver ${RECEIVER_SOURCES})
target_compile_definitions(asio_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11)

add_executable(asio_uring_receiver ${RECEIVER_SOURCES})
target_compile_definitions(asio_uring_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(asio_uring_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11 uring)

add_executable(asio_ioctx_mt_receiver receiver/worker.cpp receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_ioctx_mt_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11)

add_executable(asio_uring_ioctx_mt_receiver receiver/worker.cpp receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_uring_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(asio_uring_ioctx_mt_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11 uring)

add_executable(bsd_receiver ${RECEIVER_SOURCES})
target_compile_definitions(bsd_receiver PRIVATE BSD_API)
target_link_libraries(bsd_receiver PRIVATE bsd_lib utility_lib CLI11::CLI11)

# Client targets
add_executable(client client/main.cpp client/connection.cpp client/sender.cpp)
target_link_libraries(client PRIVATE bsd_lib utility_lib CLI11::CLI11)


# Pingpong targets
set(PINGPONG_SOURCES
    pingpong/main.cpp
    pingpong/worker.cpp
)

add_executable(uring_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(uring_pingpong PRIVATE IO_URING_API)
target_link_libraries(uring_pingpong PRIVATE uring_lib utility_lib CLI11::CLI11)

add_executable(asio_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(asio_pingpong PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_pingpong PRIVATE rasio_lib utility_lib CLI11::CLI11)

add_executable(bsd_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(bsd_pingpong PRIVATE BSD_API)
target_link_libraries(bsd_pingpong PRIVATE bsd_lib utility_lib CLI11::CLI11)

