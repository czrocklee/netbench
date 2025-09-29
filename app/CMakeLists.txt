# Generate build info header
include(CheckIncludeFile)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} describe --always --dirty
        OUTPUT_VARIABLE GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse HEAD
        OUTPUT_VARIABLE GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
else()
    set(GIT_DESCRIBE "unknown")
    set(GIT_COMMIT "unknown")
endif()

if(CMAKE_CONFIGURATION_TYPES)
    set(BUILD_TYPE "$<CONFIG>")
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()
    set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

set(CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(CXX_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
string(TIMESTAMP BUILD_TIMESTAMP_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

configure_file(common/build_info.hpp.in
    ${CMAKE_BINARY_DIR}/generated/build_info.hpp @ONLY)
include_directories(${CMAKE_BINARY_DIR}/generated)

add_library(common_lib STATIC
    common/utils.cpp
    common/metric_hud.cpp
)
target_include_directories(common_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
find_package(Boost REQUIRED COMPONENTS chrono)
target_link_libraries(common_lib PRIVATE utility_lib hdr_histogram::hdr_histogram Boost::chrono)


# Receiver targets
set(RECEIVER_SOURCES
    receiver/main.cpp
    receiver/worker.cpp
)

add_executable(uring_receiver ${RECEIVER_SOURCES})
target_compile_definitions(uring_receiver PRIVATE IO_URING_API)
target_link_libraries(uring_receiver PRIVATE common_lib uring_lib utility_lib CLI11::CLI11)

add_executable(asio_receiver ${RECEIVER_SOURCES})
target_compile_definitions(asio_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_receiver PRIVATE common_lib rasio_lib utility_lib CLI11::CLI11)

add_executable(asio_uring_receiver ${RECEIVER_SOURCES})
target_compile_definitions(asio_uring_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(asio_uring_receiver PRIVATE common_lib rasio_lib utility_lib CLI11::CLI11 uring)

add_executable(asio_ioctx_mt_receiver receiver/worker.cpp receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_ioctx_mt_receiver PRIVATE common_lib rasio_lib utility_lib CLI11::CLI11)

add_executable(asio_uring_ioctx_mt_receiver receiver/worker.cpp receiver/asio_ioctx_mt.cpp)
target_compile_definitions(asio_uring_ioctx_mt_receiver PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
target_link_libraries(asio_uring_ioctx_mt_receiver PRIVATE common_lib rasio_lib utility_lib CLI11::CLI11 uring)

add_executable(bsd_receiver ${RECEIVER_SOURCES})
target_compile_definitions(bsd_receiver PRIVATE BSD_API)
target_link_libraries(bsd_receiver PRIVATE common_lib bsd_lib utility_lib CLI11::CLI11)

# Client targets
add_executable(client client/main.cpp client/connection.cpp client/sender.cpp)
target_link_libraries(client PRIVATE common_lib bsd_lib utility_lib CLI11::CLI11)


# Pingpong targets
set(PINGPONG_SOURCES
    pingpong/main.cpp
    pingpong/worker.cpp
)

add_executable(uring_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(uring_pingpong PRIVATE IO_URING_API)
target_link_libraries(uring_pingpong PRIVATE common_lib uring_lib utility_lib CLI11::CLI11)

add_executable(asio_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(asio_pingpong PRIVATE ASIO_API ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio_pingpong PRIVATE common_lib rasio_lib utility_lib CLI11::CLI11)

add_executable(bsd_pingpong ${PINGPONG_SOURCES})
target_compile_definitions(bsd_pingpong PRIVATE BSD_API)
target_link_libraries(bsd_pingpong PRIVATE common_lib bsd_lib utility_lib CLI11::CLI11)

