cmake_minimum_required(VERSION 3.10)
project(myproject)

option(SPDLOG_LOGGER "Enable spdlog as logger backend" OFF)
option(STDERR_LOGGER "Enable stderr as logger backend" OFF)

if(SPDLOG_LOGGER AND STDERR_LOGGER)
    message(FATAL_ERROR "Only one logger backend can be enabled at a time: SPDLOG_LOGGER or STDERR_LOGGER")
elseif(SPDLOG_LOGGER)
    add_compile_definitions(USE_SPDLOG_LOGGER SPDLOG_USE_STD_FORMAT)
elseif(STDERR_LOGGER)
    add_compile_definitions(USE_STDERR_LOGGER)
else()
    # No logger macro defined, falls back to null logger
endif()

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
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(app)
add_subdirectory(lib)


# For example, if you're using Boost.Asio:
# find_package(Boost REQUIRED COMPONENTS system)
# target_link_libraries(asio_client Boost::system)
# (and similar for other targets that use asio or other libraries)

add_executable(syscall_overhead tool/syscall_overhead.c)
add_executable(zerocopy_send tool/zerocopy_send.c)