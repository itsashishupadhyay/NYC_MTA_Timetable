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

# Define path to stops file as a compile-time macro
target_compile_definitions(mta
    PRIVATE
        DEBUG #TODO: Comment out
        MTA_STOP_DETAILS="${CMAKE_SOURCE_DIR}/info/stops.txt"
        MTA_TRANSFERS="${CMAKE_SOURCE_DIR}/info/transfers.txt"
        MTA_FACTS="${CMAKE_SOURCE_DIR}/info/MTA_facts.txt"
)

if (MSVC)
    target_compile_options(mta PRIVATE /W4 /permissive-)
else()
    target_compile_options(mta PRIVATE -Wall -Wextra -Wpedantic)
endif()
