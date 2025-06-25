cmake_minimum_required(VERSION 3.10)
project(myproject)

# Add compiler flags if needed
set(CMAKE_CXX_STANDARD 20)  # Or whatever standard you're using

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer for Debug build")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

find_package(CLI11 REQUIRED)
find_package(Boost REQUIRED COMPONENTS headers)

# --- Libraries ---
add_library(metric_hud STATIC app/metric_hud.cpp)
target_include_directories(metric_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)

# --- Executables ---
# Define the executable targets and their sources
add_executable(bsd_server
    app/bsd/main.cpp
    app/bsd/receiver.cpp
    app/bsd/io_context.cpp
    app/bsd/acceptor.cpp
    app/bsd/connection.cpp
)
target_link_libraries(bsd_server PRIVATE metric_hud CLI11::CLI11 Boost::headers)

add_executable(asio_client app/asio/asio_client.cpp)

add_executable(asio_perf app/asio/asio_perf_test.cpp)

add_executable(uring_server
    app/uring/main.cpp
    app/uring/receiver.cpp
    app/uring/io_context.cpp
    app/uring/provided_buffer_pool.cpp
)

target_link_libraries(uring_server PRIVATE metric_hud Boost::headers)

add_executable(client app/client/main.cpp)
target_link_libraries(client PRIVATE metric_hud CLI11::CLI11)

# Link against liburing
find_library(LIBURING_LIBRARY uring)
if(LIBURING_LIBRARY)
    message(STATUS "Found liburing: ${LIBURING_LIBRARY}")
    target_link_libraries(uring_server PRIVATE ${LIBURING_LIBRARY})
else()
    message(FATAL_ERROR "liburing not found. Please install it (e.g., sudo apt-get install liburing-dev)")
endif()

# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)