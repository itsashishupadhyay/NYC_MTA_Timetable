#include "stop_lookup.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <functional>

// Helper: trim whitespace from both ends of a string
static inline void trim(std::string& s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

// Helper: split line on comma or tab (supports both CSV and TSV style)
static std::vector<std::string> splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;

    for (char ch : line) {
        if (ch == ',' || ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

StopLookup::StopLookup(const std::string& path) {
    loaded_ = loadFile(path);
#ifdef MTA_TRANSFERS
    if (loaded_) loadTransfers(MTA_TRANSFERS);
#endif
}

std::string StopLookup::ufFind(const std::string& x) const {
    auto it = uf_parent_.find(x);
    if (it == uf_parent_.end()) {
        uf_parent_[x] = x;
        return x;
    }
    if (it->second == x) return x;
    std::string root = ufFind(it->second);
    uf_parent_[x] = root; // path compression
    return root;
}

void StopLookup::ufUnion(const std::string& a, const std::string& b) {
    std::string ra = ufFind(a);
    std::string rb = ufFind(b);
    if (ra == rb) return;
    uf_parent_[ra] = rb;
}

bool StopLookup::loadTransfers(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = splitLine(line);
        if (fields.size() < 2) continue;
        if (firstLine) {
            firstLine = false;
            std::string h0 = fields[0]; trim(h0);
            if (h0 == "from_stop_id") continue;
        }
        std::string a = fields[0]; trim(a);
        std::string b = fields[1]; trim(b);
        if (a.empty() || b.empty()) continue;
        // Only union parent-level ids that exist in our stops map; ignore
        // platform-level ids (they'd never appear in transfers.txt anyway).
        if (stops_.count(a) == 0 || stops_.count(b) == 0) continue;
        ufUnion(a, b);
    }

    // Materialize members list per root (only for parents we know about).
    for (const auto& [id, info] : stops_) {
        if (info.location_type != 1) continue;
        std::string root = ufFind(id);
        complex_members_[root].push_back(id);
    }
    return true;
}

std::vector<std::string> StopLookup::complexFor(const std::string& parentId) const {
    if (uf_parent_.empty()) return {parentId};
    std::string root = ufFind(parentId);
    auto it = complex_members_.find(root);
    if (it == complex_members_.end()) return {parentId};
    return it->second;
}

std::string StopLookup::complexRootFor(const std::string& parentId) const {
    if (uf_parent_.empty()) return parentId;
    return ufFind(parentId);
}

std::string StopLookup::boroughFor(const std::string& parentId) const {
    auto it = stops_.find(parentId);
    if (it == stops_.end()) return std::string();
    double lat = it->second.stop_lat;
    double lon = it->second.stop_lon;
    if (lat == 0.0 && lon == 0.0) return std::string();

    // Coarse rectangles tuned to the NYC subway system. They overlap a bit at
    // the borders, so the order of checks matters — most distinctive first.
    if (lat <  40.65 && lon < -74.03) return "Staten Island";
    if (lat >  40.795)                return "Bronx";
    if (lat <  40.74 && lon > -74.03) return "Brooklyn";
    if (lon > -73.93)                 return "Queens";
    return "Manhattan";
}

bool StopLookup::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    bool firstLine = true;

    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        // Remove possible carriage return
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        auto fields = splitLine(line);
        if (fields.size() < 2) {
            continue; // not enough info to be useful
        }

        // Optional header row: skip if the first column is literally "stop_id"
        if (firstLine) {
            firstLine = false;
            std::string col0 = fields[0];
            trim(col0);
            if (col0 == "stop_id") {
                continue; // skip header
            }
        }

        StopInfo info;

        // Column 0: stop_id
        trim(fields[0]);
        info.stop_id = fields[0];

        // Column 1: stop_name
        trim(fields[1]);
        info.stop_name = fields[1];

        // Column 2: stop_lat (optional)
        if (fields.size() > 2) {
            trim(fields[2]);
            if (!fields[2].empty()) {
                info.stop_lat = std::stod(fields[2]);
            }
        }

        // Column 3: stop_lon (optional)
        if (fields.size() > 3) {
            trim(fields[3]);
            if (!fields[3].empty()) {
                info.stop_lon = std::stod(fields[3]);
            }
        }

        // Column 4: location_type (optional)
        if (fields.size() > 4) {
            trim(fields[4]);
            if (!fields[4].empty()) {
                info.location_type = std::stoi(fields[4]);
            }
        }

        // Column 5: parent_station (optional)
        if (fields.size() > 5) {
            trim(fields[5]);
            info.parent_station = fields[5];
        }

        if (!info.stop_id.empty()) {
            stops_[info.stop_id] = std::move(info);
        }
    }

    return !stops_.empty();
}

