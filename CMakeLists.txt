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

add_subdirectory(app)
add_subdirectory(lib)


# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)

add_executable(syscall_overhead tool/syscall_overhead.c)
add_executable(zerocopy_send tool/zerocopy_send.c)