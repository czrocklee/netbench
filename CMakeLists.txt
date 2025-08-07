cmake_minimum_required(VERSION 3.10)
project(myproject)

# Add compiler flags if needed
set(CMAKE_CXX_STANDARD 23)  # Or whatever standard you're using

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer for Debug build")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

find_package(CLI11 REQUIRED)
find_package(Boost REQUIRED COMPONENTS headers)
find_package(hdr_histogram REQUIRED)
find_package(magic_enum REQUIRED)

add_subdirectory(lib)

set(RECEIVER_SOURCES
    app/receiver/main.cpp
    app/receiver/worker.cpp
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

add_executable(asio_ioctx_mt_receiver app/receiver/worker.cpp app/receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED )
target_link_libraries(asio_ioctx_mt_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11)

add_executable(asio_uring_ioctx_mt_receiver app/receiver/worker.cpp app/receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_uring_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(asio_uring_ioctx_mt_receiver PRIVATE rasio_lib utility_lib CLI11::CLI11 uring)

add_executable(bsd_receiver ${RECEIVER_SOURCES})
target_compile_definitions(bsd_receiver PRIVATE BSD_API)
target_link_libraries(bsd_receiver PRIVATE bsd_lib utility_lib CLI11::CLI11)

#add_executable(asio_client app/asio/asio_client.cpp)

#add_executable(asio_perf app/asio/asio_perf_test.cpp)

#add_executable(uring_server
#    app/receiver/main.cpp
#   app/receiver/worker.cpp
#)

#target_link_libraries(uring_server PRIVATE uring_lib utility_lib Boost::headers)

add_executable(client app/client/main.cpp)
target_link_libraries(client PRIVATE bsd_lib utility_lib CLI11::CLI11)

add_executable(uring_client app/client/uring.cpp app/client/uring_sender.cpp)
target_link_libraries(uring_client PRIVATE uring_lib utility_lib CLI11::CLI11)

add_executable(syscall_overhead tool/syscall_overhead.c)

set(PINGPONG_SOURCES
    app/pingpong/main.cpp
    app/pingpong/worker.cpp
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


# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)
