#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>

#include "server_pinger.h"
#include "subway_feed_client.h"
#include "mta_subway_feed.h"
#include "gtfs-realtime.pb.h"
#include "stop_lookup.h"
#include "time_helper.h"
#include "cli_parser.h"
#include "output_formatter.h"
#include "route_planner.h"
#include "ansi_colors.h"
#include "mta_cache.h"


// Interactive station picker — shows options and lets user choose
// Returns the selected StationMatch, or exits if nothing matches.
static StationMatch pickStation(
    const std::string& query,
    const std::string& label, // "Source" or "Destination"
    const StopLookup& stops)
{
    auto matches = stops.fuzzyMatch(query);

    if (matches.empty()) {
        std::cerr << ansi::RED << "No station found matching '" << query << "'"
                  << ansi::RESET << "\n";
        std::exit(1);
    }

    // Exact single match — no need to ask
    if (matches.size() == 1 && matches[0].score < 100) {
        return matches[0];
    }

    // Determine if these are fuzzy (typo) suggestions vs substring matches
    bool isFuzzy = (matches[0].score >= 100);

    if (isFuzzy) {
        std::cout << ansi::YELLOW << label << ": No exact match for '" << query
                  << "'. Did you mean?" << ansi::RESET << "\n";
    } else if (matches.size() > 1) {
        std::cout << ansi::YELLOW << label << ": Multiple stations match '" << query
                  << "':" << ansi::RESET << "\n";
    } else {
        // Single substring match
        return matches[0];
    }

    // When two or more matches have the same display name (e.g. "7 Av" can
    // mean three physically-different stations), append a borough hint —
    // and, if the borough alone doesn't disambiguate, the parent stop_id —
    // so the user can actually tell them apart.
    std::vector<std::string> hints(matches.size());
    {
        std::unordered_map<std::string, int> nameCount;
        for (const auto& m : matches) nameCount[m.stop_name]++;
        std::unordered_map<std::string, int> nameBoroughCount;
        for (const auto& m : matches) {
            std::string b = stops.boroughFor(m.base_stop_id);
            nameBoroughCount[m.stop_name + "|" + b]++;
        }
        for (size_t i = 0; i < matches.size(); i++) {
            const auto& m = matches[i];
            if (nameCount[m.stop_name] <= 1) continue;
            std::string b = stops.boroughFor(m.base_stop_id);
            std::ostringstream h;
            h << "  " << ansi::DIM << "(";
            if (!b.empty()) h << b;
            else            h << "?";
            // Same name AND same borough — surface the parent_stop_id so the
            // two entries don't look identical in the picker.
            if (nameBoroughCount[m.stop_name + "|" + b] > 1) {
                h << " · " << m.base_stop_id;
            }
            h << ")" << ansi::RESET;
            hints[i] = h.str();
        }
    }

    for (size_t i = 0; i < matches.size(); i++) {
        std::cout << "  " << ansi::BOLD << (i + 1) << ansi::RESET
                  << ". " << matches[i].stop_name << hints[i] << "\n";
    }

    // Prompt user for selection
    std::cout << ansi::DIM << "Enter choice [1-" << matches.size() << "] (default 1): "
              << ansi::RESET;
    std::cout.flush();

    std::string input;
    if (std::getline(std::cin, input) && !input.empty()) {
        // Trim whitespace
        size_t start = input.find_first_not_of(" \t");
        size_t end = input.find_last_not_of(" \t");
        if (start != std::string::npos) {
            input = input.substr(start, end - start + 1);
        }

        if (!input.empty()) {
            try {
                int choice = std::stoi(input);
                if (choice >= 1 && choice <= (int)matches.size()) {
                    std::cout << ansi::DIM << "Selected: " << matches[choice - 1].stop_name
                              << ansi::RESET << "\n\n";
                    return matches[choice - 1];
                }
            } catch (...) {
                // Invalid input, fall through to default
            }
            std::cerr << ansi::RED << "Invalid choice. Using default."
                      << ansi::RESET << "\n";
        }
    }

    std::cout << ansi::DIM << "Selected: " << matches[0].stop_name
              << ansi::RESET << "\n\n";
    return matches[0];
}

