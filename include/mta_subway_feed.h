#ifndef MTA_SUBWAY_REALTIME_FEEDS_H
#define MTA_SUBWAY_REALTIME_FEEDS_H

/*
 * MTA NYC Subway GTFS-RT feed endpoints
 *
 * Source: https://api.mta.info/#/subwayRealTimeFeeds
 *
 * MTA real-time subway feeds are grouped by line families:
 *
 *   - ACE    → A, C, E
 *   - BDFM   → B, D, F, M
 *   - G      → G
 *   - JZ     → J, Z
 *   - NQRW   → N, Q, R, W
 *   - L      → L
 *   - 123456 → 1, 2, 3, 4, 5, 6 and GC Shuttle (A Division core)
 *   - 7      → 7
 *   - SIR    → Staten Island Railway
 *
 * Each endpoint returns GTFS-Realtime protobuf data.
 */

#ifdef __cplusplus
extern "C" {
#endif

// If not provided by the build (CMake), default to project-relative path
#ifndef MTA_STOP_DETAILS
#define MTA_STOP_DETAILS "info/stops.txt"
#endif

/* Base URL prefix (without key/header) */
#define MTA_SUBWAY_ENDPOINT "https://api-endpoint.mta.info"  
#define MTA_SUBWAY_GTFSRT_BASE MTA_SUBWAY_ENDPOINT "/Dataservice/mtagtfsfeeds/"

/* Individual full feed URLs (percent-encoded, as used by MTA) */

#define MTA_SUBWAY_FEED_URL_ACE      MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-ace"
#define MTA_SUBWAY_FEED_URL_BDFM     MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-bdfm"
#define MTA_SUBWAY_FEED_URL_G        MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-g"
#define MTA_SUBWAY_FEED_URL_JZ       MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-jz"
#define MTA_SUBWAY_FEED_URL_NQRW     MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-nqrw"
#define MTA_SUBWAY_FEED_URL_L        MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-l"
#define MTA_SUBWAY_FEED_URL_1234567  MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs"
#define MTA_SUBWAY_FEED_URL_SIR      MTA_SUBWAY_GTFSRT_BASE "nyct%2Fgtfs-si"

/*
 * If you want to work with them programmatically, use this struct + table.
 */

typedef struct {
    const char *id;          /* Short group id, e.g. "ACE" */
    const char *lines;       /* Human-readable lines in this feed, e.g. "A,C,E" */
    const char *url;         /* Full GTFS-RT endpoint URL */
} MtaSubwayFeedInfo;

static const MtaSubwayFeedInfo MTA_SUBWAY_FEEDS[] = {
    { "ACE",    "A,C,E",              MTA_SUBWAY_FEED_URL_ACE    },
    { "BDFM",   "B,D,F,M",            MTA_SUBWAY_FEED_URL_BDFM   },
    { "G",      "G",                  MTA_SUBWAY_FEED_URL_G      },
    { "JZ",     "J,Z",                MTA_SUBWAY_FEED_URL_JZ     },
    { "NQRW",   "N,Q,R,W",            MTA_SUBWAY_FEED_URL_NQRW   },
    { "L",      "L",                  MTA_SUBWAY_FEED_URL_L      },
    { "1234567", "1,2,3,4,5,6,7,S",    MTA_SUBWAY_FEED_URL_1234567 },
    { "SIR",     "Staten Island Rwy", MTA_SUBWAY_FEED_URL_SIR     },
};

static const int MTA_SUBWAY_FEEDS_COUNT =
    (int)(sizeof(MTA_SUBWAY_FEEDS) / sizeof(MTA_SUBWAY_FEEDS[0]));



#define SUBWAY_COLLOQUIAL(line) (                         \
    /* Blue Line */                                       \
    (line == "A" || line == "C" || line == "E" ||         \
     line == "ACE" || line == "H" || line == "ACEH")      \
        ? "Blue Line (A/C/E)"                             \
    /* Orange Line */                                     \
    : (line == "B" || line == "D" || line == "F" ||       \
       line == "M" || line == "BDFM" || line == "FS" ||   \
       line == "BDFMFS")                                  \
        ? "Orange Line (B/D/F/M)"                         \
    /* Light Green */                                     \
    : (line == "G")                                       \
        ? "Light Green Line (G)"                          \
    /* Brown */                                           \
    : (line == "J" || line == "Z" || line == "JZ")        \
        ? "Brown Line (J/Z)"                              \
    /* Yellow */                                          \
    : (line == "N" || line == "Q" || line == "R" ||       \
       line == "W" || line == "NQRW")                     \
        ? "Yellow Line (N/Q/R/W)"                         \
    /* Gray */                                            \
    : (line == "L")                                       \
        ? "Gray Line (L)"                                 \
    /* Red Line */                                        \
    : (line == "1" || line == "2" || line == "3" ||       \
       line == "123")                                     \
        ? "Red Line (1/2/3)"                              \
    /* Dark Green */                                      \
    : (line == "4" || line == "5" || line == "6" ||       \
       line == "456")                                     \
        ? "Green Line (4/5/6)"                            \
    /* Purple */                                          \
    : (line == "7")                                       \
        ? "Purple Line (7)"                               \
    /* Shuttles */                                        \
    : (line == "S" || line == "GS" || line == "42S")      \
        ? "Shuttle (S)"                                   \
    /* SIR */                                             \
    : (line == "SIR")                                     \
        ? "Staten Island Railway"                         \
    /* Default */                                         \
    : "Unknown Line"                                      \
)



#ifdef __cplusplus
}
#endif

#endif /* MTA_SUBWAY_REALTIME_FEEDS_H */
