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

add_subdirectory(lib)

# --- Executables ---
# Define the executable targets and their sources
add_executable(bsd_server
    app/bsd/main.cpp
    app/bsd/receiver.cpp
    app/bsd/connection.cpp
)
target_link_libraries(bsd_server PRIVATE bsd_lib utility_lib CLI11::CLI11 Boost::headers)

add_executable(asio_client app/asio/asio_client.cpp)

add_executable(asio_perf app/asio/asio_perf_test.cpp)

add_executable(uring_server
    app/uring/main.cpp
    app/uring/receiver.cpp
)

target_link_libraries(uring_server PRIVATE uring_lib utility_lib Boost::headers)

add_executable(client app/client/main.cpp)
target_link_libraries(client PRIVATE bsd_lib utility_lib CLI11::CLI11)

add_executable(uring_client app/client/uring.cpp app/client/uring_sender.cpp)
target_link_libraries(uring_client PRIVATE uring_lib utility_lib CLI11::CLI11)



# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)