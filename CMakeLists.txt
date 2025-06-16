cmake_minimum_required(VERSION 3.10)
project(myproject)

# Add compiler flags if needed
set(CMAKE_CXX_STANDARD 20)  # Or whatever standard you're using
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

find_package(CLI11 REQUIRED)

# Define the executable targets and their sources
add_executable(asio_client app/asio/asio_client.cpp)

add_executable(asio_perf app/asio/asio_perf_test.cpp)

add_executable(asio_perf_test app/asio/asio_perf_test.cpp)

add_executable(echo app/iouring/echo.c)
target_link_libraries(echo uring)


add_executable(client app/client/main.cpp)

# Link against liburing

# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)