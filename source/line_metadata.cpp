#include "line_metadata.h"

namespace {

// One bucket of the MTA stop_id namespace. `prefix` is the first character of
// the parent stop_id ('A'-'S' for IND/BMT, '1'-'9' for IRT). `min`/`max` are
// the inclusive numeric range — for letter prefixes, parsed from the chars
// after the letter (e.g. "F32" → 32); for digit prefixes, the full numeric
// stop_id (e.g. "127" → 127). First match wins.
struct Bucket {
    char prefix;
    int min_id;
    int max_id;
    const char* trains;
    const char* line_name;
};

// Ordered by prefix, then numeric range. Ranges chosen from the
// observed stop_id allocation in info/stops.txt; see the README's
// "Refreshing static GTFS data" section if MTA renumbers anything.
constexpr Bucket kBuckets[] = {
    {'A',   2,  38, "A/C/E",      "8 Av Line"},
    {'A',  40,  55, "A/C",        "Fulton St Line"},
    {'A',  57,  65, "A",          "Lefferts Branch"},
    {'B',   4,  10, "F/Q",        "63 St Line"},
    {'B',  12,  23, "D",          "West End Line"},
    {'D',   1,  13, "B/D",        "Concourse Line"},
    {'D',  14,  22, "B/D/F/M",    "6 Av Line"},
    {'D',  24,  43, "B/Q",        "Brighton Line"},
    {'E',   1,   1, "E",          "8 Av Line (WTC)"},
    {'F',   1,   7, "E/F",        "Queens Blvd Line"},
    {'F',   9,   9, "E/M/G",      "Court Sq"},
    {'F',  11,  12, "E/M",        "Queens Blvd Line"},
    {'F',  14,  18, "F",          "2 Av / Houston St Line"},
    {'F',  20,  39, "F/G",        "Culver Line"},
    {'G',   5,   7, "E/J/Z",      "Archer Av Line"},
    {'H',   1,  15, "A/S",        "Rockaway Line"},
    {'J',  12,  31, "J/Z",        "Jamaica Line"},
    {'L',   1,  29, "L",          "Canarsie Line"},
    {'M',   1,  23, "M",          "Myrtle Av Line"},
    {'N',   2,  12, "N",          "Sea Beach Line"},
    {'Q',   1,   5, "Q",          "2 Av Line"},
    {'R',   1,   9, "N/W",        "Astoria Line"},
    {'R',  11,  27, "N/Q/R/W",    "Broadway Line"},
    {'R',  28,  45, "D/N/R/W",    "4 Av Line"},
    {'S',   1,   4, "S",          "Franklin Av Shuttle"},
    {'S',   9,  31, "SIR",        "Staten Island Railway"},
    {'1', 101, 142, "1/2/3",      "Broadway-7 Av Line"},
    {'2', 201, 260, "2/5",        "White Plains Rd / Lenox"},
    {'3', 301, 302, "3",          "Lenox Av Line"},
    {'4', 401, 423, "4/5",        "Lex Av Express / Jerome Av"},
    {'5', 501, 505, "5",          "Dyre Av Line"},
    {'6', 601, 640, "6",          "Lex Av Local / Pelham Line"},
    {'7', 701, 726, "7",          "Flushing Line"},
    {'9', 901, 902, "S",          "42 St Shuttle"},
};

// Parse the numeric portion of a parent stop_id.
// For letter-prefix IDs (e.g. "F32"), skip the leading letter and parse the
// digits after it. For digit-only IDs (e.g. "127"), parse the whole ID.
int parseNumericPortion(const std::string& id) {
    if (id.empty()) return -1;
    bool first_is_digit = (id[0] >= '0' && id[0] <= '9');
    size_t start = first_is_digit ? 0 : 1;
    int num = 0;
    bool any = false;
    for (size_t i = start; i < id.size(); i++) {
        char c = id[i];
        if (c < '0' || c > '9') break;
        num = num * 10 + (c - '0');
        any = true;
    }
    return any ? num : -1;
}

}  // namespace

LineInfo lineInfoFor(const std::string& parent_stop_id) {
    if (parent_stop_id.empty()) return {"", ""};
    char first = parent_stop_id[0];
    int num = parseNumericPortion(parent_stop_id);
    if (num < 0) return {"", ""};

    for (const auto& b : kBuckets) {
        if (b.prefix != first) continue;
        if (num >= b.min_id && num <= b.max_id) {
            return {b.trains, b.line_name};
        }
    }
    return {"", ""};
}
