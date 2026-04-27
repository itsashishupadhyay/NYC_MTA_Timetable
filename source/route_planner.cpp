#include "route_planner.h"
#include "time_helper.h"

#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <cstdlib>

static constexpr int TRANSFER_PENALTY_SECONDS = 180; // 3 minutes walking
static constexpr int TRANSFER_WEIGHT = 10000;        // heavily penalize transfers in priority

// Build a transfer map keyed on **complex root ID** (not stop name). This is
// the critical fix for stations that share a name across boroughs (six "86 St"
// stations exist; the planner previously collapsed them into one node and
// happily transferred between Brooklyn 86 St and Manhattan 86 St). Two stops
// share a key only if transfers.txt connects them.
//
// complex_root_id -> list of platform stop_ids (e.g. R36N, R36S) at that
// physical complex.
using ComplexStopMap = std::unordered_map<std::string, std::vector<std::string>>;

static ComplexStopMap buildComplexStopMap(const StopLookup& stops) {
    ComplexStopMap result;
    for (const auto& [id, info] : stops.allStops()) {
        if (info.location_type == 1) continue;       // skip parents themselves
        if (info.stop_name.empty()) continue;

        // Resolve to parent. Most platforms list parent_station; fall back to
        // stripping the N/S directional suffix when not.
        std::string parentId = info.parent_station;
        if (parentId.empty()) {
            parentId = id;
            if (!parentId.empty() && (parentId.back() == 'N' || parentId.back() == 'S'))
                parentId.pop_back();
        }
        std::string complexKey = stops.complexRootFor(parentId);
        result[complexKey].push_back(id);
    }
    return result;
}

static std::string stationNameFor(const std::string& stop_id, const StopLookup& stops) {
    auto info = stops.getStopInfo(stop_id);
    if (info) return info->stop_name;
    return stops.getStopName(stop_id);
}

// ── Compact breadcrumb for path reconstruction (Option A) ────────────────────

struct Breadcrumb {
    int trip_idx;       // index into all_trips
    int board_stop;     // index within trip.stop_times
    int alight_stop;    // index within trip.stop_times
};

// ── Lightweight Dijkstra state (Option A) ────────────────────────────────────

struct LightState {
    int station_idx;
    std::chrono::system_clock::time_point arrival_time;
    int num_transfers;
    int route_idx;      // interned route_id, -1 if starting
    int trip_idx;       // index into all_trips, -1 if starting
    std::vector<Breadcrumb> crumbs;   // ~1-4 entries, 12 bytes each
    std::vector<int> visited;         // ~2-10 entries, 4 bytes each
    int priority_val;

    bool has_visited(int idx) const {
        for (int v : visited) if (v == idx) return true;
        return false;
    }
};

// ── String interning helper ──────────────────────────────────────────────────

struct StringInterner {
    std::unordered_map<std::string, int> map;
    std::vector<std::string> names;

    int intern(const std::string& s) {
        auto [it, inserted] = map.emplace(s, (int)names.size());
        if (inserted) names.push_back(s);
        return it->second;
    }

    const std::string& name(int idx) const { return names[idx]; }
    int size() const { return (int)names.size(); }
};

// ═════════════════════════════════════════════════════════════════════════════

