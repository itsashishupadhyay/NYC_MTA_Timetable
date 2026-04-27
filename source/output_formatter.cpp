#include "output_formatter.h"
#include "route_planner.h"
#include "ansi_colors.h"
#include "time_helper.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <ctime>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Shorten verbose connected-platform names to what NYers actually say.
// Used only for the dim "@ <platform>" suffix on cross-station-complex trains.
std::string abbreviatePlatform(const std::string& name) {
    std::string n = name;
    auto replace = [&](const std::string& from, const std::string& to) {
        auto p = n.find(from);
        if (p != std::string::npos) n.replace(p, from.size(), to);
    };
    replace("42 St-Port Authority Bus Terminal", "Port Authority");
    replace("Bus Terminal", "");
    // Drop trailing parens like "(Coney Island branch)".
    auto paren = n.find(" (");
    if (paren != std::string::npos) n = n.substr(0, paren);
    // Right-trim
    while (!n.empty() && (n.back() == ' ' || n.back() == '-')) n.pop_back();
    return n;
}

// Trim a station name to fit a panel column. Drops borough qualifiers in
// parentheses ("(Manhattan)", "(Coney Island branch)"...) so the destination
// reads at a glance like NYC's CIS displays.
std::string compactName(const std::string& name, std::size_t maxLen) {
    std::string n = name;
    auto paren = n.find(" (");
    if (paren != std::string::npos) n = n.substr(0, paren);
    if (n.size() > maxLen) {
        if (maxLen <= 1) return n.substr(0, maxLen);
        n = n.substr(0, maxLen - 1) + "…"; // ellipsis (1 cell)
    }
    return n;
}

// Right-align a string into exactly `w` cells, truncating from the left if
// necessary. Used for ETA cells.
std::string rightPad(const std::string& s, std::size_t w) {
    if (s.size() >= w) return s.substr(0, w);
    return std::string(w - s.size(), ' ') + s;
}

// Format a single arrival row with badge + destination + colored ETA + 3 fixed
// slots for follow-up ETAs. The timing block is *always* the same width so all
// four time values line up across every row in the panel.
std::string formatTrainRow(const TrainDisplay& t, std::size_t width) {
    constexpr std::size_t BADGE_W     = 3;   // " A "
    constexpr std::size_t GAP_W       = 1;
    constexpr std::size_t ETA_W       = 6;   // "13 min" / " 1 min" / "   now"
    constexpr std::size_t FOLLOW_W    = 6;   // "12 min" / " 5 min" / "120 mn"
    constexpr std::size_t MAX_FOLLOWS = 3;
    // Timing block = primary ETA + gap + 3 follow-up slots separated by spaces.
    constexpr std::size_t TIMING_W    = ETA_W + 1
                                        + (MAX_FOLLOWS * FOLLOW_W)
                                        + (MAX_FOLLOWS - 1);

    if (width < BADGE_W + GAP_W + 4 + GAP_W + ETA_W) {
        // Pathologically narrow: just badge + ETA.
        std::ostringstream tiny;
        tiny << ansi::routeBadge(t.route_id) << " "
             << ansi::colorForETA(t.eta_minutes)
             << (t.eta_minutes <= 0 ? std::string("now")
                                    : (std::to_string(t.eta_minutes) + "m"))
             << ansi::RESET;
        return ansi::padToWidth(tiny.str(), width);
    }

    // If the whole TIMING_W block doesn't fit, gracefully fall back to ETA-only.
    bool roomForFollows = (width >= BADGE_W + GAP_W + 8 + GAP_W + TIMING_W);
    std::size_t timingW = roomForFollows ? TIMING_W : ETA_W;
    std::size_t destW   = width - BADGE_W - GAP_W - GAP_W - timingW;

    // Build the destination cell, optionally with a dim "@ <platform>" suffix.
    std::string platformSuffix;
    if (!t.platform_name.empty()) {
        platformSuffix = std::string("  ") + ansi::DIM + "@ "
                       + abbreviatePlatform(t.platform_name) + ansi::RESET;
    }
    std::string destField;
    if (!platformSuffix.empty()) {
        std::size_t platVis = ansi::visibleLength(platformSuffix);
        if (platVis + 6 < destW) {
            destField = compactName(t.final_destination, destW - platVis) + platformSuffix;
        } else {
            destField = compactName(t.final_destination, destW);
        }
    } else {
        destField = compactName(t.final_destination, destW);
    }
    destField = ansi::padToWidth(destField, destW);

    // Primary ETA, right-aligned in ETA_W.
    std::string etaText = (t.eta_minutes <= 0)
                          ? std::string("now")
                          : (std::to_string(t.eta_minutes) + " min");
    std::string etaPadded = rightPad(etaText, ETA_W);

    std::ostringstream row;
    row << ansi::routeBadge(t.route_id)
        << ' '
        << destField
        << ' '
        << ansi::colorForETA(t.eta_minutes) << etaPadded << ansi::RESET;

    // Three follow-up slots (always present; empty slots are blanks so the
    // columns stay aligned across rows even when some trains have no follows).
    if (roomForFollows) {
        row << ' ';
        for (std::size_t i = 0; i < MAX_FOLLOWS; i++) {
            if (i > 0) row << ' ';
            if (i < t.follow_etas.size()) {
                // Absolute minutes from now, same units as the primary ETA.
                std::string s = std::to_string(t.follow_etas[i]) + " min";
                row << ansi::DIM << rightPad(s, FOLLOW_W) << ansi::RESET;
            } else {
                row << std::string(FOLLOW_W, ' ');
            }
        }
    }
    return row.str();
}

