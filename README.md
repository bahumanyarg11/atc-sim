# Sector Control

A discrete-event airport traffic simulator written in C, with a real-time
radar scope rendered through [raylib](https://www.raylib.com/).

The simulator does not run on a fixed game loop. Time is advanced by a binary
min-heap priority queue: the clock jumps directly to the timestamp of the next
scheduled event. The rendering layer runs independently at 60 FPS and
interpolates aircraft positions between event anchors, so the scope stays smooth
while the scheduler resolves traffic as fast as the event stream allows.

![radar scope](docs/scope.png)

## What it models

- **Real-world traffic.** Every flight draws a plausible identity from a
  curated dataset of actual airlines (United, Lufthansa, Emirates, Singapore,
  IndiGo, …), in-service ICAO aircraft types with their wake categories
  (A320, B77W, A359, E190, …), and major origin airports (LHR, DXB, SIN, JFK,
  …). The full table lives in [`src/dataset.c`](src/dataset.c).
- **Arrivals** enter the sector on a Poisson process (exponential
  interarrival times) and request a runway.
- **Runways** (1–4) are exclusive resources held for landing or takeoff.
- **Gates** (1–12) handle turnaround; aircraft taxi in, sit for servicing,
  then taxi back out for departure.
- **Wake-turbulence separation**: heavier aircraft hold a runway longer. The
  occupancy time scales with the ICAO wake category (Light / Medium / Heavy /
  Super) carried by each real aircraft type.
- **Weather** drifts between `CLEAR`, `WINDY`, and `STORM`. Worse weather
  lengthens runway occupancy and raises the chance of a go-around.
- **Go-arounds (missed approaches)**: a fraction of landings are waved off —
  randomly (more often in bad weather) or on the controller's command — and
  re-enter the holding pattern.
- **Holding patterns** absorb arrivals when every runway is busy. Aircraft in
  holding keep burning fuel.
- **Fuel emergencies**: fuel decays as `F(t) = F0 − α·Δt`. Below a threshold an
  aircraft declares an emergency and preempts the landing queue. If it drops to
  critical before a runway opens, it is recorded as lost.
- **Deadlock avoidance**: arrivals that land but find no free gate wait on the
  taxiway instead of locking a runway, so inbound traffic is never blocked by
  full gates.
- **Controller score**: a running performance score rewards clean, low-delay
  departures and penalises losses, diversions, emergencies, and go-arounds.

Every spawned aircraft is conserved: it ends `DEPARTED`, `LOST`, or (only on a
controller command) `DIVERTED` — never stranded. The `DEPARTED + LOST ==
SPAWNED` invariant is checked in the automated test suite, including under
deliberate saturation.

## Building

Requires CMake ≥ 3.16 and a C11 compiler. raylib is fetched and built
automatically if it is not already installed — no manual dependency setup.

```sh
cmake -B build
cmake --build build
```

### macOS (including Apple Silicon)

```sh
xcode-select --install        # if you don't already have the toolchain
brew install cmake
cmake -B build && cmake --build build
./build/sector_control
```

### Linux

```sh
# Debian/Ubuntu: raylib needs X11/GL dev headers to build from source
sudo apt install build-essential cmake libgl1-mesa-dev libx11-dev \
     libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake -B build && cmake --build build
./build/sector_control
```

### Windows

Use the Visual Studio CMake integration, or:

```sh
cmake -B build
cmake --build build --config Release
build\Release\sector_control.exe
```

## Running

```sh
./build/sector_control                          # default scenario
./build/sector_control --scenario rush          # a preset busy-hour scenario
./build/sector_control --runways 1 --gates 4 --rate 0.5    # busy single runway
./build/sector_control --headless --seed 42     # one run, prints a report
./build/sector_control --runs 50 --scenario rush > runs.csv  # 50-seed CSV sweep
```

Options:

| flag           | meaning                                       | default |
|----------------|-----------------------------------------------|---------|
| `--seed N`     | PRNG seed (runs are fully reproducible)       | 1       |
| `--runways N`  | number of runways, 1–4                        | 2       |
| `--gates N`    | number of gates, 1–12                         | 8       |
| `--rate F`     | mean minutes between arrivals                 | 1.5     |
| `--horizon F`  | stop spawning after this many sim minutes     | 720     |
| `--scenario S` | preset: `calm`, `rush`, `storm`, `night`      | —       |
| `--headless`   | no window; run to completion, print a report  | off     |
| `--csv`        | with `--headless`, print one CSV row instead  | off     |
| `--runs N`     | run N consecutive seeds headless → CSV table  | 1       |
| `--snapshot F` | render a single frame to PNG file `F` and exit | —      |

### Batch analysis

`--runs N` runs `N` consecutive seeds to completion and emits a CSV table
(`spawned, departed, lost, diverted, emergencies, go_arounds, mean_wait,
peak_wait, score, …`), which makes it easy to sweep configurations and compare
outcomes in a spreadsheet or notebook without ever opening a window.

### Controls

| key / mouse | action                         |
|-------------|--------------------------------|
| `Click`     | select an aircraft on the scope |
| `E`         | **expedite** the selected flight (priority for the next runway) |
| `H`         | **hold** the selected inbound (send to the holding pattern) |
| `D`         | **divert** the selected flight to an alternate field |
| `G`         | **go-around** — wave off the selected flight on final |
| `Space`     | pause / resume                 |
| `1 2 3 4`   | time warp 1× / 5× / 25× / 100× |
| `R`         | reset with a new seed          |
| `F1`        | toggle the in-app reference / legend |
| `Esc`       | quit                           |

### Controller decisions

The scope is interactive: click any blip to pull its flight strip into the
sidebar — callsign, airline, aircraft type, origin, fuel, wake category, and
queue wait. From there you (the controller) make sequencing decisions instead
of only watching:

- **Expedite** bumps a holding aircraft to the front of the landing queue; the
  next runway to open serves it first.
- **Hold** sends an inbound into the holding pattern, deferring it behind other
  traffic.
- **Divert** releases any resources the aircraft holds and sends it to an
  alternate, recorded in the `diverted` tally.
- **Go-around** waves a flight off short final; it re-enters the pattern and
  tries again.

Emergencies still preempt everything automatically — your expedite never jumps
ahead of a fuel-critical aircraft. Every decision is scored, so the running
**controller score** in the sidebar reflects how well you are sequencing
traffic.

### Live telemetry

The interface is a full ops console, not just a scope:

- a **sidebar** of stat cards (score, in-sector count, departures, mean wait,
  emergencies, losses, diversions, go-arounds), runway/gate state, and the
  selected flight strip with its command buttons;
- a **local clock** and a **weather pill** that track the simulated day;
- a bottom **event-log ticker** narrating arrivals, clearances, emergencies,
  go-arounds, and weather changes in real time;
- a **throughput sparkline** of departures over time.

## Architecture

Three independent layers:

```
  src/pqueue.c   binary min-heap priority queue   (push/pop O(log N))
  src/rng.c      xoshiro256** PRNG, seed-reproducible
  src/dataset.c  real-world airline / aircraft / airport reference data
  src/sim.c      event-driven core: resources, contention, fuel, decisions
  src/render.c   raylib radar scope, flight strips + controller controls
  src/main.c     CLI / mode selection
```

`atc_core` (queue + rng + sim) has no graphics dependency and is compiled on
its own for the test target, so the simulation logic can be exercised in CI
without a display.

### Determinism

The event ordering is a total order: earliest timestamp first, ties broken by
priority weight, then by insertion sequence (FIFO). Combined with the seeded
PRNG, a given seed and configuration always produces an identical run — useful
for debugging and for regression tests.

## Tests

```sh
cmake --build build
ctest --test-dir build --output-on-failure
# or directly:
./build/test_core
```

Covered: heap ordering and tie-breaking, a 100k-event heap stress check, PRNG
determinism, run-to-run reproducibility, and flow conservation under both
normal and saturated load.

Run under Valgrind for a clean memory report:

```sh
valgrind --leak-check=full ./build/test_core
```

## License

MIT. See [LICENSE](LICENSE).
