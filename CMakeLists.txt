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

# --- Libraries ---
add_library(metric_hud STATIC app/metric_hud.cpp)
target_include_directories(metric_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)

# --- Executables ---
# Define the executable targets and their sources
add_executable(bsd_receiver app/bsd/receiver.cpp)

add_executable(asio_client app/asio/asio_client.cpp)

add_executable(asio_perf app/asio/asio_perf_test.cpp)

add_executable(iouring_server app/iouring/main.cpp app/iouring/provided_buffer_pool.cpp)
target_link_libraries(iouring_server PRIVATE uring metric_hud)

add_executable(client app/client/main.cpp)
target_link_libraries(client PRIVATE metric_hud CLI11::CLI11)

# Link against liburing

# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)