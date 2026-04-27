#pragma once

#include <string>
#include <optional>

struct CliOptions {
    std::optional<std::string> source_query;  // empty = cache prediction mode
    std::optional<std::string> dest_query;    // destination station (triggers routing mode)
    std::optional<std::string> line;          // feed group ID: ACE, BDFM, G, JZ, NQRW, L, 1234567, SIR
    int  min_lead_minutes = 0;                // hide trains/options leaving in < N min
    bool verbose = false;
    bool help = false;
};

CliOptions parseArgs(int argc, char* argv[]);
void printUsage(const char* progName);
