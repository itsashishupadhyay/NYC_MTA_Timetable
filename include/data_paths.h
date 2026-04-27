#pragma once

#include <cstdlib>
#include <string>

// Resolve the on-disk path for one of the bundled data files
// (stops.txt, transfers.txt, MTA_facts.txt).
//
// Resolution order:
//   1. $MTA_DATADIR/<filename>     (runtime override — Homebrew post-install
//                                   relocation, custom user setup, etc.)
//   2. compile_time_default        (baked in by CMakeLists.txt:MTA_DATADIR)
//
// The runtime override is what makes the installed binary relocatable: a
// distro/Homebrew package can move the data files later without rebuilding.
inline std::string mtaDataPath(const char* filename, const char* compile_time_default) {
    if (const char* dir = std::getenv("MTA_DATADIR")) {
        if (*dir) {
            std::string out = dir;
            if (!out.empty() && out.back() != '/') out.push_back('/');
            out += filename;
            return out;
        }
    }
    return compile_time_default;
}
