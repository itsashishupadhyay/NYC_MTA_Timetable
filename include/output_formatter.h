#pragma once

#include <string>
#include <vector>
#include <utility>

// Forward declare to avoid circular includes
struct RoutePlan;

struct TrainDisplay {
    std::string route_id;
    std::string direction;           // "Northbound" / "Southbound"
    std::string final_destination;   // name of last stop
    int eta_minutes;                 // minutes from now
    std::string eta_time;            // "HH:MM"
    std::string platform_name;       // parent station name when it differs from
                                     // the searched station — e.g. "42 St-Port
                                     // Authority Bus Terminal" for ACE trains
                                     // surfaced by a "Times Sq" search. Empty
                                     // when the train is at the searched station.

    // Subsequent ETAs of the *same* train (same route + direction + dest +
    // platform). Populated by the renderer when collapsing duplicate rows so
    // the user sees "N Astoria 4 min   +10 +14" instead of three separate rows.
    std::vector<int> follow_etas;

    // For verbose mode: remaining stops from source onward
    // Each pair is {stop_name, "HH:MM"}
    std::vector<std::pair<std::string, std::string>> remaining_stops;
};

// Compact table: one line per train, sorted by ETA
void renderCompactTable(const std::vector<TrainDisplay>& trains,
                        const std::string& stationName,
                        const std::string& timestamp);

// Verbose: ASCII train diagram per train, color-coded
void renderVerboseDiagram(const std::vector<TrainDisplay>& trains,
                          const std::string& stationName,
                          const std::string& timestamp);

// Route plan: compact source-to-destination journey with transfers.
// One row per leg, suitable for comparing multiple options on one screen.
void renderRoutePlan(const std::vector<RoutePlan>& plans,
                     const std::string& sourceName,
                     const std::string& destName,
                     const std::string& timestamp);

// Verbose route plan: per-stop diagram with vivid Board/Transfer/Arrive cues.
// Use when the user has selected a specific journey and wants the full picture.
void renderRoutePlanVerbose(const std::vector<RoutePlan>& plans,
                            const std::string& sourceName,
                            const std::string& destName,
                            const std::string& timestamp);
