

add_library(bsd_lib
    bsd/acceptor.cpp
    bsd/connector.cpp
    bsd/io_context.cpp
    bsd/socket.cpp
    bsd/receiver.cpp
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
)

target_include_directories(uring_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uring_lib PUBLIC bsd_lib uring)


add_library(rasio_lib 
    rasio/connector.cpp 
    rasio/acceptor.cpp
)

target_include_directories(rasio_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_library(utility_lib STATIC utility/metric_hud.cpp)
target_include_directories(utility_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(utility_lib PUBLIC hdr_histogram::hdr_histogram)
