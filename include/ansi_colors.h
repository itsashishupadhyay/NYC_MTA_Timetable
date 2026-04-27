#pragma once

#include <string>
#include <cstdlib>

namespace ansi {

constexpr const char* RESET       = "\033[0m";
constexpr const char* BOLD        = "\033[1m";
constexpr const char* DIM         = "\033[2m";
constexpr const char* UNDERLINE   = "\033[4m";

// Standard foreground colors (kept for back-compat with existing callers)
constexpr const char* RED         = "\033[1;31m";
constexpr const char* GREEN       = "\033[1;32m";
constexpr const char* YELLOW      = "\033[1;33m";
constexpr const char* BLUE        = "\033[1;34m";
constexpr const char* MAGENTA     = "\033[1;35m";
constexpr const char* GRAY        = "\033[90m";
constexpr const char* WHITE       = "\033[1;37m";

// 256-color foregrounds for line palettes
constexpr const char* ORANGE      = "\033[38;5;208m";
constexpr const char* LIGHT_GREEN = "\033[38;5;119m";
constexpr const char* BROWN       = "\033[38;5;130m";
constexpr const char* SLATE       = "\033[38;5;245m";

// Foreground color matching MTA's official line palette (bold).
inline const char* colorForRoute(const std::string& route_id) {
    if (route_id == "A" || route_id == "C" || route_id == "E") return BLUE;
    if (route_id == "B" || route_id == "D" || route_id == "F" || route_id == "M") return ORANGE;
    if (route_id == "G") return LIGHT_GREEN;
    if (route_id == "J" || route_id == "Z") return BROWN;
    if (route_id == "N" || route_id == "Q" || route_id == "R" || route_id == "W") return YELLOW;
    if (route_id == "L") return SLATE;
    if (route_id == "1" || route_id == "2" || route_id == "3") return RED;
    if (route_id == "4" || route_id == "5" || route_id == "6") return GREEN;
    if (route_id == "7") return MAGENTA;
    if (route_id == "S" || route_id == "GS" || route_id == "FS" || route_id == "H") return GRAY;
    if (route_id == "SIR") return BLUE;
    return RESET;
}

// 256-color background matching MTA palette (close approximations of MTA hex).
// IRT 1/2/3 red:    #EE352E ~= 196
// IRT 4/5/6 green:  #00933C ~= 28
// IRT 7 purple:     #B933AD ~= 127
// IND ACE blue:     #0039A6 ~= 20
// IND BDFM orange:  #FF6319 ~= 208
// IND G lime:       #6CBE45 ~= 113
// BMT JZ brown:     #996633 ~= 130
// BMT L slate:      #A7A9AC ~= 245
// BMT NQRW yellow:  #FCCC0A ~= 220 (paired w/ black fg)
// Shuttle gray:     #808183 ~= 240
// SIR blue:         #0078C6 ~= 26
inline const char* bgForRoute(const std::string& route_id) {
    if (route_id == "A" || route_id == "C" || route_id == "E") return "\033[48;5;20m";
    if (route_id == "B" || route_id == "D" || route_id == "F" || route_id == "M") return "\033[48;5;208m";
    if (route_id == "G") return "\033[48;5;113m";
    if (route_id == "J" || route_id == "Z") return "\033[48;5;130m";
    if (route_id == "N" || route_id == "Q" || route_id == "R" || route_id == "W") return "\033[48;5;220m";
    if (route_id == "L") return "\033[48;5;245m";
    if (route_id == "1" || route_id == "2" || route_id == "3") return "\033[48;5;196m";
    if (route_id == "4" || route_id == "5" || route_id == "6") return "\033[48;5;28m";
    if (route_id == "7") return "\033[48;5;127m";
    if (route_id == "S" || route_id == "GS" || route_id == "FS" || route_id == "H") return "\033[48;5;240m";
    if (route_id == "SIR") return "\033[48;5;26m";
    return "\033[48;5;236m";
}

// NQRW yellow needs black foreground for legibility; everything else uses white.
inline const char* fgOnRouteBg(const std::string& route_id) {
    if (route_id == "N" || route_id == "Q" || route_id == "R" || route_id == "W") {
        return "\033[38;5;16m"; // black
    }
    return "\033[38;5;231m"; // bright white
}

// NYC-station-sign style line bullet: " A " on a colored background, bold.
// Always 3 visible columns wide (so panels stay aligned). Single-letter routes
// pad as " A "; two-letter routes (e.g. "SI", "GS") render as "SI ".
inline std::string routeBadge(const std::string& route_id) {
    std::string label = route_id;
    if (label.empty()) label = "?";
    // Trim/normalize to <=2 chars for visual budget; but most MTA route_ids are 1 char.
    if (label.size() > 2) label = label.substr(0, 2);

    std::string padded;
    if (label.size() == 1) padded = std::string(" ") + label + " ";
    else                   padded = label + " ";

    std::string out;
    out.reserve(32);
    out += BOLD;
    out += bgForRoute(route_id);
    out += fgOnRouteBg(route_id);
    out += padded;
    out += RESET;
    return out;
}

// Color an ETA by urgency. Approximates the visual emphasis on NYC PA/CIS
// "next train" displays: arriving = bold red, very soon = bold green,
// soon = yellow, later = normal, way out = dim.
inline const char* colorForETA(int minutes) {
    if (minutes <= 0) return "\033[1;91m";   // bold bright red — arriving / now
    if (minutes <= 2) return "\033[1;92m";   // bold bright green — go now
    if (minutes <= 5) return "\033[1;93m";   // bold yellow — soon
    if (minutes <= 9) return "\033[37m";     // normal white — comfortable
    return DIM;                              // dim — far out
}

// True visible (display-cell) length of a string containing ANSI escapes.
// Counts bytes outside ESC[...m sequences; assumes ASCII printable inputs.
inline std::size_t visibleLength(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) i++;
            if (i < s.size()) i++; // skip terminator
            continue;
        }
        // Best-effort UTF-8: count one display cell per code point start.
        unsigned char c = (unsigned char)s[i];
        if ((c & 0xC0) != 0x80) n++;
        i++;
    }
    return n;
}

// Pad/truncate s to exactly `width` visible columns (right-padded with spaces).
// UTF-8 aware: counts code points, not bytes, when measuring visible cells.
inline std::string padToWidth(const std::string& s, std::size_t width) {
    std::size_t vis = visibleLength(s);
    if (vis == width) return s;
    if (vis < width)  return s + std::string(width - vis, ' ');

    if (width == 0) return std::string();
    std::string out;
    out.reserve(s.size());
    std::size_t shown = 0;
    bool inEsc = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!inEsc && c == 0x1B) { inEsc = true; out += s[i]; continue; }
        if (inEsc) {
            out += s[i];
            if (c >= '@' && c <= '~') inEsc = false;
            continue;
        }
        bool isContinuation = ((c & 0xC0) == 0x80);
        if (!isContinuation) {
            if (shown >= width - 1) break; // leave one cell for ellipsis
            shown++;
        }
        out += s[i];
    }
    out += '.';
    out += RESET;
    return out;
}

// Detect terminal width via $COLUMNS; falls back to 80.
inline int terminalWidth() {
    if (const char* cols = std::getenv("COLUMNS")) {
        try {
            int v = std::stoi(cols);
            if (v > 20) return v;
        } catch (...) {}
    }
    return 80;
}

// Honour NO_COLOR by checking once at process start.
inline bool colorEnabled() {
    static const bool v = (std::getenv("NO_COLOR") == nullptr);
    return v;
}

} // namespace ansi
