

add_library(bsd_lib
    bsd/acceptor.cpp
    bsd/connector.cpp
    bsd/io_context.cpp
    bsd/socket.cpp
    bsd/receiver.cpp
    bsd/buffered_sender.cpp
)

target_include_directories(bsd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_library(uring_lib
    uring/acceptor.cpp
    uring/connector.cpp
    uring/io_context.cpp
    uring/provided_buffer_pool.cpp
    uring/registered_buffer_pool.cpp
    uring/receiver.cpp
    uring/bundle_receiver.cpp
    uring/bundle_sender.cpp
    uring/sender.cpp
)

target_include_directories(uring_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uring_lib PUBLIC bsd_lib uring)


set(RASIO_SOURCES
    rasio/connector.cpp 
    rasio/acceptor.cpp
    rasio/receiver.cpp
)


add_library(rasio_lib ${RASIO_SOURCES})
target_include_directories(rasio_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rasio_lib PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED)

add_library(rasio_uring_lib ${RASIO_SOURCES})
target_include_directories(rasio_uring_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rasio_uring_lib PUBLIC ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(rasio_uring_lib PUBLIC uring)

add_library(utility_lib
    utility/mmap_buffer_array.cpp
    utility/logger.cpp
    utility/machine_info.cpp
)

target_include_directories(utility_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
