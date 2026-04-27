# MTA TimeTable

> **A fast, color-coded NYC subway arrival board for your terminal — written in C++.**
> Real-time arrivals, multi-route trip planning, and transfer-complex resolution, all from one quick command.

`mta-timetable` is a Mac/Linux command-line app that pulls live MTA GTFS-realtime feeds and renders the next subway trains at a station — or the best route between two stations — using NYC-station-display conventions: official MTA line colors, urgency-coded ETAs, and side-by-side direction panels. It's built for developers who already live in iTerm2 / Ghostty / Alacritty / kitty and don't want to fish out their phone every time they leave the office.

**Keywords:** NYC subway · MTA realtime · GTFS-realtime · subway timetable CLI · next train terminal · macOS subway tracker · TUI subway · NYC transit terminal app · MTA arrivals command line · C++ GTFS · subway route planner CLI

![Hero — Times Sq arrivals at a glance](images/01-arrivals-hero.png)

---

## Table of contents

- [Features](#features)
- [Screenshots](#screenshots)
- [Quick start](#quick-start)
- [Building from source](#building-from-source)
- [Usage](#usage)
- [How it works](#how-it-works)
- [Data sources](#data-sources)
- [Project layout](#project-layout)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [Acknowledgments](#acknowledgments)
- [License](#license)

---

## Features

- **Real-time arrivals** at any station, grouped by direction (uptown / downtown).
- **NYC-station-display look** — official MTA line bullets (` Q `, ` L `, ` 4 `…) on color backgrounds, the way the signs at every station render them.
- **Time-emphasis coloring** — bright red for "now", green for catchable (1–2 min), yellow for soon (3–5 min), normal for comfortable (6–9 min), dim for further out (10+ min).
- **Grouped arrivals** — same line + direction + destination collapses into one row with up to three follow-up ETAs (`now  10 min  18 min  30 min`), so a busy hub like Times Sq fits on one screen.
- **Multi-station-complex resolution** — searching `Times Sq` automatically pulls in the connected ACE platforms at *42 St-Port Authority Bus Terminal* via `transfers.txt`, with a dim `@ Port Authority` label so you know where to walk.
- **Route planning** — `mta -s X -d Y` returns the best 5 alternatives with line badges, transfer points, board / arrive times, and number of stops.
- **Verbose route view** (`-v`) — per-stop diagram with vivid `▶ BOARD`, `⇄ TRANSFER → [L]`, `★ ARRIVE` action pills.
- **Lead-time filter** (`-t N`) — only show trains/options leaving in `N+` minutes from now (great for "I need 10 min to walk to the station").
- **Smart fuzzy station search** — Levenshtein-backed matching means `bedford`, `bedford ave`, `bedfrod` all resolve to *Bedford Av*.
- **History-based prediction** — running `mta` with no args picks your most likely trip based on time of day and day of week, learned from your usage cache (`./.mta_cache`).
- **Transparency** — every render shows the snapshot timestamp so you know how stale the data is.
- **No telemetry, no tracking, no Electron** — single ~1MB binary, native C++ throughout.

---

## Screenshots

| | |
|---|---|
| **Arrivals at a busy hub.** Side-by-side direction panels at 130+ cols. Each unique train shows once with follow-ups; ETAs colored by urgency. | ![Side-by-side arrivals](images/02-arrivals-wide.png) |
| **Multi-option route plan.** `mta -s "Times Sq" -d "Bedford Av"` — three alternatives, each on its own header + one row per leg, all visible without scrolling. | ![Route plan](images/04-route-plan.png) |
| **Verbose route plan** (`-v`). Per-stop diagram with green `▶ BOARD` / yellow `⇄ TRANSFER → [L]` / green `★ ARRIVE` pills. | ![Verbose route plan](images/05-route-plan-verbose.png) |
| **Lead-time filter.** `mta -s "Bay Ridge-95" -d "Forest Hills" -t 10` shifts the planner clock so it returns options that leave 10+ minutes from now. | ![Lead-time filter](images/06-lead-time-filter.png) |
| **Connected complex resolution.** A search for *Times Sq* surfaces the A/C/E trains at the connected *42 St-Port Authority* platforms, dim-labeled so you know where to walk. | ![Connected complex](images/07-connected-complex.png) |
| **Help.** `mta --help` lists every flag, line group, and example. | ![Help screen](images/08-help.png) |

> **For maintainers capturing screenshots:** see [the screenshot recipes below](#screenshot-recipes-for-maintainers).

---

### Detailed walkthrough — verbose multi-option route plan

When you combine `-v` with a long-distance trip and a lead-time filter, `mta` lays out **every option** as its own per-stop diagram so you can pick the one that best fits how you like to ride. Each option starts with a colored summary header (`★` on the recommended one), then expands into a per-stop strip with `▶ BOARD`, `⇄ TRANSFER → [next line]`, and `★ ARRIVE` pills.

The example below is `mta -s "Bay Ridge-95" -d "Forest Hills" -t 10 -v` — the recommended option (`★`) shown in full per-stop detail. Every transfer point is a station that's actually physically connected per the MTA's `transfers.txt`, so what you see is what you can actually do:

![Verbose route — Option 1](images/09-route-verbose-multi-1.png)

Reading top-to-bottom gives you the full picture of the leg: which station to board, every intermediate stop with its arrival time, where exactly to transfer, and what time you arrive. Subsequent options in the live output use the same visual grammar — once you've parsed one you've parsed them all.

---

## Quick start

> **Homebrew tap is on the roadmap.** Until then, build from source — it takes ~30 seconds on a modern Mac.

```bash
git clone https://github.com/<your-username>/mta-timetable.git
cd mta-timetable
mkdir -p build && cd build
cmake .. && make -j4
./mta -s "Times Sq"
```

That's it. The binary is self-contained — no Python, no Node, no runtime.

To use `mta` from anywhere:

```bash
sudo cp build/mta /usr/local/bin/
mta -s "Times Sq"
```

---

## Building from source

### Prerequisites

| Dependency | Why | macOS install |
|---|---|---|
| **CMake** ≥ 3.10 | Build system | `brew install cmake` |
| **C++17 compiler** | Apple Clang ships with Xcode CLT works fine | `xcode-select --install` |
| **libcurl** | HTTP fetches against MTA feeds | Ships with macOS; for fresh build: `brew install curl` |
| **Protocol Buffers** ≥ 3.0 | Decode GTFS-realtime binary feeds | `brew install protobuf` |
| **Abseil** | Required by recent protobuf | `brew install abseil` |

If you're on Linux, the equivalent packages on Ubuntu/Debian:

```bash
sudo apt-get install -y cmake g++ libcurl4-openssl-dev libprotobuf-dev protobuf-compiler libabsl-dev
```

### Build

```bash
git clone <repo-url> mta-timetable
cd mta-timetable
mkdir -p build && cd build
cmake ..
make -j4
```

Output: `build/mta` — the single binary you'll run.

### Optional: refreshing the static GTFS data

The repo ships with `info/stops.txt` and `info/transfers.txt`, snapshots from the [MTA static GTFS bundle](https://rrgtfsfeeds.s3.amazonaws.com/gtfs_subway.zip). The MTA occasionally adds new stations or transfer-complexes; refresh them with:

```bash
curl -sL -o /tmp/gtfs.zip https://rrgtfsfeeds.s3.amazonaws.com/gtfs_subway.zip
unzip -p /tmp/gtfs.zip stops.txt     > info/stops.txt
unzip -p /tmp/gtfs.zip transfers.txt > info/transfers.txt
make -C build -j4   # rebuild — these are baked in via compile-time macros
```

### Optional: regenerating the GTFS-RT protobuf bindings

The pre-generated `gtfs-realtime.pb.{h,cc}` are committed to keep the build single-step. If Google updates the GTFS-realtime spec and you want fresh bindings:

```bash
brew install protobuf       # if not already
cd /tmp
curl -L -o gtfs-realtime.proto \
  "https://raw.githubusercontent.com/google/transit/master/gtfs-realtime/proto/gtfs-realtime.proto"
protoc --cpp_out=. gtfs-realtime.proto
mv gtfs-realtime.pb.h  /path/to/mta-timetable/include/
mv gtfs-realtime.pb.cc /path/to/mta-timetable/source/
```

---

## Usage

```text
mta-timetable [-s STATION] [-d STATION] [-l LINE] [-t MIN] [-v] [-h]
```

| Flag | What it does |
|---|---|
| `-s, --source <station>` | Source station (fuzzy-matched, e.g. `"14 st"`, `"union sq"`, `"bedfrod"`) |
| `-d, --dest <station>` | Destination station — switches to **route planning** mode |
| `-l, --line <group>` | Filter to one feed group: `ACE`, `BDFM`, `G`, `JZ`, `NQRW`, `L`, `1234567`, `SIR` |
| `-t, --time <min>` | Hide trains/options leaving in less than `<min>` minutes from now (default 0) |
| `-v, --verbose` | Detailed view — per-stop diagram in route mode, full train timeline in arrivals mode |
| `-h, --help` | Print the full help text |

Bare positional argument is treated as `--source`, so `mta "Times Sq"` works too. Running `mta` with no arguments invokes **prediction mode**, which suggests trips based on your local history cache.

### Common recipes

```bash
# Next trains at Union Sq, all directions, all lines:
mta -s "Union Sq"

# Just the L:
mta -s "Bedford Av" -l L

# Best route Times Sq → Bedford, all options visible at once:
mta -s "Times Sq" -d "Bedford Av"

# Same trip, with full per-stop diagram and color-pill action cues:
mta -s "Times Sq" -d "Bedford Av" -v

# I'm 10 minutes from the station — only show options I can actually catch:
mta -s "Bay Ridge-95" -d "Forest Hills" -t 10

# Whatever I'd usually take right now (uses ./.mta_cache):
mta
```

### Reading the display

- **Line badge** (` Q `, ` L `, ` 4 `): MTA's official rondelle, rendered with the matching color background. Yellow lines (NQRW) get a black letter for legibility; everything else uses white.
- **Primary ETA color**:
  - `now` (≤ 0 min): **bold red** — the train is arriving / leaving now.
  - `1–2 min`: **bold green** — go now.
  - `3–5 min`: **yellow** — soon.
  - `6–9 min`: white — comfortable.
  - `10+ min`: dim — far out.
- **Follow-up ETAs**: dim minutes-from-now for the next 1–3 trains of the same line+direction+destination. `now  10 min  18 min  30 min` reads as "next is now, then in 10, 18, and 30 minutes from current time."
- **`@ <platform>` suffix** (dim): the train is at a connected platform with a *different* name from your search. For example, A/C/E trains at *Times Sq* are physically at *42 St-Port Authority Bus Terminal* — the suffix tells you to walk.
- **Snapshot footer**: the time the data was fetched, in NY time. Stale-aware by design.

In route plans:
- `★` next to **Option 1** marks the planner's recommended option (fewest transfers, then fastest).
- `▶ BOARD`, `⇄ TRANSFER → [L]`, `★ ARRIVE` pills appear in `-v` mode with solid color backgrounds for unmistakable visual cues.

---

## How it works

```
┌────────────┐     8 GTFS-RT feeds      ┌─────────────────┐
│ MTA api.   │ ───────────────────────▶ │ subway_feed_    │
│ mta.info   │   protobuf over HTTPS    │ client (curl +  │
└────────────┘                          │ protobuf)       │
                                        └────────┬────────┘
                                                 │ DetailedTripUpdate
                                                 ▼
┌──────────────┐    fuzzy match     ┌──────────────────────┐
│  CLI parse   │ ─────────────────▶ │ stop_lookup          │
│ (cli_parser) │   transfer-complex │ (stops.txt +         │
└──────┬───────┘    expansion       │  transfers.txt)      │
       │                            └──────┬───────────────┘
       │                                   │ StationMatch
       ▼                                   ▼
┌──────────────────────────────────────────────────────────┐
│  Source-only mode:                                       │
│    filter trips to {station N/S stops × complex parents} │
│    sort by ETA, group by (route, dir, dest, platform),   │
│    apply -t lead-time filter                             │
│  Routing mode:                                           │
│    findRoutes() — Dijkstra over transfer graph,          │
│    capped at 5 options + dedup by line pattern           │
└────────────────┬─────────────────────────────────────────┘
                 ▼
┌──────────────────────────────────────────┐
│  output_formatter (ANSI-aware,           │
│  width-adaptive, terminal-detect)        │
└──────────────────────────────────────────┘
```

Key design choices:
- **One-shot CLI, not a TUI.** `mta` prints and exits. No event loop, no curses. Composes naturally with shell pipes (`mta -s X | grep N`), tmux panes, and Hammerspoon menubar scripts.
- **Side-by-side panels** above ~130 cols, single-column below — chosen so destination names never get arbitrarily truncated on small terminals.
- **Fixed-column timing block** so primary + 3 follow-up ETAs align across every row, regardless of how many actual follow-ups exist.
- **Compile-time data files.** `stops.txt`, `transfers.txt`, and `MTA_facts.txt` paths are baked in via CMake `target_compile_definitions` — no runtime path resolution, no XDG fuss.
- **Cache lives alongside the binary** as `.mta_cache` (relative path, current working directory).

---

## Data sources

| Source | What we use it for | Where |
|---|---|---|
| MTA GTFS-realtime feeds | Live trip updates, ETAs, route IDs | `https://api.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-*` ([MTA developer portal](https://api.mta.info/)) |
| MTA static GTFS bundle | `stops.txt` (station IDs, names, lat/lon) and `transfers.txt` (connected complexes) | `https://rrgtfsfeeds.s3.amazonaws.com/gtfs_subway.zip` |
| Google Transit `gtfs-realtime.proto` | Protobuf schema for decoding the realtime feeds | [google/transit on GitHub](https://github.com/google/transit/tree/master/gtfs-realtime) |

The realtime feeds are public and unauthenticated as of the MTA's 2020 API simplification. There's no API key to register.

---

## Project layout

```
.
├── CMakeLists.txt              # build configuration
├── main.cpp                    # CLI entry — argparse, dispatch, fetch loop
├── include/
│   ├── ansi_colors.h           # MTA palette + line badges + width helpers
│   ├── cli_parser.h            # CliOptions + parseArgs
│   ├── gtfs-realtime.pb.h      # generated protobuf (huge, do not edit)
│   ├── mta_cache.h             # local trip-history cache for prediction mode
│   ├── mta_subway_feed.h       # endpoint URLs, line-group constants
│   ├── output_formatter.h      # render structs (TrainDisplay)
│   ├── route_planner.h         # findRoutes() + RoutePlan/Segment types
│   ├── server_pinger.h         # health-check helper
│   ├── stop_lookup.h           # stops.txt + transfers.txt loader, fuzzy match
│   ├── subway_feed_client.h    # GTFS-RT fetch + parse
│   └── time_helper.h           # America/New_York time formatting
├── source/                     # implementations matching the headers above
├── info/
│   ├── stops.txt               # static GTFS — station table
│   ├── transfers.txt           # static GTFS — transfer-complex graph
│   └── MTA_facts.txt           # trivia shown occasionally
└── README.MD                   # you are here
```

---

## Roadmap

- [ ] Homebrew tap (`brew install mta-timetable`) for one-line install on macOS.
- [ ] `--watch` / `--tui` mode with auto-refresh and `j/k` navigation (lazygit-style).
- [ ] Service-alert integration (planned diversions, suspensions).
- [ ] First-run setup that saves a "home station" so bare `mta` is even more useful.
- [ ] Best-train-car / best-exit hints (Citymapper-style) where the data supports it.
- [ ] `--json` / `--plain` output modes for shell scripting.

See `RECOMMENDATIONS.md` for the longer research-backed product plan.

---

## Contributing

Issues and PRs welcome. A few guidelines:

- Keep the binary single-file and dependency-light. New features that drag in heavy libraries need a strong case.
- The fast path is the unauthenticated MTA realtime fetch — any change that would slow startup past ~3 seconds on a typical fiber connection should be opt-in.
- Match the existing layout idiom: side-by-side panels for ≥ 130 cols, single-column below; fixed-column timing block; MTA palette for line bullets.
- ANSI escapes go through `include/ansi_colors.h`. Don't sprinkle raw `\033[...m` elsewhere.

---

## Acknowledgments

- The **MTA** for publishing GTFS-realtime feeds without an API key.
- **lazygit, k9s, btop, fzf, atuin** — the TUI/CLI tools whose UX standards this project tries to live up to.
- **Andrew Dickinson's [`nyct-gtfs`](https://github.com/Andrew-Dickinson/nyct-gtfs)** for the clearest write-up I've seen on the *quirks* of MTA's realtime data (ghost trains, mutating IDs).
- The protobuf and Abseil teams at Google.

---

## License

Released under the **MIT License** — see [`LICENSE`](LICENSE) for the full text. In short: do what you want with it, just keep the copyright notice intact.

---

## Screenshot recipes for maintainers

When refreshing the screenshots, run each command in a clean terminal at the specified width and save the PNG with the suggested filename. Screenshots are checked into `images/` so the README renders without network requests.

| Filename | Terminal width (cols) | Command | What it should show |
|---|---:|---|---|
| `images/01-arrivals-hero.png` | 140 | `mta -s "Times Sq"` | The marquee shot — side-by-side panels, multiple line groups, mix of `now` / green / yellow / dim ETAs. |
| `images/02-arrivals-wide.png` | 140 | `mta -s "14 St-Union Sq"` | Another side-by-side example with 4/5/6 + L + N/Q/R lines, several lines visible at once. |
| `images/04-route-plan.png` | 130 | `mta -s "Times Sq" -d "Bedford Av"` | 2–3 options stacked, each on a 1-line header + 2 leg rows, `★` on Option 1. |
| `images/05-route-plan-verbose.png` | 130 | `mta -s "Times Sq" -d "Bedford Av" -v` | Per-stop diagram with `▶ BOARD` / `⇄ TRANSFER → [L]` / `★ ARRIVE` colored pills. |
| `images/06-lead-time-filter.png` | 130 | `mta -s "Bay Ridge-95" -d "Forest Hills" -t 10` | Several options now appear; show that the planner found trains 10+ min out. |
| `images/07-connected-complex.png` | 130 | `mta -s "Times Sq"` | Crop to a section showing the dim `@ Port Authority` suffix on A/C/E trains. |
| `images/08-help.png` | 100 | `mta --help` | Full help screen, color-coded line groups visible. |
| `images/09-route-verbose-multi-1.png` | 130 | `mta -s "Bay Ridge-95" -d "Forest Hills" -t 10 -v` | Crop to **Option 1 only** — the starred recommendation, every leg with per-stop detail. |

Tips for cleaner screenshots:
- Use a consistent font (JetBrains Mono, Berkeley Mono, SF Mono, or Menlo at 13–14pt).
- Set a neutral background (e.g., iTerm2's default Pro / "Solarized Dark" or a flat near-black).
- Capture with `Cmd+Shift+4` on macOS, then crop to the relevant rows so each image has high signal-to-pixel ratio.
- For the hero, pick a time when at least one train is `now` and several are in the green/yellow range — it makes the urgency coloring obvious.
- Save as PNG (not JPG); the colored badges have hard edges that JPG mangles.
