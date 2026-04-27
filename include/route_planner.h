#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <utility>

#include "subway_feed_client.h"
#include "stop_lookup.h"

// One leg of a journey (riding one train)
struct RouteSegment {
    std::string route_id;           // e.g. "A"
    std::string trip_id;
    std::string final_destination;  // last stop of this train (e.g. "Far Rockaway-Mott Av")
    std::string direction;          // "Northbound" / "Southbound"
    // Each stop: {station_name, "HH:MM"}
    std::vector<std::pair<std::string, std::string>> stops;
    std::chrono::system_clock::time_point board_time;
    std::chrono::system_clock::time_point alight_time;
};

// A complete route from source to destination
struct RoutePlan {
    std::vector<RouteSegment> segments;
    int total_minutes = 0;
    int num_transfers = 0;  // segments.size() - 1
};

// Find best routes from source to destination using real-time trip data.
// Graph nodes are *transfer-complex roots* (from transfers.txt) so two stops
// sharing a name (e.g. "86 St" in Brooklyn vs Manhattan) are NOT collapsed.
// Returns up to maxRoutes plans, sorted by (fewest transfers, then fastest).
std::vector<RoutePlan> findRoutes(
    const StationMatch& source,
    const StationMatch& dest,
    const std::vector<DetailedTripUpdate>& all_trips,
    const StopLookup& stops,
    std::chrono::system_clock::time_point now,
    int maxRoutes = 5);