// Header for a direction panel: "↑ NORTHBOUND" + rule.
std::string panelHeader(const std::string& direction, std::size_t width) {
    std::string arrow;
    std::string label = direction;
    if (direction == "Northbound")      { arrow = "↑ "; label = "Uptown / Northbound"; }
    else if (direction == "Southbound") { arrow = "↓ "; label = "Downtown / Southbound"; }
    else                                 { arrow = "• "; }

    std::string heading = arrow + label;
    std::ostringstream out;
    out << ansi::BOLD << heading << ansi::RESET;
    std::string padded = ansi::padToWidth(out.str(), width);
    return padded;
}

std::string panelRule(std::size_t width) {
    std::string rule;
    rule.reserve(width * 3);
    for (std::size_t i = 0; i < width; ++i) rule += "─"; // ─
    return std::string(ansi::DIM) + rule + ansi::RESET;
}

// Render two columns side-by-side: walk both lists, padding the shorter one.
void printTwoColumn(const std::vector<std::string>& left,
                    const std::vector<std::string>& right,
                    std::size_t leftWidth,
                    std::size_t /*rightWidth*/,
                    const std::string& gap = "   ") {
    std::size_t rows = std::max(left.size(), right.size());
    for (std::size_t i = 0; i < rows; i++) {
        std::string l = (i < left.size())  ? left[i]  : std::string();
        std::string r = (i < right.size()) ? right[i] : std::string();
        std::cout << ansi::padToWidth(l, leftWidth) << gap << r << "\n";
    }
}