std::vector<RoutePlan> findRoutes(
    const StationMatch& source,
    const StationMatch& dest,
    const std::vector<DetailedTripUpdate>& all_trips,
    const StopLookup& stops,
    std::chrono::system_clock::time_point now,
    int maxRoutes)
{
    using time_point = std::chrono::system_clock::time_point;

    // ═══ 0. Build the complex-keyed transfer map ═══
    // Graph nodes are transfer-complex roots, NOT stop names. This is the
    // distinction that keeps Brooklyn 86 St and Manhattan 86 St separate.
    ComplexStopMap transfers = buildComplexStopMap(stops);

    // ═══ 1. String interning ═══
    // The interner stores complex_root_ids (canonical, stable strings); not
    // human-readable names. Display names are looked up separately when we
    // build the RoutePlan output.

    StringInterner stations;
    StringInterner routes;

    // Source/dest come straight from the user's picked StationMatch — we
    // know their exact base_stop_id, so there's no ambiguity even if multiple
    // physically-distinct stations share a name.
    std::string source_complex = stops.complexRootFor(source.base_stop_id);
    std::string dest_complex   = stops.complexRootFor(dest.base_stop_id);
    int source_idx = stations.intern(source_complex);
    int dest_idx   = stations.intern(dest_complex);

    // Map platform stop_id -> station_idx (== interned complex_root_id)
    std::unordered_map<std::string, int> stop_to_station;

    for (const auto& [complexKey, sids] : transfers) {
        int sidx = stations.intern(complexKey);
        for (const auto& sid : sids) {
            stop_to_station[sid] = sidx;
        }
    }

    // Resolve a stop_id to its station index (with caching). For platform
    // stops, walks via parent_station -> complex_root.
    auto resolve_stop = [&](const std::string& stop_id) -> int {
        auto it = stop_to_station.find(stop_id);
        if (it != stop_to_station.end()) return it->second;
        auto info = stops.getStopInfo(stop_id);
        std::string parentId;
        if (info && !info->parent_station.empty()) {
            parentId = info->parent_station;
        } else {
            parentId = stop_id;
            if (!parentId.empty() && (parentId.back() == 'N' || parentId.back() == 'S'))
                parentId.pop_back();
        }
        std::string complexKey = stops.complexRootFor(parentId);
        int idx = stations.intern(complexKey);
        stop_to_station[stop_id] = idx;
        return idx;
    };

    // ═══ 2. Pre-resolve station index for every trip stop ═══
    // Avoids repeated string lookups during Dijkstra

    std::vector<std::vector<int>> trip_stations(all_trips.size());

    for (int t = 0; t < (int)all_trips.size(); t++) {
        const auto& trip = all_trips[t];
        trip_stations[t].resize(trip.stop_times.size());
        for (int s = 0; s < (int)trip.stop_times.size(); s++) {
            trip_stations[t][s] = resolve_stop(trip.stop_times[s].stop_id);
        }
    }

    // ═══ 3. Compute transfer stations (Option B) ═══
    // A station is a transfer point if:
    //   (a) it has stop_ids from multiple base groups (different line families), OR
    //   (b) trips from multiple route_ids serve a stop_id at this station
    // The destination is always treated as a transfer station.

    std::unordered_set<int> transfer_stations;
    transfer_stations.insert(dest_idx);

    // (a) Multi-base detection: complexes whose member parents span multiple
    //     stop_id base groups (different line families) are transfer points.
    for (const auto& [complexKey, sids] : transfers) {
        std::set<std::string> bases;
        for (const auto& sid : sids) {
            std::string base = sid;
            if (!base.empty() && (base.back() == 'N' || base.back() == 'S'))
                base.pop_back();
            bases.insert(base);
        }
        if (bases.size() > 1) {
            transfer_stations.insert(stations.intern(complexKey));
        }
    }

    // ═══ 4. Time window pruning (Option C) ═══
    // Peak hours: 90 min window. Off-peak: 150 min window.
    // Guarantee: at least 2 trips per stop_id regardless of window.

    std::time_t now_t = std::chrono::system_clock::to_time_t(now);
    const char* oldTZ = std::getenv("TZ");
    std::string oldTZCopy = oldTZ ? std::string(oldTZ) : "";
    setenv("TZ", "America/New_York", 1);
    tzset();
    std::tm now_tm{};
    localtime_r(&now_t, &now_tm);
    if (!oldTZCopy.empty()) setenv("TZ", oldTZCopy.c_str(), 1);
    else unsetenv("TZ");
    tzset();

    bool is_peak = (now_tm.tm_wday >= 1 && now_tm.tm_wday <= 5) &&
                   ((now_tm.tm_hour >= 6 && now_tm.tm_hour < 10) ||
                    (now_tm.tm_hour >= 16 && now_tm.tm_hour < 20));
    auto window_end = now + std::chrono::minutes(is_peak ? 90 : 150);

    // ═══ 5. Build trip index with time filter + next-2 guarantee ═══

    struct TripRef {
        int trip_idx;
        int stop_idx;
        time_point dep_time;
    };

    std::unordered_map<std::string, std::vector<TripRef>> trip_index;

    for (int t = 0; t < (int)all_trips.size(); t++) {
        const auto& trip = all_trips[t];
        for (int s = 0; s < (int)trip.stop_times.size(); s++) {
            auto dep = trip.stop_times[s].departure_time
                           ? trip.stop_times[s].departure_time
                           : trip.stop_times[s].arrival_time;
            if (!dep || *dep < now) continue;
            trip_index[trip.stop_times[s].stop_id].push_back({t, s, *dep});
        }
    }

    // Sort by departure and apply window filter with next-2 guarantee
    for (auto& [sid, refs] : trip_index) {
        std::sort(refs.begin(), refs.end(),
                  [](const TripRef& a, const TripRef& b) {
                      return a.dep_time < b.dep_time;
                  });

        std::vector<TripRef> filtered;
        for (const auto& ref : refs) {
            if (ref.dep_time <= window_end || (int)filtered.size() < 2) {
                filtered.push_back(ref);
            }
        }
        refs = std::move(filtered);
    }

    // (b) Multi-route detection — now that trip_index is built, find stops
    //     where trips from different routes depart (same-line transfer points)
    for (const auto& [sid, refs] : trip_index) {
        if (refs.size() < 2) continue;
        std::string first_route = all_trips[refs[0].trip_idx].trip.route_id;
        bool multi = false;
        for (size_t i = 1; i < refs.size(); i++) {
            if (all_trips[refs[i].trip_idx].trip.route_id != first_route) {
                multi = true;
                break;
            }
        }
        if (multi) {
            auto it = stop_to_station.find(sid);
            if (it != stop_to_station.end()) {
                transfer_stations.insert(it->second);
            }
        }
    }

    // ═══ 6. Dijkstra with lightweight states ═══

    auto compute_priority = [&](int num_transfers, time_point arrival) -> int {
        int elapsed = (int)std::chrono::duration_cast<std::chrono::minutes>(
            arrival - now).count();
        return num_transfers * TRANSFER_WEIGHT + elapsed;
    };

    auto cmp = [](const LightState& a, const LightState& b) {
        return a.priority_val > b.priority_val;
    };

    std::priority_queue<LightState, std::vector<LightState>, decltype(cmp)> pq(cmp);

    // Visited: (station_idx, transfers) -> best arrival time
    std::map<std::pair<int,int>, time_point> visited;

    std::vector<RoutePlan> results;

    // Track how many routes per line-pattern we've collected (max 3 per pattern)
    std::map<std::vector<std::string>, int> pattern_counts;
    static constexpr int MAX_PER_PATTERN = 3;

    // Seed
    {
        LightState init;
        init.station_idx = source_idx;
        init.arrival_time = now;
        init.num_transfers = 0;
        init.route_idx = -1;
        init.trip_idx = -1;
        init.priority_val = 0;
        init.visited.push_back(source_idx);
        pq.push(std::move(init));
    }

    int iterations = 0;
    static constexpr int MAX_ITERATIONS = 200000;

    while (!pq.empty() && (int)results.size() < maxRoutes && iterations < MAX_ITERATIONS) {
        iterations++;
        auto cur = std::move(const_cast<LightState&>(pq.top()));
        pq.pop();

        // ── Reached destination? Reconstruct path ──
        if (cur.station_idx == dest_idx) {
            RoutePlan plan;

            for (const auto& bc : cur.crumbs) {
                const auto& trip = all_trips[bc.trip_idx];
                RouteSegment seg;
                seg.route_id = trip.trip.route_id;
                seg.trip_id  = trip.trip.trip_id;
                seg.final_destination = stationNameFor(
                    trip.stop_times.back().stop_id, stops);
                seg.direction = directionToString(trip);

                auto bd = trip.stop_times[bc.board_stop].departure_time
                              ? trip.stop_times[bc.board_stop].departure_time
                              : trip.stop_times[bc.board_stop].arrival_time;
                auto al = trip.stop_times[bc.alight_stop].arrival_time
                              ? trip.stop_times[bc.alight_stop].arrival_time
                              : trip.stop_times[bc.alight_stop].departure_time;
                seg.board_time  = bd ? *bd : now;
                seg.alight_time = al ? *al : now;

                for (int k = bc.board_stop; k <= bc.alight_stop; k++) {
                    const auto& st = trip.stop_times[k];
                    std::string name = stationNameFor(st.stop_id, stops);
                    auto tp = st.arrival_time ? st.arrival_time : st.departure_time;
                    seg.stops.push_back({name,
                        tp ? TimeHelper::formatHHMM(*tp) : "??:??"});
                }

                // Merge consecutive segments on the same trip
                if (!plan.segments.empty() &&
                    plan.segments.back().trip_id == seg.trip_id) {
                    auto& prev = plan.segments.back();
                    for (size_t i = 1; i < seg.stops.size(); i++)
                        prev.stops.push_back(seg.stops[i]);
                    prev.alight_time = seg.alight_time;
                } else {
                    plan.segments.push_back(std::move(seg));
                }
            }

            plan.num_transfers = std::max(0, (int)plan.segments.size() - 1);
            plan.total_minutes = (int)std::chrono::duration_cast<std::chrono::minutes>(
                cur.arrival_time - now).count();

            // Build line pattern for this route (e.g. ["R"] or ["C","Q"])
            std::vector<std::string> pattern;
            for (const auto& seg : plan.segments)
                pattern.push_back(seg.route_id);

            // Skip dominated routes
            bool dominated = false;

            // Same lines departing within 3 min = truly the same train, skip
            for (const auto& existing : results) {
                std::vector<std::string> ep;
                for (const auto& seg : existing.segments)
                    ep.push_back(seg.route_id);
                if (ep == pattern &&
                    std::abs(existing.total_minutes - plan.total_minutes) < 3) {
                    dominated = true;
                    break;
                }
            }

            // Cap at MAX_PER_PATTERN departures for the same line pattern
            if (!dominated && pattern_counts[pattern] >= MAX_PER_PATTERN)
                dominated = true;

            // Strictly worse: more transfers AND more time than any existing
            if (!dominated) {
                for (const auto& existing : results) {
                    if (plan.num_transfers > existing.num_transfers &&
                        plan.total_minutes >= existing.total_minutes) {
                        dominated = true;
                        break;
                    }
                }
            }

            if (!dominated) {
                pattern_counts[pattern]++;
                results.push_back(std::move(plan));
            }
            continue;
        }

        // ── Pruning: skip if we've been here with same-or-fewer transfers earlier ──
        auto vk = std::make_pair(cur.station_idx, cur.num_transfers);
        auto vit = visited.find(vk);
        if (vit != visited.end() && vit->second <= cur.arrival_time) continue;
        visited[vk] = cur.arrival_time;

        // ── Expand: find trips at this station ──
        auto tit = transfers.find(stations.name(cur.station_idx));
        if (tit == transfers.end()) continue;

        for (const auto& sid : tit->second) {
            auto idx_it = trip_index.find(sid);
            if (idx_it == trip_index.end()) continue;

            for (const auto& ref : idx_it->second) {
                const auto& trip = all_trips[ref.trip_idx];
                int trip_route = routes.intern(trip.trip.route_id);

                // Don't re-board the same trip
                if (cur.trip_idx == ref.trip_idx) continue;

                // Don't re-board the same route (prevents back-and-forth loops)
                if (cur.route_idx >= 0 && cur.route_idx == trip_route) continue;

                // Transfer penalty
                bool is_transfer = (cur.route_idx >= 0);
                auto min_depart = cur.arrival_time;
                if (is_transfer)
                    min_depart += std::chrono::seconds(TRANSFER_PENALTY_SECONDS);

                if (ref.dep_time < min_depart) continue;

                int new_transfers = cur.num_transfers + (is_transfer ? 1 : 0);
                if (new_transfers > 3) continue;

                // ── Option B: only alight at transfer stations + destination ──
                for (int s = ref.stop_idx + 1;
                     s < (int)trip.stop_times.size(); s++) {

                    int stop_station = trip_stations[ref.trip_idx][s];

                    // Skip non-transfer, non-destination stops
                    if (transfer_stations.count(stop_station) == 0) continue;

                    auto arr_tp = trip.stop_times[s].arrival_time
                                      ? trip.stop_times[s].arrival_time
                                      : trip.stop_times[s].departure_time;
                    if (!arr_tp) continue;

                    // Cycle detection (allow destination)
                    if (stop_station != dest_idx &&
                        cur.has_visited(stop_station)) continue;

                    LightState next;
                    next.station_idx  = stop_station;
                    next.arrival_time = *arr_tp;
                    next.num_transfers = new_transfers;
                    next.route_idx    = trip_route;
                    next.trip_idx     = ref.trip_idx;
                    next.priority_val = compute_priority(new_transfers, *arr_tp);
                    next.crumbs  = cur.crumbs;
                    next.crumbs.push_back({ref.trip_idx, ref.stop_idx, s});
                    next.visited = cur.visited;
                    next.visited.push_back(stop_station);

                    pq.push(std::move(next));
                }
            }
        }
    }

    // Sort: fewest transfers, then fastest
    std::sort(results.begin(), results.end(),
              [](const RoutePlan& a, const RoutePlan& b) {
                  if (a.num_transfers != b.num_transfers)
                      return a.num_transfers < b.num_transfers;
                  return a.total_minutes < b.total_minutes;
              });

    return results;
}
