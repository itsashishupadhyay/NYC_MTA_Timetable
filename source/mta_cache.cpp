#include "mta_cache.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <map>

// ── Eastern time helper (same pattern as time_helper.cpp) ────────────────────

static std::tm toEastern(std::time_t t) {
    const char* oldTZ = std::getenv("TZ");
    std::string oldTZCopy = oldTZ ? std::string(oldTZ) : std::string();

    setenv("TZ", "America/New_York", 1);
    tzset();

    std::tm tm{};
    localtime_r(&t, &tm);

    if (!oldTZCopy.empty()) {
        setenv("TZ", oldTZCopy.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return tm;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

// Resolve the per-user cache directory.
//
// Order:
//   1. $MTA_CACHE_DIR        — explicit override (tests, packagers)
//   2. macOS:  $HOME/Library/Caches/mta-timetable
//      Linux:  $XDG_CACHE_HOME/mta-timetable, else $HOME/.cache/mta-timetable
//   3. cwd fallback (returns empty → caller uses ".mta_cache")
//
// Returns the directory; the cache file lives at <dir>/cache.
static std::string defaultCacheDir() {
    if (const char* override_dir = std::getenv("MTA_CACHE_DIR")) {
        if (*override_dir) return override_dir;
    }
    const char* home = std::getenv("HOME");
    if (!home || !*home) return std::string();
#ifdef __APPLE__
    return std::string(home) + "/Library/Caches/mta-timetable";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        if (*xdg) return std::string(xdg) + "/mta-timetable";
    }
    return std::string(home) + "/.cache/mta-timetable";
#endif
}

static std::string defaultCachePath() {
    std::string dir = defaultCacheDir();
    if (dir.empty()) {
        // No HOME — fall back to cwd. Rare (CI without env), but graceful.
        return ".mta_cache";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // ignore failure; open() will report

    std::string path = dir + "/cache";

    // One-time migration: if a legacy ./.mta_cache exists in the cwd at first
    // run and we don't yet have a user-cache file, seed the new path with it.
    // Don't delete the legacy file — the user may still be running an older
    // build, and we don't want to surprise-destroy their history.
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::path legacy = ".mta_cache";
        if (std::filesystem::exists(legacy, ec)) {
            std::filesystem::copy_file(legacy, path,
                std::filesystem::copy_options::skip_existing, ec);
        }
    }
    return path;
}

static std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char ch : line) {
        if (ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

// Parse "HH:MM" → minutes since midnight
static int parseTimeMinutes(const std::string& hhmm) {
    if (hhmm.size() < 4) return -1;
    auto pos = hhmm.find(':');
    if (pos == std::string::npos) return -1;
    int h = std::stoi(hhmm.substr(0, pos));
    int m = std::stoi(hhmm.substr(pos + 1));
    return h * 60 + m;
}

// Parse "YYYY-MM-DD" → days since epoch (for recency calculation)
static int parseDateDays(const std::string& date) {
    if (date.size() < 10) return 0;
    struct std::tm tm{};
    tm.tm_year = std::stoi(date.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(date.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(date.substr(8, 2));
    tm.tm_hour = 12; // noon to avoid DST edge cases
    std::time_t t = timegm(&tm);
    return (int)(t / 86400);
}

static int todayDays() {
    std::time_t now = std::time(nullptr);
    std::tm tm = toEastern(now);
    // Reconstruct as UTC date to get consistent day count
    struct std::tm utc{};
    utc.tm_year = tm.tm_year;
    utc.tm_mon  = tm.tm_mon;
    utc.tm_mday = tm.tm_mday;
    utc.tm_hour = 12;
    std::time_t t = timegm(&utc);
    return (int)(t / 86400);
}

// Label for time-of-day (used in reason strings)
static std::string timeOfDayLabel(int minutesSinceMidnight) {
    if (minutesSinceMidnight < 360)  return "early morning";  // before 6 AM
    if (minutesSinceMidnight < 600)  return "morning";        // 6-10 AM
    if (minutesSinceMidnight < 900)  return "midday";         // 10 AM - 3 PM
    if (minutesSinceMidnight < 1140) return "evening";        // 3-7 PM
    if (minutesSinceMidnight < 1380) return "night";          // 7-11 PM
    return "late night";
}

// ── Known column names for V1 ───────────────────────────────────────────────

static const std::vector<std::string> V1_COLUMNS = {
    "date", "day_of_week", "is_weekend", "time", "source", "destination"
};

// ── Constructor ─────────────────────────────────────────────────────────────

MtaCache::MtaCache() : path_(defaultCachePath()) {
    load();
}

MtaCache::MtaCache(const std::string& path) : path_(path) {
    load();
}

// ── File I/O ────────────────────────────────────────────────────────────────

bool MtaCache::load() {
    std::ifstream in(path_);
    if (!in.is_open()) {
        // File doesn't exist yet — that's OK, we'll create it on first record
        column_names_ = V1_COLUMNS;
        loaded_ = true;
        return true;
    }

    std::string line;
    int version = 0;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // Version header
        if (line.rfind("#MTA_CACHE_VERSION", 0) == 0) {
            auto fields = splitTabs(line);
            if (fields.size() >= 2) {
                version = std::stoi(fields[1]);
            }
            continue;
        }

        // Column header
        if (line.rfind("#columns", 0) == 0) {
            auto fields = splitTabs(line);
            column_names_.clear();
            for (size_t i = 1; i < fields.size(); i++) {  // skip "#columns"
                column_names_.push_back(fields[i]);
            }
            continue;
        }

        // Skip other comment lines
        if (line[0] == '#') continue;

        // Data line
        auto fields = splitTabs(line);
        if (fields.empty()) continue;

        // Map fields by column name
        CacheEntry entry;
        std::map<std::string, std::string> fieldMap;
        for (size_t i = 0; i < column_names_.size() && i < fields.size(); i++) {
            fieldMap[column_names_[i]] = fields[i];
        }

        // Parse known V1 fields
        if (fieldMap.count("date"))        entry.date        = fieldMap["date"];
        if (fieldMap.count("day_of_week")) entry.day_of_week = std::stoi(fieldMap["day_of_week"]);
        if (fieldMap.count("is_weekend"))  entry.is_weekend  = (fieldMap["is_weekend"] == "1");
        if (fieldMap.count("time"))        entry.time        = fieldMap["time"];
        if (fieldMap.count("source"))      entry.source      = fieldMap["source"];
        if (fieldMap.count("destination")) entry.destination  = fieldMap["destination"];

        // Preserve any extra columns beyond V1 for round-trip safety
        for (size_t i = V1_COLUMNS.size(); i < fields.size(); i++) {
            entry.extra_fields.push_back(fields[i]);
        }

        entries_.push_back(std::move(entry));
    }

    // If no column header was found, assume V1
    if (column_names_.empty()) {
        column_names_ = V1_COLUMNS;
    }

    (void)version; // Future: migration logic based on version
    loaded_ = true;
    return true;
}

bool MtaCache::save() const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) return false;

    // Version header
    out << "#MTA_CACHE_VERSION\t" << CACHE_VERSION << "\n";

    // Column header
    out << "#columns";
    for (const auto& col : column_names_) {
        out << "\t" << col;
    }
    out << "\n";

    // Data rows
    for (const auto& e : entries_) {
        out << e.date << "\t"
            << e.day_of_week << "\t"
            << (e.is_weekend ? 1 : 0) << "\t"
            << e.time << "\t"
            << e.source << "\t"
            << e.destination;

        // Preserve extra columns
        for (const auto& extra : e.extra_fields) {
            out << "\t" << extra;
        }
        out << "\n";
    }

    return true;
}

void MtaCache::evict() {
    if ((int)entries_.size() > MAX_ENTRIES) {
        int excess = (int)entries_.size() - MAX_ENTRIES;
        entries_.erase(entries_.begin(), entries_.begin() + excess);
    }
}

// ── Current context ─────────────────────────────────────────────────────────

MtaCache::Now MtaCache::currentContext() const {
    std::time_t t = std::time(nullptr);
    std::tm tm = toEastern(t);

    Now now;
    now.minutes_since_midnight = tm.tm_hour * 60 + tm.tm_min;
    now.day_of_week = tm.tm_wday;  // 0=Sun
    now.is_weekend = (tm.tm_wday == 0 || tm.tm_wday == 6);

    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    now.date = buf;

    return now;
}

// ── Record ──────────────────────────────────────────────────────────────────

void MtaCache::record(const std::string& source, const std::string& destination) {
    Now now = currentContext();

    CacheEntry entry;
    entry.date        = now.date;
    entry.day_of_week = now.day_of_week;
    entry.is_weekend  = now.is_weekend;

    // Format time as HH:MM
    char timeBuf[8];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
                  now.minutes_since_midnight / 60,
                  now.minutes_since_midnight % 60);
    entry.time = timeBuf;

    entry.source      = source;
    entry.destination = destination;

    entries_.push_back(std::move(entry));
    evict();
    save();
}

// ── Predict ─────────────────────────────────────────────────────────────────

std::vector<Prediction> MtaCache::predict(int maxResults) const {
    if (entries_.empty()) return {};

    Now now = currentContext();
    int today = todayDays();

    // Detect return-trip opportunity: most recent entry with a destination
    std::string returnSource, returnDest;
    for (int i = (int)entries_.size() - 1; i >= 0; i--) {
        if (!entries_[i].destination.empty()) {
            int entryDays = parseDateDays(entries_[i].date);
            int entryMinutes = parseTimeMinutes(entries_[i].time);
            int ageDays = today - entryDays;
            int ageMinutes = ageDays * 1440 + (now.minutes_since_midnight - entryMinutes);
            if (ageMinutes >= 0 && ageMinutes <= 720) {  // within 12 hours
                returnSource = entries_[i].destination;
                returnDest   = entries_[i].source;
            }
            break;
        }
    }

    // Group entries by (source, destination) and compute weighted score
    struct RouteKey {
        std::string source;
        std::string destination;
        bool operator<(const RouteKey& o) const {
            if (source != o.source) return source < o.source;
            return destination < o.destination;
        }
    };

    struct RouteScore {
        double score = 0.0;
        double best_time_sim = 0.0;    // for reason generation
        double total_day_type = 0.0;
        int count = 0;
        bool return_boosted = false;
    };

    std::map<RouteKey, RouteScore> scores;
    const double sigma = 90.0;  // minutes

    for (const auto& entry : entries_) {
        RouteKey key{entry.source, entry.destination};
        auto& rs = scores[key];
        rs.count++;

        // 1. Time similarity: gaussian decay from current time
        int entryMinutes = parseTimeMinutes(entry.time);
        if (entryMinutes < 0) continue;
        double deltaMin = std::abs(now.minutes_since_midnight - entryMinutes);
        // Handle wrap-around (e.g., 23:50 vs 00:10 = 20 min, not 1420)
        if (deltaMin > 720) deltaMin = 1440 - deltaMin;
        double timeSim = std::exp(-(deltaMin * deltaMin) / (2.0 * sigma * sigma));

        // 2. Day type match
        double dayTypeW = (entry.is_weekend == now.is_weekend) ? 1.5 : 0.3;

        // 3. Day-of-week match
        double dayW = (entry.day_of_week == now.day_of_week) ? 1.3 : 1.0;

        // 4. Recency decay
        int entryDays = parseDateDays(entry.date);
        int ageDays = today - entryDays;
        if (ageDays < 0) ageDays = 0;
        double recency = std::exp(-(double)ageDays / 30.0);

        double entryScore = timeSim * dayTypeW * dayW * recency;
        rs.score += entryScore;

        if (timeSim > rs.best_time_sim) rs.best_time_sim = timeSim;
        rs.total_day_type += dayTypeW;
    }

    // Apply Markov return-trip boost
    if (!returnSource.empty()) {
        RouteKey returnKey{returnSource, returnDest};
        auto it = scores.find(returnKey);
        if (it != scores.end()) {
            it->second.score *= 2.0;
            it->second.return_boosted = true;
        } else {
            // Even if this exact route hasn't been cached before,
            // suggest it as a return trip
            RouteScore rs;
            rs.score = 1.0;  // baseline score
            rs.return_boosted = true;
            rs.count = 0;
            scores[returnKey] = rs;
        }
    }

    // Sort by score descending
    std::vector<std::pair<RouteKey, RouteScore>> sorted(scores.begin(), scores.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second.score > b.second.score; });

    // Build predictions
    std::vector<Prediction> results;
    for (const auto& [key, rs] : sorted) {
        if ((int)results.size() >= maxResults) break;
        if (rs.score < 0.01) continue;  // skip negligible scores

        Prediction p;
        p.source      = key.source;
        p.destination = key.destination;
        p.score       = rs.score;

        // Generate reason string
        if (rs.return_boosted) {
            p.reason = "Return trip (you went " + key.destination + " -> " + key.source + " earlier)";
        } else if (rs.best_time_sim > 0.7 && now.is_weekend) {
            p.reason = "Weekend " + timeOfDayLabel(now.minutes_since_midnight) + " trip";
        } else if (rs.best_time_sim > 0.7 && !now.is_weekend) {
            p.reason = "Weekday " + timeOfDayLabel(now.minutes_since_midnight) + " commute";
        } else if (rs.count >= 5) {
            p.reason = "Frequent route (" + std::to_string(rs.count) + " trips)";
        } else {
            p.reason = "Based on travel history";
        }

        results.push_back(std::move(p));
    }

    return results;
}