// Interactive prediction picker — shows cached route predictions
// Returns the selected Prediction, or exits if no history.
static Prediction pickPrediction(const MtaCache& cache) {
    auto predictions = cache.predict(5);

    if (predictions.empty()) {
        std::cerr << ansi::RED << "No travel history found. Use --source to specify a station."
                  << ansi::RESET << "\n";
        std::exit(1);
    }

    std::cout << ansi::BOLD << "Based on your travel history:" << ansi::RESET << "\n";
    for (size_t i = 0; i < predictions.size(); i++) {
        const auto& p = predictions[i];
        std::cout << "  " << ansi::BOLD << (i + 1) << ansi::RESET << ". ";
        if (p.destination.empty()) {
            std::cout << p.source;
        } else {
            std::cout << p.source << " -> " << p.destination;
        }
        std::cout << ansi::DIM << "  (" << p.reason << ")" << ansi::RESET << "\n";
    }

    std::cout << ansi::DIM << "Enter choice [1-" << predictions.size() << "] (default 1): "
              << ansi::RESET;
    std::cout.flush();

    std::string input;
    if (std::getline(std::cin, input) && !input.empty()) {
        size_t start = input.find_first_not_of(" \t");
        size_t end = input.find_last_not_of(" \t");
        if (start != std::string::npos) {
            input = input.substr(start, end - start + 1);
        }
        if (!input.empty()) {
            try {
                int choice = std::stoi(input);
                if (choice >= 1 && choice <= (int)predictions.size()) {
                    const auto& sel = predictions[choice - 1];
                    std::cout << ansi::DIM << "Selected: " << sel.source;
                    if (!sel.destination.empty()) std::cout << " -> " << sel.destination;
                    std::cout << ansi::RESET << "\n\n";
                    return sel;
                }
            } catch (...) {}
            std::cerr << ansi::RED << "Invalid choice. Using default." << ansi::RESET << "\n";
        }
    }

    const auto& sel = predictions[0];
    std::cout << ansi::DIM << "Selected: " << sel.source;
    if (!sel.destination.empty()) std::cout << " -> " << sel.destination;
    std::cout << ansi::RESET << "\n\n";
    return sel;
}

// Load facts from file and print a random one
static void printRandomFact() {
#ifdef MTA_FACTS
    std::ifstream f(MTA_FACTS);
    if (!f.is_open()) return;

    std::vector<std::string> facts;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        facts.push_back(line);
    }
    if (facts.empty()) return;

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, facts.size() - 1);
    const auto& fact = facts[dist(rng)];

    std::cout << ansi::DIM << "Did you know? " << ansi::RESET
              << ansi::BOLD << fact << ansi::RESET << "\n\n";
#endif
}

static bool site_up() {
    ServerPinger pinger;
    const std::string url = MTA_SUBWAY_FEED_URL_ACE;

    long httpCode = 0;
    std::string errorMsg;
    const bool ok = pinger.ping(url, 5, &httpCode, &errorMsg);

#ifdef DEBUG
    if (ok) {
        std::cout << ansi::DIM << "Server OK (HTTP " << httpCode << ")" << ansi::RESET << "\n";
    }
#endif

    return ok;
}


// Find the index of a stop_id in a trip's stop_times, or -1 if not found
static int findStopIndex(const DetailedTripUpdate& trip, const std::string& stopId) {
    for (int i = 0; i < (int)trip.stop_times.size(); i++) {
        if (trip.stop_times[i].stop_id == stopId) return i;
    }
    return -1;
}


