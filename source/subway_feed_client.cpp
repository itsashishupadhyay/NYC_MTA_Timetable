#include "subway_feed_client.h"
#include <fstream>
#include <curl/curl.h>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <chrono>
#include <ctime>
#include "time_helper.h"

#include "gtfs-realtime.pb.h" // generated protobuf header

namespace {

// libcurl write callback: append incoming bytes to a std::string
size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(ptr, size * nmemb);
    return size * nmemb;
}

// Time conversions moved to TimeHelper

TripScheduleRelationship mapTripScheduleRelationship(int value) {
    switch (value) {
        case 0: return TripScheduleRelationship::SCHEDULED;
        case 1: return TripScheduleRelationship::ADDED;
        case 2: return TripScheduleRelationship::UNSCHEDULED;
        case 3: return TripScheduleRelationship::CANCELED;
        default: return TripScheduleRelationship::UNKNOWN;
    }
}

StopTimeScheduleRelationship mapStopTimeScheduleRelationship(int value) {
    switch (value) {
        case 0: return StopTimeScheduleRelationship::SCHEDULED;
        case 1: return StopTimeScheduleRelationship::SKIPPED;
        case 2: return StopTimeScheduleRelationship::NO_DATA;
        default: return StopTimeScheduleRelationship::UNKNOWN;
    }
}

} // namespace

// Moved from header: implement utility functions here
// moved to TimeHelper

// moved to TimeHelper

// moved to TimeHelper

void SubwayFeedClient::writeDebugOutput(const std::vector<DetailedTripUpdate>& trips) const {
#ifdef DEBUG
    std::ofstream out("output.txt");
    if (!out.is_open()) return;

    for (const auto& t : trips) {
        out << "Route " << t.trip.route_id
            << " | Trip " << t.trip.trip_id;

        if (t.trip.direction_id.has_value()) {
            out << " | " << (t.trip.direction_id.value() == 0 ? "Northbound" : "Southbound");
        } else {
            out << " | UnknownDirection";
        }

        out << "\n";

        for (const auto& st : t.stop_times) {
            out << "  Stop: " << st.stop_id;

            if (st.stop_sequence.has_value())
                out << ", Sequence: " << st.stop_sequence.value();

            // Arrival time
            if (st.arrival_time.has_value()) {
                std::time_t t_arr = std::chrono::system_clock::to_time_t(st.arrival_time.value());
                out << ", Arrive: " << std::ctime(&t_arr);
                out.seekp(-1, std::ios_base::end); // remove newline from ctime
            }

            // Departure time
            if (st.departure_time.has_value()) {
                std::time_t t_dep = std::chrono::system_clock::to_time_t(st.departure_time.value());
                out << ", Depart: " << std::ctime(&t_dep);
                out.seekp(-1, std::ios_base::end);
            }

            // Delay
            if (st.arrival_delay.has_value())
                out << ", ArrivalDelay: " << st.arrival_delay.value();
            if (st.departure_delay.has_value())
                out << ", DepartureDelay: " << st.departure_delay.value();

            // Status
            out << ", Status: ";
            switch (st.schedule_relationship) {
                case StopTimeScheduleRelationship::SCHEDULED: out << "SCHEDULED"; break;
                case StopTimeScheduleRelationship::SKIPPED:   out << "SKIPPED"; break;
                case StopTimeScheduleRelationship::NO_DATA:   out << "NO_DATA"; break;
                default: out << "UNKNOWN"; break;
            }

            out << "\n";
        }

        out << "--------------------------------------------------\n";
    }
#endif
}


SubwayFeedClient::SubwayFeedClient(const std::string& url)
    : url_(url) {}

std::string SubwayFeedClient::downloadFeed() const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("CURL error: ") +
                                 curl_easy_strerror(res));
    }
    if (http_code < 200 || http_code >= 300) {
        throw std::runtime_error("HTTP error: " + std::to_string(http_code));
    }

    return response;
}

// Your “old” simple method can still exist unchanged
std::vector<TripUpdateEntry> SubwayFeedClient::fetchTripUpdates() {
    std::string data = downloadFeed();

    transit_realtime::FeedMessage feed;
    if (!feed.ParseFromString(data)) {
        throw std::runtime_error("Failed to parse GTFS-realtime protobuf feed");
    }

    std::vector<TripUpdateEntry> result;

    for (const auto& entity : feed.entity()) {
        if (!entity.has_trip_update()) {
            continue;
        }

        const auto& tripUpdate = entity.trip_update();

        TripUpdateEntry baseEntry;

        if (tripUpdate.trip().has_route_id()) {
            baseEntry.route_id = tripUpdate.trip().route_id();
        }

        if (tripUpdate.trip().has_trip_id()) {
            baseEntry.trip_id = tripUpdate.trip().trip_id();
        }

        for (const auto& stu : tripUpdate.stop_time_update()) {
            TripUpdateEntry entry = baseEntry;

            if (stu.has_stop_id()) {
                entry.stop_id = stu.stop_id();
            }

            if (stu.has_arrival() && stu.arrival().has_time()) {
                entry.arrival_time = TimeHelper::unixToTimePoint(stu.arrival().time());
            }

            if (stu.has_departure() && stu.departure().has_time()) {
                entry.departure_time = TimeHelper::unixToTimePoint(stu.departure().time());
            }

            result.push_back(std::move(entry));
        }
    }

    return result;
}

