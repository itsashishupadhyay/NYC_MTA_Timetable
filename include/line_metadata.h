#pragma once

#include <string>

// Trains and line name that serve a given parent station, derived from the
// MTA GTFS stop_id allocation. Empty fields when the prefix isn't recognized.
struct LineInfo {
    const char* trains;     // e.g. "F", "D", "A/C/E"
    const char* line_name;  // e.g. "Culver Line", "West End Line"
};

// Map a parent stop_id (e.g. "F32", "101", "R20") to its line family.
//
// The MTA's parent-stop_id allocation is letter+number (e.g. "F32") for IND/BMT
// and 3-digit numeric (e.g. "127") for IRT. The letter+range buckets a station
// by physical line (Culver, West End, Sea Beach, ...). This is what the
// disambiguation picker uses to tell a user that three stations all named
// "Bay Pkwy" actually sit on three different lines.
//
// Returns {"", ""} for IDs whose prefix isn't recognized.
LineInfo lineInfoFor(const std::string& parent_stop_id);