std::optional<StopInfo> StopLookup::getStopInfo(const std::string& stop_id) const {
    auto it = stops_.find(stop_id);
    if (it == stops_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string StopLookup::getStopName(const std::string& stop_id) const {
    auto it = stops_.find(stop_id);
    if (it == stops_.end()) {
        return stop_id; // fall back to ID if unknown
    }
    return it->second.stop_name;
}

static std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// Levenshtein edit distance between two strings
static int editDistance(const std::string& a, const std::string& b) {
    int m = (int)a.size(), n = (int)b.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            if (a[i - 1] == b[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    return dp[m][n];
}

// Deduplicate matches by (station name, transfer-complex). Two parents with
// the same name in the SAME complex (e.g. Times Sq's 127, 725, 902, R16 — all
// physically connected) merge into one entry. Two parents with the same name
// in DIFFERENT complexes (e.g. "7 Av" in Manhattan vs Brooklyn Brighton vs
// Brooklyn Culver) stay as separate entries so the picker can offer a real
// choice.
static std::vector<StationMatch> deduplicateMatches(
    std::vector<StationMatch>& matches, int maxResults, const StopLookup& stops)
{
    std::vector<StationMatch> deduped;
    std::unordered_map<std::string, std::size_t> nameComplexIndex;
    for (auto& m : matches) {
        std::string complexKey;
        if (!m.parent_stop_ids.empty()) {
            complexKey = stops.complexRootFor(m.parent_stop_ids.front());
        }
        std::string key = toLower(m.stop_name) + "\x01" + complexKey;
        auto it = nameComplexIndex.find(key);
        if (it == nameComplexIndex.end()) {
            nameComplexIndex[key] = deduped.size();
            deduped.push_back(std::move(m));
        } else {
            // Same name AND same complex — merge sibling parents (different
            // line groups at the same physical station).
            auto& kept = deduped[it->second];
            for (const auto& p : m.parent_stop_ids)    kept.parent_stop_ids.push_back(p);
            for (const auto& s : m.all_north_stop_ids) kept.all_north_stop_ids.push_back(s);
            for (const auto& s : m.all_south_stop_ids) kept.all_south_stop_ids.push_back(s);
        }
    }
    if ((int)deduped.size() > maxResults) {
        deduped.resize(maxResults);
    }
    return deduped;
}

std::vector<StationMatch> StopLookup::fuzzyMatch(const std::string& query, int maxResults) const {
    if (query.empty()) return {};

    std::string lowerQuery = toLower(query);
    std::vector<StationMatch> matches;

    for (const auto& [id, info] : stops_) {
        // Only match parent stations (location_type == 1)
        if (info.location_type != 1) continue;

        std::string lowerName = toLower(info.stop_name);

        int score = -1;
        if (lowerName == lowerQuery) {
            score = 0; // exact match
        } else if (lowerName.find(lowerQuery) == 0) {
            score = 1; // starts with
        } else if (lowerName.find(lowerQuery) != std::string::npos) {
            score = 2; // contains
        }

        if (score < 0) continue;

        StationMatch m;
        m.base_stop_id = info.stop_id;
        m.stop_name = info.stop_name;
        m.score = score;

        // Derive directional stop IDs
        std::string nId = info.stop_id + "N";
        std::string sId = info.stop_id + "S";
        m.north_stop_id = (stops_.count(nId) > 0) ? nId : "";
        m.south_stop_id = (stops_.count(sId) > 0) ? sId : "";
        m.parent_stop_ids.push_back(info.stop_id);
        if (!m.north_stop_id.empty()) m.all_north_stop_ids.push_back(m.north_stop_id);
        if (!m.south_stop_id.empty()) m.all_south_stop_ids.push_back(m.south_stop_id);

        matches.push_back(std::move(m));
    }

    // Sort: by score ascending, then by name length (prefer shorter/more specific)
    std::sort(matches.begin(), matches.end(), [](const StationMatch& a, const StationMatch& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.stop_name.size() < b.stop_name.size();
    });

    auto deduped = deduplicateMatches(matches, maxResults, *this);

    // Expand each match to cover its full transfer-complex.
    auto expand = [this](StationMatch& m) {
        if (m.parent_stop_ids.empty()) return;
        std::set<std::string> parents(m.parent_stop_ids.begin(), m.parent_stop_ids.end());
        std::set<std::string> seenN(m.all_north_stop_ids.begin(), m.all_north_stop_ids.end());
        std::set<std::string> seenS(m.all_south_stop_ids.begin(), m.all_south_stop_ids.end());
        std::set<std::string> extraNames;

        // Snapshot original parents — complexFor() must run on each.
        std::vector<std::string> seedParents = m.parent_stop_ids;
        for (const auto& seed : seedParents) {
            for (const auto& sib : complexFor(seed)) {
                if (parents.count(sib)) continue;
                parents.insert(sib);
                m.parent_stop_ids.push_back(sib);

                std::string nId = sib + "N";
                std::string sId = sib + "S";
                if (stops_.count(nId) && !seenN.count(nId)) {
                    m.all_north_stop_ids.push_back(nId);
                    seenN.insert(nId);
                }
                if (stops_.count(sId) && !seenS.count(sId)) {
                    m.all_south_stop_ids.push_back(sId);
                    seenS.insert(sId);
                }
                auto it = stops_.find(sib);
                if (it != stops_.end() && it->second.stop_name != m.stop_name) {
                    extraNames.insert(it->second.stop_name);
                }
            }
        }
        m.complex_extras.assign(extraNames.begin(), extraNames.end());
    };
    for (auto& m : deduped) expand(m);

    // If substring matching found results, return them
    if (!deduped.empty()) return deduped;

    // Fallback: Levenshtein edit distance for typos / spelling mistakes
    // Score each station by edit distance to the query
    struct EditMatch {
        StationMatch match;
        int distance;
    };
    std::vector<EditMatch> editMatches;

    for (const auto& [id, info] : stops_) {
        if (info.location_type != 1) continue;

        std::string lowerName = toLower(info.stop_name);

        // Split station name into words
        auto splitWords = [](const std::string& s) {
            std::vector<std::string> words;
            std::istringstream iss(s);
            std::string w;
            while (iss >> w) {
                // Strip punctuation from edges, also split on '-'
                std::string current;
                for (char c : w) {
                    if (c == '-') {
                        if (!current.empty()) { words.push_back(current); current.clear(); }
                    } else if (std::isalnum((unsigned char)c)) {
                        current.push_back(c);
                    }
                }
                if (!current.empty()) words.push_back(current);
            }
            return words;
        };

        auto stationWords = splitWords(lowerName);
        auto queryWords = splitWords(lowerQuery);

        // Strategy 1: full query vs full name
        int dist = editDistance(lowerQuery, lowerName);

        // Strategy 2: match each query word to best station word, sum distances
        if (!queryWords.empty() && !stationWords.empty()) {
            int wordSumDist = 0;
            for (const auto& qw : queryWords) {
                int bestWord = (int)qw.size(); // worst case: all insertions
                for (const auto& sw : stationWords) {
                    bestWord = std::min(bestWord, editDistance(qw, sw));
                }
                wordSumDist += bestWord;
            }
            dist = std::min(dist, wordSumDist);
        }

        // Strategy 3: single query word vs each station word (for single-word queries)
        if (queryWords.size() == 1) {
            for (const auto& sw : stationWords) {
                dist = std::min(dist, editDistance(queryWords[0], sw));
            }
        }

        // Only consider reasonable matches
        int threshold = (int)lowerQuery.size() * 2 / 5 + 2;
        if (dist > threshold) continue;

        StationMatch m;
        m.base_stop_id = info.stop_id;
        m.stop_name = info.stop_name;
        m.score = 100 + dist; // high base score so these sort after substring matches

        std::string nId = info.stop_id + "N";
        std::string sId = info.stop_id + "S";
        m.north_stop_id = (stops_.count(nId) > 0) ? nId : "";
        m.south_stop_id = (stops_.count(sId) > 0) ? sId : "";
        m.parent_stop_ids.push_back(info.stop_id);
        if (!m.north_stop_id.empty()) m.all_north_stop_ids.push_back(m.north_stop_id);
        if (!m.south_stop_id.empty()) m.all_south_stop_ids.push_back(m.south_stop_id);

        editMatches.push_back({std::move(m), dist});
    }

    // Sort by edit distance
    std::sort(editMatches.begin(), editMatches.end(),
              [](const EditMatch& a, const EditMatch& b) {
                  if (a.distance != b.distance) return a.distance < b.distance;
                  return a.match.stop_name.size() < b.match.stop_name.size();
              });

    std::vector<StationMatch> fuzzyResults;
    for (auto& em : editMatches) {
        fuzzyResults.push_back(std::move(em.match));
    }

    auto fuzzyDeduped = deduplicateMatches(fuzzyResults, maxResults, *this);
    for (auto& m : fuzzyDeduped) expand(m);
    return fuzzyDeduped;
}
