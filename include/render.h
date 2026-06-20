#ifndef ATC_RENDER_H
#define ATC_RENDER_H

#include "sim.h"

/* Runs the full interactive application: opens a window, drives the
 * simulation under a wall-clock-to-sim-time mapping, and renders the
 * radar scope plus the data sidebar. Returns the process exit code. */
int run_app(uint64_t seed, int num_runways, int num_gates,
            double mean_interarrival, double horizon);

/* Renders a single frame of the scene advanced to `at_minute` and writes
 * it to `outfile` as a PNG, then exits. Used to regenerate the README
 * preview image. Returns the process exit code.                       */
int run_snapshot(uint64_t seed, int num_runways, int num_gates,
                 double mean_interarrival, double horizon,
                 double at_minute, const char *outfile);

#endif /* ATC_RENDER_H */