// 🔥 New rich parser
std::vector<DetailedTripUpdate> SubwayFeedClient::fetchDetailedTripUpdates() {
    // 1. Download raw GTFS-realtime protobuf bytes
    std::string data = downloadFeed();

    // 2. Parse protobuf FeedMessage
    transit_realtime::FeedMessage feed;
    if (!feed.ParseFromString(data)) {
        throw std::runtime_error("Failed to parse GTFS-realtime protobuf feed");
    }

    std::vector<DetailedTripUpdate> result;

    // 3. Iterate all entities and extract TripUpdates
    for (const auto& entity : feed.entity()) {
        if (!entity.has_trip_update()) {
            continue;
        }

        const auto& tripUpdate = entity.trip_update();
        const auto& tripDesc   = tripUpdate.trip();

        DetailedTripUpdate detailed;
        TripDescriptorInfo& tripInfo = detailed.trip;

        // TripDescriptor: trip_id, route_id
        if (tripDesc.has_trip_id()) {
            tripInfo.trip_id = tripDesc.trip_id();
        }

        if (tripDesc.has_route_id()) {
            tripInfo.route_id = tripDesc.route_id();
        }

        // Optional direction_id (0 or 1)
        if (tripDesc.has_direction_id()) {
            tripInfo.direction_id = tripDesc.direction_id();
        }

        // Optional start_time ("HH:MM:SS") and start_date ("YYYYMMDD")
        if (tripDesc.has_start_time()) {
            tripInfo.start_time = tripDesc.start_time();
        }

        if (tripDesc.has_start_date()) {
            tripInfo.start_date = tripDesc.start_date();
        }

        // Trip-level schedule relationship
        tripInfo.schedule_relationship =
            mapTripScheduleRelationship(static_cast<int>(tripDesc.schedule_relationship()));

        // StopTimeUpdates
        for (const auto& stu : tripUpdate.stop_time_update()) {
            StopTimePrediction st;

            if (stu.has_stop_id()) {
                st.stop_id = stu.stop_id();
            }

            if (stu.has_stop_sequence()) {
                st.stop_sequence = stu.stop_sequence();
            }

            // Arrival time/delay
            if (stu.has_arrival()) {
                const auto& arr = stu.arrival();
                if (arr.has_time()) {
                    st.arrival_time = TimeHelper::unixToTimePoint(arr.time());
                }
                if (arr.has_delay()) {
                    st.arrival_delay = arr.delay();
                }
            }

            // Departure time/delay
            if (stu.has_departure()) {
                const auto& dep = stu.departure();
                if (dep.has_time()) {
                    st.departure_time = TimeHelper::unixToTimePoint(dep.time());
                }
                if (dep.has_delay()) {
                    st.departure_delay = dep.delay();
                }
            }

            // Stop-level schedule relationship
            st.schedule_relationship =
                mapStopTimeScheduleRelationship(static_cast<int>(stu.schedule_relationship()));

            detailed.stop_times.push_back(std::move(st));
        }

        result.push_back(std::move(detailed));
    }
    writeDebugOutput(result);
    return result;
}

// Utility translations moved from header to implementation
std::string stopStatusToString(StopTimeScheduleRelationship r) {
    switch (r) {
        case StopTimeScheduleRelationship::SCHEDULED: return "SCHEDULED";
        case StopTimeScheduleRelationship::SKIPPED:   return "SKIPPED";
        case StopTimeScheduleRelationship::NO_DATA:   return "NO_DATA";
        default: return "UNKNOWN";
    }
}

std::string stationName(const std::string& stop_id) {
    // TODO: map from stops.txt file
    (void)stop_id;
    return "";   // returns empty → prints nothing
}

std::string directionToString(const DetailedTripUpdate& t) {
    // 1) Prefer direction_id if present
    if (t.trip.direction_id.has_value()) {
        return t.trip.direction_id.value() == 0 ? "Northbound" : "Southbound";
    }

    // 2) Fallback: infer from trip_id pattern, e.g. "014950_A..S74R"
    const std::string& id = t.trip.trip_id;
    if (!id.empty()) {
        // Look for the ".." marker that precedes the direction letter
        auto pos = id.find("..");
        if (pos != std::string::npos && pos + 2 < id.size()) {
            char dir = id[pos + 2];  // char right after ".."
            if (dir == 'N') return "Northbound";
            if (dir == 'S') return "Southbound";
        }

        // Extra fallback: if the *last* char is N/S and there was no ".."
        char last = id.back();
        if (last == 'N') return "Northbound";
        if (last == 'S') return "Southbound";
    }

    return "Unknown Direction";
}