// Footer line used by every renderer — small, dim, transparency-first.
// Shows the snapshot time in Eastern so the reader knows how stale data is.
std::string freshnessLine(const std::string& /*fetchTimestamp*/) {
    std::string hms = TimeHelper::formatHHMM(std::chrono::system_clock::now());
    std::ostringstream out;
    out << ansi::DIM << "snapshot · " << hms << " ET" << ansi::RESET;
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Public renderers
// ---------------------------------------------------------------------------

void renderCompactTable(const std::vector<TrainDisplay>& trains,
                        const std::string& stationName,
                        const std::string& timestamp) {
    int termW = ansi::terminalWidth();
    if (termW < 60) termW = 60;
    if (termW > 140) termW = 140;

    // Header: station name (bold, full-width), date/time aligned right.
    std::cout << "\n";
    {
        std::string left = std::string(ansi::BOLD) + ansi::WHITE + stationName + ansi::RESET;
        std::string right = std::string(ansi::DIM) + timestamp + ansi::RESET;
        std::size_t lvis = ansi::visibleLength(left);
        std::size_t rvis = ansi::visibleLength(right);
        std::size_t pad = (lvis + rvis < (std::size_t)termW) ? termW - lvis - rvis : 1;
        std::cout << left << std::string(pad, ' ') << right << "\n";
    }
    std::cout << panelRule((std::size_t)termW) << "\n\n";

    if (trains.empty()) {
        std::cout << "  " << ansi::YELLOW
                  << "No upcoming trains found at this station."
                  << ansi::RESET << "\n\n";
        std::cout << "  " << freshnessLine(timestamp) << "\n\n";
        return;
    }

    // Collapse repeats: same route + direction + final destination + platform
    // share one row. The earliest ETA stays as primary; up to 3 later ETAs
    // appear as a dim "+N +M" suffix. This is the NYC station-display idiom —
    // "N to Astoria · 4 min · +10 +14" — and keeps each line group on one row.
    std::vector<TrainDisplay> grouped;
    grouped.reserve(trains.size());
    {
        std::unordered_map<std::string, std::size_t> firstIdx;
        for (const auto& t : trains) {
            std::string k = t.route_id + "|" + t.direction + "|"
                          + t.final_destination + "|" + t.platform_name;
            auto it = firstIdx.find(k);
            if (it == firstIdx.end()) {
                firstIdx[k] = grouped.size();
                grouped.push_back(t);
            } else {
                auto& primary = grouped[it->second];
                if (primary.follow_etas.size() < 3 &&
                    t.eta_minutes != primary.eta_minutes) {
                    primary.follow_etas.push_back(t.eta_minutes);
                }
            }
        }
    }

    // Split by direction.
    std::vector<TrainDisplay> nb, sb, other;
    for (const auto& t : grouped) {
        if (t.direction == "Northbound")      nb.push_back(t);
        else if (t.direction == "Southbound") sb.push_back(t);
        else                                  other.push_back(t);
    }

    // Show everything arriving in the next ~30 minutes — that's the planning
    // horizon a NYC commuter actually thinks in. Hard-cap each panel so a
    // station with very frequent service still fits on one screen.
    constexpr int  WINDOW_MIN     = 30;
    constexpr std::size_t MAX_PER_PANEL = 10;
    auto trim = [](std::vector<TrainDisplay>& v) {
        v.erase(std::remove_if(v.begin(), v.end(),
                               [](const TrainDisplay& t) { return t.eta_minutes > WINDOW_MIN; }),
                v.end());
        if (v.size() > MAX_PER_PANEL) v.resize(MAX_PER_PANEL);
    };
    trim(nb);
    trim(sb);

    // Side-by-side needs enough width that each panel can hold a comfortable
    // name column plus the full 4-time timing block (each "12 min" wide).
    // Below ~130 cols, stack vertically so names get the full row width.
    bool twoColumn = !nb.empty() && !sb.empty() && termW >= 130;

    if (twoColumn) {
        std::size_t gapW = 4;
        std::size_t panelW = ((std::size_t)termW - gapW) / 2;

        std::vector<std::string> leftCol, rightCol;
        leftCol.push_back(panelHeader("Northbound", panelW));
        leftCol.push_back(panelRule(panelW));
        for (const auto& t : nb) leftCol.push_back("  " + formatTrainRow(t, panelW - 2));

        rightCol.push_back(panelHeader("Southbound", panelW));
        rightCol.push_back(panelRule(panelW));
        for (const auto& t : sb) rightCol.push_back("  " + formatTrainRow(t, panelW - 2));

        printTwoColumn(leftCol, rightCol, panelW, panelW, std::string(gapW, ' '));
    } else {
        // Stacked single-column fallback.
        auto printPanel = [termW](const std::string& label, const std::vector<TrainDisplay>& list) {
            if (list.empty()) return;
            std::cout << panelHeader(label, (std::size_t)termW) << "\n";
            std::cout << panelRule((std::size_t)termW) << "\n";
            for (const auto& t : list) {
                std::cout << "  " << formatTrainRow(t, (std::size_t)termW - 2) << "\n";
            }
            std::cout << "\n";
        };
        printPanel("Northbound", nb);
        printPanel("Southbound", sb);
        printPanel("Direction unknown", other);
    }

    std::cout << "\n  " << freshnessLine(timestamp) << "\n\n";
}

// ---------------------------------------------------------------------------
// Verbose ASCII diagram — kept for power users; refreshed badge + ETA color.
// ---------------------------------------------------------------------------

void renderVerboseDiagram(const std::vector<TrainDisplay>& trains,
                          const std::string& stationName,
                          const std::string& timestamp) {
    int termW = ansi::terminalWidth();
    if (termW < 60) termW = 60;

    std::cout << "\n" << ansi::BOLD << ansi::WHITE
              << stationName << ansi::RESET
              << "   " << ansi::DIM << timestamp << ansi::RESET << "\n";
    std::cout << panelRule((std::size_t)termW) << "\n";

    if (trains.empty()) {
        std::cout << ansi::YELLOW << "  No upcoming trains found." << ansi::RESET << "\n";
        return;
    }

    for (const auto& t : trains) {
        const char* color = ansi::colorForRoute(t.route_id);
        std::string dirTag = (t.direction == "Northbound") ? "uptown" :
                             (t.direction == "Southbound") ? "downtown" : "?";

        std::cout << "\n"
                  << ansi::routeBadge(t.route_id) << "  "
                  << ansi::BOLD << t.final_destination << ansi::RESET
                  << ansi::DIM << "  (" << dirTag << ")" << ansi::RESET
                  << "   " << ansi::colorForETA(t.eta_minutes)
                  << (t.eta_minutes <= 0 ? "now" : (std::to_string(t.eta_minutes) + " min"))
                  << ansi::RESET
                  << ansi::DIM << "  · board " << t.eta_time << ansi::RESET
                  << "\n";

        if (t.remaining_stops.empty()) continue;

        std::size_t maxNameLen = 0;
        for (const auto& [name, time] : t.remaining_stops) {
            maxNameLen = std::max(maxNameLen, name.size());
        }
        maxNameLen = std::min(maxNameLen, (std::size_t)40);

        for (std::size_t i = 0; i < t.remaining_stops.size(); i++) {
            const auto& [name, time] = t.remaining_stops[i];
            bool isFirst = (i == 0);
            bool isLast  = (i == t.remaining_stops.size() - 1);

            std::cout << color;
            if (isFirst)      std::cout << "  ●── ";   // ●──
            else if (isLast)  std::cout << "  └── ";   // └──
            else              std::cout << "  │   ";              // │
            std::cout << ansi::RESET;

            std::cout << std::left << std::setw((int)maxNameLen + 2) << name;
            std::cout << ansi::DIM << time << ansi::RESET << "\n";
        }
    }

    std::cout << "\n  " << freshnessLine(timestamp) << "\n\n";
}

// ---------------------------------------------------------------------------
// Route plan — multi-leg journey, source -> destination, with alternates.
// ---------------------------------------------------------------------------

void renderRoutePlan(const std::vector<RoutePlan>& plans,
                     const std::string& sourceName,
                     const std::string& destName,
                     const std::string& timestamp) {
    int termW = ansi::terminalWidth();
    if (termW < 60) termW = 60;
    if (termW > 140) termW = 140;

    std::cout << "\n";
    {
        std::string left = std::string(ansi::BOLD) + ansi::WHITE
                         + sourceName + "  →  " + destName + ansi::RESET;
        std::string right = std::string(ansi::DIM) + timestamp + ansi::RESET;
        std::size_t lvis = ansi::visibleLength(left);
        std::size_t rvis = ansi::visibleLength(right);
        std::size_t pad = (lvis + rvis < (std::size_t)termW) ? termW - lvis - rvis : 1;
        std::cout << left << std::string(pad, ' ') << right << "\n";
    }
    std::cout << panelRule((std::size_t)termW) << "\n";

    if (plans.empty()) {
        std::cout << "\n  " << ansi::YELLOW
                  << "No route found between these stations."
                  << ansi::RESET << "\n\n";
        std::cout << "  " << freshnessLine(timestamp) << "\n\n";
        return;
    }

    // Each option is a tiny self-contained block:
    //   Option N ★    18 min · leaves in 4 min · 1 transfer
    //     [Q]  Times Sq-42 St  →  14 St-Union Sq    13:24 board · 5 stops · 5 min
    //     [L]  14 St-Union Sq  →  Bedford Av        13:38 board · 3 stops · arrive 13:44
    //
    // The aim: every option fits in ~3 lines, so 3-5 options live on one screen.
    // The starred option is the recommended one (top of the planner's sort).

    auto fromName = [](const RouteSegment& s) {
        return s.stops.empty() ? std::string("?") : s.stops.front().first;
    };
    auto toName = [](const RouteSegment& s) {
        return s.stops.empty() ? std::string("?") : s.stops.back().first;
    };
    auto boardTime = [](const RouteSegment& s) {
        return s.stops.empty() ? std::string("--:--") : s.stops.front().second;
    };
    auto alightTime = [](const RouteSegment& s) {
        return s.stops.empty() ? std::string("--:--") : s.stops.back().second;
    };

    for (std::size_t p = 0; p < plans.size(); p++) {
        const auto& plan = plans[p];

        int leavesIn = -1;
        if (!plan.segments.empty() && !plan.segments[0].stops.empty()) {
            auto now = std::chrono::system_clock::now();
            leavesIn = TimeHelper::minutesDiff(now, plan.segments[0].board_time);
            if (leavesIn < 0) leavesIn = 0;
        }

        std::string transferText;
        if (plan.num_transfers == 0)      transferText = "direct";
        else if (plan.num_transfers == 1) transferText = "1 transfer";
        else                              transferText = std::to_string(plan.num_transfers) + " transfers";

        // ---- Option header line ----
        std::cout << "\n  "
                  << ansi::BOLD << ansi::WHITE << "Option " << (p + 1) << ansi::RESET;
        if (p == 0) std::cout << ansi::BOLD << ansi::GREEN << " ★" << ansi::RESET;
        std::cout << ansi::DIM << "  ·  " << ansi::RESET
                  << ansi::colorForETA(plan.total_minutes) << ansi::BOLD
                  << plan.total_minutes << " min" << ansi::RESET;
        if (leavesIn >= 0) {
            std::cout << ansi::DIM << "  ·  " << ansi::RESET
                      << ansi::colorForETA(leavesIn);
            if (leavesIn == 0) std::cout << "leaving now";
            else               std::cout << "leaves in " << leavesIn << " min";
            std::cout << ansi::RESET;
        }
        std::cout << ansi::DIM << "  ·  " << transferText << ansi::RESET << "\n";

        // ---- One condensed row per leg ----
        for (std::size_t seg_idx = 0; seg_idx < plan.segments.size(); seg_idx++) {
            const auto& seg = plan.segments[seg_idx];
            bool is_last_segment = (seg_idx == plan.segments.size() - 1);

            int rideMin = TimeHelper::minutesDiff(seg.board_time, seg.alight_time);
            if (rideMin < 0) rideMin = 0;
            int nstops = (int)seg.stops.size() - 1;
            if (nstops < 0) nstops = 0;

            // Left half: badge + "from → to"
            std::ostringstream left;
            left << "    " << ansi::routeBadge(seg.route_id) << "  "
                 << fromName(seg) << ansi::DIM << "  →  " << ansi::RESET
                 << ansi::BOLD << toName(seg) << ansi::RESET;

            // Right half: timing
            std::ostringstream right;
            right << ansi::DIM << boardTime(seg) << " board" << ansi::RESET;
            if (nstops > 0) {
                right << ansi::DIM << "  ·  " << nstops
                      << (nstops == 1 ? " stop" : " stops")
                      << "  ·  " << rideMin << " min" << ansi::RESET;
            }
            if (is_last_segment) {
                right << ansi::DIM << "  ·  " << ansi::RESET
                      << ansi::BOLD << ansi::GREEN
                      << "arrive " << alightTime(seg) << ansi::RESET;
            } else {
                right << ansi::DIM << "  ·  " << ansi::RESET
                      << ansi::BOLD << ansi::YELLOW
                      << "transfer @ " << toName(seg) << ansi::RESET;
            }

            // Compose: pad left half to a column, append right.
            std::string l = left.str();
            std::string r = right.str();
            std::size_t leftCol = (std::size_t)termW > 60
                                  ? std::min<std::size_t>(48, (std::size_t)termW - 30)
                                  : 30;
            std::cout << ansi::padToWidth(l, leftCol) << "  " << r << "\n";
        }
    }

    std::cout << "\n  " << freshnessLine(timestamp) << "\n\n";
}

// ---------------------------------------------------------------------------
// Verbose route plan: per-stop view with vivid action cues.
// ---------------------------------------------------------------------------

namespace {

// Bright pill: " BOARD " on solid background. Picks the right foreground for
// contrast (yellow background needs black text; everything else needs white).
std::string actionPill(const std::string& text, const char* bg, bool blackFg) {
    std::string out;
    out += ansi::BOLD;
    out += bg;
    out += blackFg ? "\033[38;5;16m" : "\033[38;5;231m";
    out += ' ';
    out += text;
    out += ' ';
    out += ansi::RESET;
    return out;
}

constexpr const char* BG_GREEN_PILL  = "\033[48;5;28m";   // arrive / board
constexpr const char* BG_YELLOW_PILL = "\033[48;5;220m";  // transfer

} // namespace

void renderRoutePlanVerbose(const std::vector<RoutePlan>& plans,
                            const std::string& sourceName,
                            const std::string& destName,
                            const std::string& timestamp) {
    int termW = ansi::terminalWidth();
    if (termW < 60) termW = 60;
    if (termW > 140) termW = 140;

    std::cout << "\n";
    {
        std::string left = std::string(ansi::BOLD) + ansi::WHITE
                         + sourceName + "  →  " + destName + ansi::RESET;
        std::string right = std::string(ansi::DIM) + timestamp + ansi::RESET;
        std::size_t lvis = ansi::visibleLength(left);
        std::size_t rvis = ansi::visibleLength(right);
        std::size_t pad = (lvis + rvis < (std::size_t)termW) ? termW - lvis - rvis : 1;
        std::cout << left << std::string(pad, ' ') << right << "\n";
    }
    std::cout << panelRule((std::size_t)termW) << "\n";

    if (plans.empty()) {
        std::cout << "\n  " << ansi::YELLOW
                  << "No route found between these stations."
                  << ansi::RESET << "\n\n";
        std::cout << "  " << freshnessLine(timestamp) << "\n\n";
        return;
    }

    for (std::size_t p = 0; p < plans.size(); p++) {
        const auto& plan = plans[p];

        int leavesIn = -1;
        if (!plan.segments.empty() && !plan.segments[0].stops.empty()) {
            auto now = std::chrono::system_clock::now();
            leavesIn = TimeHelper::minutesDiff(now, plan.segments[0].board_time);
            if (leavesIn < 0) leavesIn = 0;
        }
        std::string transferText;
        if (plan.num_transfers == 0)      transferText = "direct";
        else if (plan.num_transfers == 1) transferText = "1 transfer";
        else                              transferText = std::to_string(plan.num_transfers) + " transfers";

        // ------ Option header ------
        std::cout << "\n  " << ansi::BOLD << ansi::WHITE
                  << "Option " << (p + 1) << ansi::RESET;
        if (p == 0) std::cout << ansi::BOLD << ansi::GREEN << " ★" << ansi::RESET;
        std::cout << ansi::DIM << "  ·  " << ansi::RESET
                  << ansi::colorForETA(plan.total_minutes) << ansi::BOLD
                  << plan.total_minutes << " min" << ansi::RESET;
        if (leavesIn >= 0) {
            std::cout << ansi::DIM << "  ·  " << ansi::RESET
                      << ansi::colorForETA(leavesIn);
            if (leavesIn == 0) std::cout << "leaving now";
            else               std::cout << "leaves in " << leavesIn << " min";
            std::cout << ansi::RESET;
        }
        std::cout << ansi::DIM << "  ·  " << transferText << ansi::RESET << "\n";
        std::cout << "  " << panelRule((std::size_t)termW - 2) << "\n";

        // ------ Per-leg per-stop diagram ------
        for (std::size_t seg_idx = 0; seg_idx < plan.segments.size(); seg_idx++) {
            const auto& seg = plan.segments[seg_idx];
            const char* color = ansi::colorForRoute(seg.route_id);
            bool is_last_segment = (seg_idx == plan.segments.size() - 1);

            int rideMin = TimeHelper::minutesDiff(seg.board_time, seg.alight_time);
            if (rideMin < 0) rideMin = 0;
            int nstops = (int)seg.stops.size() - 1;
            if (nstops < 0) nstops = 0;

            std::string fromN = seg.stops.empty() ? std::string("?") : seg.stops.front().first;
            std::string toN   = seg.stops.empty() ? std::string("?") : seg.stops.back().first;
            std::string dirTag = seg.direction.empty() ? std::string("?") : seg.direction;

            // Segment header line
            std::cout << "\n    " << ansi::routeBadge(seg.route_id) << "  "
                      << ansi::BOLD << fromN << ansi::RESET
                      << ansi::DIM << "  →  " << ansi::RESET
                      << ansi::BOLD << toN << ansi::RESET
                      << ansi::DIM << "    " << dirTag
                      << "  ·  " << nstops << (nstops == 1 ? " stop" : " stops")
                      << "  ·  " << rideMin << " min" << ansi::RESET << "\n";

            // Per-stop diagram
            std::size_t maxNameLen = 0;
            for (const auto& [name, time] : seg.stops) {
                maxNameLen = std::max(maxNameLen, name.size());
            }
            maxNameLen = std::min(maxNameLen, (std::size_t)40);

            for (std::size_t i = 0; i < seg.stops.size(); i++) {
                const auto& [name, time] = seg.stops[i];
                bool isFirst = (i == 0);
                bool isLast  = (i == seg.stops.size() - 1);

                // Bullet column (3 cells)
                std::cout << "      ";
                if (isFirst) {
                    std::cout << ansi::BOLD << ansi::GREEN << "▶ " << ansi::RESET;
                } else if (isLast && is_last_segment) {
                    std::cout << ansi::BOLD << ansi::GREEN << "★ " << ansi::RESET;
                } else if (isLast) {
                    std::cout << ansi::BOLD << ansi::YELLOW << "⇄ " << ansi::RESET;
                } else {
                    std::cout << color << "│ " << ansi::RESET;
                }

                // Station name + time
                std::cout << std::left << std::setw((int)maxNameLen + 2) << name
                          << ansi::DIM << time << ansi::RESET;

                // Action pill
                if (isFirst) {
                    std::cout << "    " << actionPill("BOARD", BG_GREEN_PILL, false);
                } else if (isLast && is_last_segment) {
                    std::cout << "    " << actionPill("ARRIVE", BG_GREEN_PILL, false);
                } else if (isLast) {
                    // Show next leg's badge to make the transfer destination obvious.
                    std::cout << "    " << actionPill("TRANSFER", BG_YELLOW_PILL, true);
                    if (seg_idx + 1 < plan.segments.size()) {
                        std::cout << "  " << ansi::DIM << "→" << ansi::RESET << " "
                                  << ansi::routeBadge(plan.segments[seg_idx + 1].route_id);
                    }
                }
                std::cout << "\n";
            }

            // Walk-between-segments cue
            if (!is_last_segment) {
                std::cout << "         " << ansi::DIM << "┊ walk to "
                          << plan.segments[seg_idx + 1].route_id
                          << " platform" << ansi::RESET << "\n";
            }
        }
    }

    std::cout << "\n  " << freshnessLine(timestamp) << "\n\n";
}
