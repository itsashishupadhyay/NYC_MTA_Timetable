#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <utility>

// Information about a single stop or station from stops.txt
struct StopInfo {
    std::string stop_id;
    std::string stop_name;
    double stop_lat = 0.0;
    double stop_lon = 0.0;
    int location_type = 0;               // 0/empty = stop/platform, 1 = station, etc.
    std::string parent_station;          // empty if none
};

// Result of a fuzzy station name match.
// A "station" by name may correspond to multiple parent stops in stops.txt
// (e.g. Times Sq-42 St = parents 127 [1/2/3], 725 [N/Q/R/W], 902 [7/S]).
// Callers that want all arrivals at the named station should use the
// `all_north_stop_ids` / `all_south_stop_ids` vectors.
struct StationMatch {
    std::string base_stop_id;   // first parent (kept for back-compat)
    std::string stop_name;
    std::string north_stop_id;  // first parent's N stop (back-compat)
    std::string south_stop_id;  // first parent's S stop (back-compat)
    std::vector<std::string> parent_stop_ids;     // every parent in the resolved complex
    std::vector<std::string> all_north_stop_ids;  // every parent's N stop
    std::vector<std::string> all_south_stop_ids;  // every parent's S stop
    // Names of parents in the complex whose name differs from stop_name —
    // used by the picker to disclose connected platforms (e.g. ACE @ 42 St-PABT).
    std::vector<std::string> complex_extras;
    int score;                  // lower = better (0=exact, 1=starts-with, 2=contains)
};

class StopLookup {
public:
    // By default, loads stops.txt from MTA_STOP_DETAILS and (if defined)
    // transfers.txt from MTA_TRANSFERS — letting the lookup resolve
    // cross-station transfer complexes (e.g. Times Sq <-> 42 St-Port Authority).
    explicit StopLookup(const std::string& path = MTA_STOP_DETAILS);

    // Returns true if loading/parsing succeeded.
    bool isLoaded() const { return loaded_; }

    // Lookup the full StopInfo; std::nullopt if unknown stop_id.
    std::optional<StopInfo> getStopInfo(const std::string& stop_id) const;

    // Convenience: return just the stop_name, or the original stop_id if unknown.
    std::string getStopName(const std::string& stop_id) const;

    // Fuzzy match a station name query against all parent stations (location_type=1).
    // Returns up to maxResults matches sorted by relevance. Each match is
    // automatically expanded to cover every parent in the same transfer-complex.
    std::vector<StationMatch> fuzzyMatch(const std::string& query, int maxResults = 5) const;

    // Check if a stop_id exists
    bool hasStop(const std::string& stop_id) const { return stops_.count(stop_id) > 0; }

    // Iterate all stops (needed for building transfer maps)
    const std::unordered_map<std::string, StopInfo>& allStops() const { return stops_; }

    // All parent stop_ids that share a transfer-complex with parentId
    // (always includes parentId itself). Returns {parentId} if no transfers
    // were loaded or the stop has no listed connections.
    std::vector<std::string> complexFor(const std::string& parentId) const;

    // Stable canonical ID for the transfer-complex containing parentId.
    // Two stops share a complex root iff they're physically connected per
    // transfers.txt — use this (not stop_name) when treating stops as the
    // "same station" in routing graphs. Returns parentId itself if no
    // transfers loaded.
    std::string complexRootFor(const std::string& parentId) const;

    // Best-effort borough for a parent stop_id, derived from its lat/lon.
    // Returns "Manhattan", "Brooklyn", "Queens", "Bronx", "Staten Island",
    // or "" when the stop is unknown.
    std::string boroughFor(const std::string& parentId) const;

    // Names of the parent stations immediately before and after `parentId`
    // along the same physical line — i.e. the same first character of stop_id
    // and the next-lower / next-higher numeric portion that exists. Either may
    // be empty (terminus, or unknown stop). Used by the disambiguation picker
    // to render hints like "between Avenue I and Avenue N".
    std::pair<std::string, std::string> neighborNamesFor(const std::string& parentId) const;

private:
    bool loadFile(const std::string& path);
    bool loadTransfers(const std::string& path);

    // Union-find over parent stop_ids: parentId -> root parentId.
    // Lazily mutated by find(); marked mutable so const accessors can use it.
    mutable std::unordered_map<std::string, std::string> uf_parent_;
    // After loading, materialized list of all stop_ids per root (post-compress).
    std::unordered_map<std::string, std::vector<std::string>> complex_members_;

    std::string ufFind(const std::string& x) const;
    void ufUnion(const std::string& a, const std::string& b);

    std::unordered_map<std::string, StopInfo> stops_;
    bool loaded_ = false;
};
