#include "sim.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    printf(
"Sector Control — discrete-event airport traffic simulator\n\n"
"usage: %s [options]\n\n"
"  --seed N           PRNG seed for reproducible runs (default 1)\n"
"  --runways N        number of runways, 1-4 (default 2)\n"
"  --gates N          number of terminal gates, 1-12 (default 8)\n"
"  --rate F           mean minutes between arrivals (default 1.5)\n"
"  --horizon F        stop spawning after this many sim minutes (default 720)\n"
"  --scenario NAME    preset: calm | rush | storm | night\n"
"  --headless         run to completion and print a report, no window\n"
"  --csv              with --headless, print one CSV row instead of a report\n"
"  --runs N           run N consecutive seeds headless and print a CSV table\n"
"  --snapshot FILE    render one frame to FILE (PNG) and exit\n"
"  --help             show this message\n",
        prog);
}

typedef struct {
    uint64_t seed;
    int      runways, gates;
    double   rate, horizon;
} Config;

static void apply_scenario(Config *c, const char *name)
{
    if      (!strcmp(name, "calm"))  { c->rate = 3.0; c->runways = 2; c->gates = 10; }
    else if (!strcmp(name, "rush"))  { c->rate = 0.8; c->runways = 2; c->gates = 8;  }
    else if (!strcmp(name, "storm")) { c->rate = 1.1; c->runways = 2; c->gates = 8;  }
    else if (!strcmp(name, "night")) { c->rate = 4.0; c->runways = 1; c->gates = 6;  }
    else fprintf(stderr, "unknown scenario '%s' (using defaults)\n", name);
}

static void print_report(const Simulation *s)
{
    printf("\n==== run complete ====\n");
    printf("seed                 : %llu\n", (unsigned long long)s->seed);
    printf("runways / gates      : %d / %d\n", s->num_runways, s->num_gates);
    printf("final sim clock (min): %.1f\n", s->now);
    printf("flights spawned      : %d\n", s->stats.flights_spawned);
    printf("flights departed     : %d\n", s->stats.flights_departed);
    printf("aircraft lost        : %d\n", s->stats.flights_lost);
    printf("flights diverted     : %d\n", s->stats.flights_diverted);
    printf("emergencies declared : %d\n", s->stats.emergencies);
    printf("go-arounds flown     : %d\n", s->stats.go_arounds);
    printf("mean queue wait (min): %.2f\n", sim_mean_wait(s));
    printf("peak queue wait (min): %.2f\n", s->stats.max_wait);
    printf("controller score     : %.0f\n", sim_score(s));
    printf("events dispatched    : %ld\n", s->stats.events_processed);
    printf("======================\n");
}

static void csv_header(void)
{
    printf("seed,runways,gates,rate,horizon,spawned,departed,lost,diverted,"
           "emergencies,go_arounds,mean_wait,peak_wait,score,events\n");
}

static void csv_row(const Config *c, const Simulation *s)
{
    printf("%llu,%d,%d,%.2f,%.0f,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.0f,%ld\n",
           (unsigned long long)c->seed, s->num_runways, s->num_gates,
           c->rate, c->horizon,
           s->stats.flights_spawned, s->stats.flights_departed,
           s->stats.flights_lost, s->stats.flights_diverted,
           s->stats.emergencies, s->stats.go_arounds,
           sim_mean_wait(s), s->stats.max_wait, sim_score(s),
           s->stats.events_processed);
}

static void run_one(const Config *c, Simulation *s)
{
    sim_init(s, c->seed, c->runways, c->gates, c->rate, c->horizon);
    sim_run_to_end(s);
}

int main(int argc, char **argv)
{
    Config cfg = { .seed = 1, .runways = 2, .gates = 8, .rate = 1.5, .horizon = 720.0 };
    bool  headless = false, csv = false;
    int   runs = 1;
    const char *snapshot = NULL;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--help"))     { usage(argv[0]); return 0; }
        else if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--csv"))      csv = true;
        else if (!strcmp(argv[i], "--seed")     && i+1 < argc) cfg.seed    = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--runways")  && i+1 < argc) cfg.runways = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--gates")    && i+1 < argc) cfg.gates   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rate")     && i+1 < argc) cfg.rate    = atof(argv[++i]);
        else if (!strcmp(argv[i], "--horizon")  && i+1 < argc) cfg.horizon = atof(argv[++i]);
        else if (!strcmp(argv[i], "--runs")     && i+1 < argc) runs        = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scenario") && i+1 < argc) apply_scenario(&cfg, argv[++i]);
        else if (!strcmp(argv[i], "--snapshot") && i+1 < argc) snapshot    = argv[++i];
        else { fprintf(stderr, "unknown option: %s\n\n", argv[i]); usage(argv[0]); return 1; }
    }

    if (snapshot) {
        return run_snapshot(cfg.seed, cfg.runways, cfg.gates, cfg.rate,
                            cfg.horizon, 60.0, snapshot);
    }

    /* batch sweep: N seeds -> CSV table */
    if (runs > 1) {
        csv_header();
        for (int k = 0; k < runs; k++) {
            Config c = cfg; c.seed = cfg.seed + (uint64_t)k;
            Simulation s; run_one(&c, &s);
            csv_row(&c, &s);
            sim_free(&s);
        }
        return 0;
    }

    if (headless) {
        Simulation s; run_one(&cfg, &s);
        if (csv) { csv_header(); csv_row(&cfg, &s); }
        else     print_report(&s);
        sim_free(&s);
        return 0;
    }

    return run_app(cfg.seed, cfg.runways, cfg.gates, cfg.rate, cfg.horizon);
}
