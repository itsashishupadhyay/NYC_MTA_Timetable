#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include "mta_subway_feed.h"

// Existing simple struct (keep it if you’re already using it)
struct TripUpdateEntry {
    std::string route_id;   // e.g. "A", "C", "E"
    std::string trip_id;    // GTFS trip ID
    std::string stop_id;    // GTFS stop ID

    std::chrono::system_clock::time_point arrival_time;
    std::chrono::system_clock::time_point departure_time;
};

// ---- New richer data model ----

enum class TripScheduleRelationship : int {
    UNKNOWN      = -1,
    SCHEDULED    = 0,
    ADDED        = 1,
    UNSCHEDULED  = 2,
    CANCELED     = 3
};

enum class StopTimeScheduleRelationship : int {
    UNKNOWN   = -1,
    SCHEDULED = 0,
    SKIPPED   = 1,
    NO_DATA   = 2
};

struct TripDescriptorInfo {
    std::string trip_id;     // TripUpdate.trip.trip_id
    std::string route_id;    // TripUpdate.trip.route_id

    // Optional: GTFS direction_id (0 or 1)
    std::optional<int> direction_id;

    // As in the GTFS-RT spec: "HH:MM:SS" and "YYYYMMDD"
    std::optional<std::string> start_time;
    std::optional<std::string> start_date;

    TripScheduleRelationship schedule_relationship = TripScheduleRelationship::UNKNOWN;
};

struct StopTimePrediction {
    std::string stop_id;                 // stop_time_update.stop_id
    std::optional<int> stop_sequence;    // stop_time_update.stop_sequence

    std::optional<std::chrono::system_clock::time_point> arrival_time;
    std::optional<std::chrono::system_clock::time_point> departure_time;

    std::optional<int> arrival_delay;    // seconds vs schedule
    std::optional<int> departure_delay;  // seconds vs schedule

    StopTimeScheduleRelationship schedule_relationship = StopTimeScheduleRelationship::UNKNOWN;
};

struct DetailedTripUpdate {
    TripDescriptorInfo trip;
    std::vector<StopTimePrediction> stop_times;
};

std::string stopStatusToString(StopTimeScheduleRelationship r);
std::string stationName(const std::string& stop_id);
std::string directionToString(const DetailedTripUpdate& t);

class SubwayFeedClient {
public:
    // URL comes from your macro 
    explicit SubwayFeedClient(const std::string& url = MTA_SUBWAY_FEED_URL_ACE);
    std::vector<TripUpdateEntry> fetchTripUpdates();
    std::vector<DetailedTripUpdate> fetchDetailedTripUpdates();

private:
    std::string url_;
    // Download raw protobuf bytes from the GTFS-RT endpoint using libcurl
    std::string downloadFeed() const;
    void writeDebugOutput(const std::vector<DetailedTripUpdate>& trips) const;
};
