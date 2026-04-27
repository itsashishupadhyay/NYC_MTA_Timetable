cmake_minimum_required(VERSION 3.10)

project(MTA_TimeTable CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

add_executable(mta
    main.cpp
    source/server_pinger.cpp
    source/gtfs-realtime.pb.cc
    source/subway_feed_client.cpp
    source/stop_lookup.cpp
    source/time_helper.cpp
    source/cli_parser.cpp
    source/output_formatter.cpp
    source/route_planner.cpp
    source/mta_cache.cpp
)


target_include_directories(mta
    PRIVATE
        include
)


target_link_libraries(mta
    PRIVATE
        CURL::libcurl
        protobuf::libprotobuf
        absl::log
        absl::check
        absl::strings
        absl::base
)

# Bake static-data paths in as compile-time macros so the binary
# resolves them without any runtime path-discovery logic.
target_compile_definitions(mta
    PRIVATE
        MTA_STOP_DETAILS="${CMAKE_SOURCE_DIR}/info/stops.txt"
        MTA_TRANSFERS="${CMAKE_SOURCE_DIR}/info/transfers.txt"
        MTA_FACTS="${CMAKE_SOURCE_DIR}/info/MTA_facts.txt"
)

# Verbose feed-trace output (writes ./output.txt and prints a server-ping
# line). Off in release builds; turn on with `cmake -DMTA_DEBUG=ON ..`.
option(MTA_DEBUG "Enable verbose feed-trace and server-ping output" OFF)
if (MTA_DEBUG)
    target_compile_definitions(mta PRIVATE DEBUG)
endif()

if (MSVC)
    target_compile_options(mta PRIVATE /W4 /permissive-)
else()
    target_compile_options(mta PRIVATE -Wall -Wextra -Wpedantic)
endif()
