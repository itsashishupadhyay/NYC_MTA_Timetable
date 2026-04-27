cmake_minimum_required(VERSION 3.10)

project(MTA_TimeTable VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(CURL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

# Where stops.txt / transfers.txt / MTA_facts.txt live at runtime.
# - Local builds default to the in-tree info/ dir so `./build/mta` just works.
# - Packagers (Homebrew, distro packages) override this at configure time:
#       cmake -DMTA_DATADIR=${CMAKE_INSTALL_FULL_DATAROOTDIR}/mta-timetable ...
#   The compile-time path is the fallback; the runtime env var MTA_DATADIR
#   takes precedence so the binary stays relocatable post-install.
set(MTA_DATADIR "${CMAKE_SOURCE_DIR}/info" CACHE PATH
    "Directory containing stops.txt, transfers.txt, MTA_facts.txt at runtime")

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
    source/line_metadata.cpp
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

target_compile_definitions(mta
    PRIVATE
        MTA_STOP_DETAILS="${MTA_DATADIR}/stops.txt"
        MTA_TRANSFERS="${MTA_DATADIR}/transfers.txt"
        MTA_FACTS="${MTA_DATADIR}/MTA_facts.txt"
        MTA_VERSION="${PROJECT_VERSION}"
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

# Install rules — what `cmake --install build --prefix=<x>` (and Homebrew) ships.
install(TARGETS mta RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install the static data files to the same dir the binary was compiled to
# look in (MTA_DATADIR). Skipped when MTA_DATADIR is still the in-tree default,
# since installing into the source tree is a no-op for local devs running
# ./build/mta directly. Packagers always pass an explicit MTA_DATADIR.
if (NOT MTA_DATADIR STREQUAL "${CMAKE_SOURCE_DIR}/info")
    install(FILES
            info/stops.txt
            info/transfers.txt
            info/MTA_facts.txt
        DESTINATION ${MTA_DATADIR})
endif()