int main(int argc, char* argv[]) {
    // 1. Parse CLI arguments
    CliOptions opts = parseArgs(argc, argv);
    if (opts.help) {
        printUsage(argv[0]);
        return 0;
    }

    // 2. Check server connectivity
    if (!site_up()) {
        std::cerr << ansi::RED << "Failed to talk to server. Check your network connection."
                  << ansi::RESET << "\n";
        return 1;
    }

    // 3. Load stops data
    StopLookup stops;
    if (!stops.isLoaded()) {
        std::cerr << ansi::RED << "Failed to load stops data." << ansi::RESET << "\n";
        return 1;
    }

    // 4. Initialize cache
    MtaCache cache;

    // === PREDICTION MODE: no source specified ===
    if (!opts.source_query) {
        Prediction pred = pickPrediction(cache);
        opts.source_query = pred.source;
        if (!pred.destination.empty()) {
            opts.dest_query = pred.destination;
        }
    }

    StationMatch station = pickStation(*opts.source_query, "Source", stops);

    // === ROUTING MODE: source + destination ===
    if (opts.dest_query) {
        StationMatch destStation = pickStation(*opts.dest_query, "Destination", stops);

        // Record trip to cache
        cache.record(station.stop_name, destStation.stop_name);

        // Show a fun fact while fetching
        printRandomFact();

        // Fetch ALL feeds for routing (need complete network view)
        std::vector<DetailedTripUpdate> allTrips;
        for (int i = 0; i < MTA_SUBWAY_FEEDS_COUNT; i++) {
            std::cout << "\r" << ansi::DIM << "Fetching " << MTA_SUBWAY_FEEDS[i].id
                      << " (" << MTA_SUBWAY_FEEDS[i].lines << ")..."
                      << std::string(20, ' ') << ansi::RESET << std::flush;
            SubwayFeedClient client(MTA_SUBWAY_FEEDS[i].url);
            try {
                auto trips = client.fetchDetailedTripUpdates();
                allTrips.insert(allTrips.end(),
                                std::make_move_iterator(trips.begin()),
                                std::make_move_iterator(trips.end()));
            } catch (const std::exception& e) {
                std::cout << "\r" << std::string(60, ' ') << "\r";
                std::cerr << ansi::RED << "Error fetching " << MTA_SUBWAY_FEEDS[i].id
                          << ": " << e.what() << ansi::RESET << "\n";
            }
        }
        std::cout << "\r" << std::string(60, ' ') << "\r" << std::flush;

        // Shift the planner's reference time forward by the lead-time filter
        // so it picks departures the feed already has but would otherwise
        // skip as "too soon". Single planner pass, no extra fetch — same
        // speed as the no-filter case.
        auto now = std::chrono::system_clock::now();
        auto plannerNow = now;
        if (opts.min_lead_minutes > 0) {
            plannerNow = now + std::chrono::minutes(opts.min_lead_minutes);
        }
        auto routes = findRoutes(
            station, destStation,
            allTrips, stops, plannerNow, 5);

        // total_minutes from the planner is measured from plannerNow. When
        // -t shifts the clock, the user still wants "minutes from real now"
        // so they can compare options at a glance. Recompute it.
        if (opts.min_lead_minutes > 0) {
            for (auto& r : routes) {
                if (r.segments.empty()) continue;
                r.total_minutes = TimeHelper::minutesDiff(now, r.segments.back().alight_time);
            }
        }
        if (routes.empty() && opts.min_lead_minutes > 0) {
            std::cerr << ansi::YELLOW
                      << "No route departs " << opts.min_lead_minutes
                      << "+ min from now. The realtime feed only sees ~30 min ahead."
                      << ansi::RESET << "\n";
        }

        // Render
        std::string timestamp = TimeHelper::nowDateTimeEastern();
        if (opts.verbose) {
            renderRoutePlanVerbose(routes, station.stop_name, destStation.stop_name, timestamp);
        } else {
            renderRoutePlan(routes, station.stop_name, destStation.stop_name, timestamp);
        }
        return 0;
    }

    // === SOURCE-ONLY MODE ===

    // Record source-only trip to cache
    cache.record(station.stop_name);

    // 5. Determine which feeds to fetch
    std::vector<const MtaSubwayFeedInfo*> feeds;
    if (opts.line) {
        for (int i = 0; i < MTA_SUBWAY_FEEDS_COUNT; i++) {
            if (opts.line.value() == MTA_SUBWAY_FEEDS[i].id) {
                feeds.push_back(&MTA_SUBWAY_FEEDS[i]);
            }
        }
        if (feeds.empty()) {
            std::cerr << ansi::RED << "Unknown line group: " << opts.line.value()
                      << ansi::RESET << "\n";
            return 1;
        }
    } else {
        for (int i = 0; i < MTA_SUBWAY_FEEDS_COUNT; i++) {
            feeds.push_back(&MTA_SUBWAY_FEEDS[i]);
        }
    }

    // 5. Fetch feeds and build train display list
    auto now = std::chrono::system_clock::now();
    std::vector<TrainDisplay> allTrains;

    // Collect every directional stop ID under the named station — including
    // sibling parents that share the name (Times Sq-42 St = 127/725/902) and
    // every parent in the same transfer-complex (e.g. A27 @ 42 St-Port
    // Authority for ACE). A train surfaced by a different-name parent gets
    // that parent's name as its platform_label, so the user sees where to walk.
    struct StopCheck {
        std::string stop_id;
        std::string direction;
        std::string platform_label; // empty when same as the searched station
    };
    auto parentNameFor = [&](const std::string& dirStopId) {
        std::string parentId = dirStopId;
        if (!parentId.empty() && (parentId.back() == 'N' || parentId.back() == 'S')) {
            parentId.pop_back();
        }
        std::string n = stops.getStopName(parentId);
        return (n == station.stop_name) ? std::string() : n;
    };

    std::vector<StopCheck> stopChecks;
    for (const auto& nid : station.all_north_stop_ids)
        stopChecks.push_back({nid, "Northbound", parentNameFor(nid)});
    for (const auto& sid : station.all_south_stop_ids)
        stopChecks.push_back({sid, "Southbound", parentNameFor(sid)});
    if (stopChecks.empty()) {
        stopChecks.push_back({station.base_stop_id, "Unknown", std::string()});
    }

    // Show a fun fact while fetching
    printRandomFact();

    for (const auto* feed : feeds) {
        std::cout << "\r" << ansi::DIM << "Fetching " << feed->id
                  << " (" << feed->lines << ")..."
                  << std::string(20, ' ') << ansi::RESET << std::flush;

        SubwayFeedClient client(feed->url);
        std::vector<DetailedTripUpdate> trips;
        try {
            trips = client.fetchDetailedTripUpdates();
        } catch (const std::exception& e) {
            std::cout << "\r" << std::string(60, ' ') << "\r";
            std::cerr << ansi::RED << "Error fetching " << feed->id
                      << ": " << e.what() << ansi::RESET << "\n";
            continue;
        }

        for (const auto& trip : trips) {
            for (const auto& check : stopChecks) {
                int srcIdx = findStopIndex(trip, check.stop_id);
                if (srcIdx < 0) continue;

                const auto& srcStop = trip.stop_times[srcIdx];

                // Compute ETA
                auto timePoint = srcStop.arrival_time
                                     ? srcStop.arrival_time
                                     : srcStop.departure_time;
                if (!timePoint) continue;

                int minutes = TimeHelper::minutesDiff(now, *timePoint);
                if (minutes < 0) continue; // already passed
                if (minutes < opts.min_lead_minutes) continue; // need more lead time

                TrainDisplay td;
                td.route_id = trip.trip.route_id;
                td.direction = check.direction;
                td.platform_name = check.platform_label;
                td.eta_minutes = minutes;
                td.eta_time = TimeHelper::formatHHMM(*timePoint);

                // Final destination = last stop of the trip
                const std::string& finalStopId = trip.stop_times.back().stop_id;
                td.final_destination = stops.getStopName(finalStopId);

                // For verbose mode: collect remaining stops from source onward
                if (opts.verbose) {
                    for (int i = srcIdx; i < (int)trip.stop_times.size(); i++) {
                        const auto& st = trip.stop_times[i];
                        std::string name = stops.getStopName(st.stop_id);
                        auto tp = st.arrival_time ? st.arrival_time : st.departure_time;
                        std::string time = tp ? TimeHelper::formatHHMM(*tp) : "??:??";
                        td.remaining_stops.push_back({name, time});
                    }
                }

                allTrains.push_back(std::move(td));
                break; // found this trip at this station, no need to check other direction
            }
        }
    }
    std::cout << "\r" << std::string(60, ' ') << "\r" << std::flush;

    // 6. Sort by ETA
    std::sort(allTrains.begin(), allTrains.end(),
              [](const TrainDisplay& a, const TrainDisplay& b) {
                  return a.eta_minutes < b.eta_minutes;
              });

    // 7. Render
    std::string timestamp = TimeHelper::nowDateTimeEastern();

    if (opts.verbose) {
        renderVerboseDiagram(allTrains, station.stop_name, timestamp);
    } else {
        renderCompactTable(allTrains, station.stop_name, timestamp);
    }

    return 0;
}
