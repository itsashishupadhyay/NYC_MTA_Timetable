#include "cli_parser.h"
#include "ansi_colors.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>

static const char* VALID_LINES[] = {
    "ACE", "BDFM", "G", "JZ", "NQRW", "L", "1234567", "SIR"
};

static std::string toUpper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return out;
}

static bool isValidLine(const std::string& line) {
    std::string upper = toUpper(line);
    for (const auto& valid : VALID_LINES) {
        if (upper == valid) return true;
    }
    return false;
}

CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.help = true;
            return opts;
        }

        if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
            continue;
        }

        if ((arg == "--source" || arg == "-s") && i + 1 < argc) {
            opts.source_query = argv[++i];
            continue;
        }

        if ((arg == "--dest" || arg == "-d") && i + 1 < argc) {
            opts.dest_query = argv[++i];
            continue;
        }

        if ((arg == "--time" || arg == "-t") && i + 1 < argc) {
            std::string val = argv[++i];
            try {
                int v = std::stoi(val);
                if (v < 0) v = 0;
                opts.min_lead_minutes = v;
            } catch (...) {
                std::cerr << ansi::RED << "Error: --time expects an integer (minutes), got '"
                          << val << "'" << ansi::RESET << "\n";
                std::exit(1);
            }
            continue;
        }

        if ((arg == "--line" || arg == "-l") && i + 1 < argc) {
            std::string val = argv[++i];
            if (!isValidLine(val)) {
                std::cerr << ansi::RED << "Error: Unknown line group '" << val << "'" << ansi::RESET << "\n";
                std::cerr << "Valid options: ACE, BDFM, G, JZ, NQRW, L, 1234567, SIR\n";
                std::exit(1);
            }
            opts.line = toUpper(val);
            continue;
        }

        // If no flag prefix, treat as source query (positional argument)
        if (arg[0] != '-' && !opts.source_query) {
            opts.source_query = arg;
            continue;
        }

        std::cerr << ansi::RED << "Error: Unknown option '" << arg << "'" << ansi::RESET << "\n";
        printUsage(argv[0]);
        std::exit(1);
    }

    // No source is valid — triggers cache prediction mode
    return opts;
}

void printUsage(const char* progName) {
    std::cout << ansi::BOLD << "mta" << ansi::RESET
              << ansi::DIM  << "  —  NYC subway arrivals & route planner for the terminal"
              << ansi::RESET << "\n\n"
              << "Usage:\n"
              << "  " << progName << " -s <station> [-d <station>] [-l <group>] [-t <min>] [-v]\n"
              << "  " << progName << " <station> [-d <station>] [-l <group>] [-t <min>] [-v]\n"
              << "  " << progName << "                    " << ansi::DIM
              << "(no args = smart prediction from history)" << ansi::RESET << "\n\n"
              << "Options:\n"
              << "  -s, --source <name>   Source station name (fuzzy matched)\n"
              << "  -d, --dest <name>     Destination station (enables routing mode)\n"
              << "  -l, --line <group>    Filter by line group (default: all)\n"
              << "  -t, --time <min>      Only show trains leaving in N+ min from now (default: 0)\n"
              << "  -v, --verbose         Detailed view (per-stop diagram for routing)\n"
              << "  -h, --help            Show this help\n\n"
              << "When run with no arguments, " << ansi::BOLD << "mta" << ansi::RESET
              << " predicts your route based on travel\n"
              << "history, time of day, and day of week.\n\n"
              << "Line groups:\n"
              << "  " << ansi::BLUE    << "ACE"    << ansi::RESET << "     A, C, E\n"
              << "  " << ansi::ORANGE  << "BDFM"   << ansi::RESET << "    B, D, F, M\n"
              << "  " << ansi::LIGHT_GREEN << "G"   << ansi::RESET << "       G\n"
              << "  " << ansi::BROWN   << "JZ"     << ansi::RESET << "      J, Z\n"
              << "  " << ansi::YELLOW  << "NQRW"   << ansi::RESET << "    N, Q, R, W\n"
              << "  " << ansi::GRAY    << "L"      << ansi::RESET << "       L\n"
              << "  " << ansi::RED     << "1234567" << ansi::RESET << " 1, 2, 3, 4, 5, 6, 7, S\n"
              << "  " << ansi::BLUE    << "SIR"    << ansi::RESET << "     Staten Island Railway\n\n"
              << "Examples:\n"
              << "  " << progName << " -s \"34 St\" -l ACE\n"
              << "  " << progName << " \"DeKalb\" -v\n"
              << "  " << progName << " \"Times Sq\"\n"
              << "  " << progName << " -s \"Penn Station\" -d \"DeKalb Av\"\n"
              << "  " << progName << " -s \"Bay Ridge-95\" -d \"Forest Hills\" -t 10\n";
}
