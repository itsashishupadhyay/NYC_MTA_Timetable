#pragma once

#include <string>
#include <vector>

struct CacheEntry {
    std::string date;           // YYYY-MM-DD
    int day_of_week = 0;        // 0=Sun ... 6=Sat
    bool is_weekend = false;
    std::string time;           // HH:MM (Eastern)
    std::string source;         // resolved station name
    std::string destination;    // resolved station name, or "" for source-only
    std::vector<std::string> extra_fields;  // future columns preserved on round-trip
};

struct Prediction {
    std::string source;
    std::string destination;    // empty = source-only prediction
    double score = 0.0;
    std::string reason;         // human-readable explanation
};

class MtaCache {
public:
    MtaCache();                                  // uses ./.mta_cache (project-relative)
    explicit MtaCache(const std::string& path);  // custom path (testing)

    // Record a completed trip into the cache
    void record(const std::string& source, const std::string& destination = "");

    // Predict the most likely route(s) based on current time/day context
    std::vector<Prediction> predict(int maxResults = 3) const;

    bool isLoaded() const { return loaded_; }
    const std::string& path() const { return path_; }

private:
    static constexpr int MAX_ENTRIES = 1000;
    static constexpr int CACHE_VERSION = 1;

    std::string path_;
    std::vector<CacheEntry> entries_;
    std::vector<std::string> column_names_;  // from file header, for round-trip preservation
    bool loaded_ = false;

    bool load();
    bool save() const;
    void evict();

    // Snapshot of current Eastern time context
    struct Now {
        int minutes_since_midnight;
        int day_of_week;        // 0=Sun ... 6=Sat
        bool is_weekend;
        std::string date;       // YYYY-MM-DD
    };
    Now currentContext() const;
};
