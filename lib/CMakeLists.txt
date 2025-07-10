

add_library(bsd_lib
    bsd/acceptor.cpp
    bsd/io_context.cpp
    bsd/socket.cpp
    bsd/receiver.cpp
)

target_include_directories(bsd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_library(uring_lib
    uring/acceptor.cpp
    uring/io_context.cpp
    uring/provided_buffer_pool.cpp
    uring/receiver.cpp
    uring/bundle_receiver.cpp
)

target_include_directories(uring_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uring_lib PUBLIC bsd_lib uring)


add_library(utility_lib STATIC utility/metric_hud.cpp)
target_include_directories(utility_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
