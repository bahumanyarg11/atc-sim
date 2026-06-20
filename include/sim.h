#ifndef ATC_SIM_H
#define ATC_SIM_H

#include "types.h"
#include "pqueue.h"
#include "rng.h"

typedef enum {
    RW_IDLE,
    RW_LANDING,
    RW_TAKEOFF,
    RW_LOCKED
} RunwayState;

typedef enum {
    GATE_VACANT,
    GATE_RESERVED,
    GATE_OCCUPIED
} GateState;

/* Sector weather. Worse weather slows runways and raises go-arounds. */
typedef enum {
    WX_CLEAR,
    WX_WINDY,
    WX_STORM
} Weather;

typedef struct {
    RunwayState  state;
    int          occupant;   /* flight id or -1 */
} Runway;

typedef struct {
    GateState    state;
    int          occupant;   /* flight id or -1 */
} Gate;

/* Aggregate counters surfaced on the dashboard. */
typedef struct {
    int    flights_spawned;
    int    flights_landed;
    int    flights_departed;
    int    flights_lost;
    int    flights_diverted;     /* sent to an alternate by the user   */
    int    emergencies;
    int    go_arounds;           /* missed approaches flown            */
    double cumulative_wait;     /* sum of all queue waits (minutes)   */
    double max_wait;
    int    wait_samples;
    long   events_processed;
    double score;               /* controller performance score       */
} SimStats;

/* ------------------------------------------------------------------ */
/*  rolling event log (surfaced as a live ticker by the render layer)  */
/* ------------------------------------------------------------------ */

#define SIM_LOG_CAP 48

typedef struct {
    double t;            /* sim minute the line was recorded         */
    int    severity;     /* 0 info, 1 caution, 2 alert               */
    char   msg[56];
} SimLogEntry;

typedef struct {
    /* clock */
    double           now;          /* current simulated minute        */
    double           horizon;      /* stop spawning after this        */

    /* engine */
    EventPriorityQueue pq;
    Rng                rng;
    uint64_t           seed;

    /* entities */
    Aircraft        flights[MAX_FLIGHTS];
    int             flight_count;

    /* resources */
    Runway          runways[MAX_RUNWAYS];
    int             num_runways;
    Gate            gates[MAX_GATES];
    int             num_gates;

    /* contention queues (indices into flights[]) */
    int             holding[MAX_HOLDING_SLOTS];   /* circular FIFO     */
    int             holding_head, holding_tail, holding_len;

    int             taxi_wait[MAX_TAXI_SLOTS];    /* waiting for a gate */
    int             taxi_head, taxi_tail, taxi_len;

    double          mean_interarrival;            /* minutes           */

    /* environment */
    Weather         weather;

    /* rolling event log (circular) */
    SimLogEntry     log[SIM_LOG_CAP];
    int             log_head, log_len;

    SimStats        stats;
    bool            spawning_done;
} Simulation;

/* lifecycle */
void sim_init(Simulation *s, uint64_t seed, int num_runways,
              int num_gates, double mean_interarrival, double horizon);
void sim_free(Simulation *s);

/* advance the clock to and process exactly one event.
 * returns false when the queue is exhausted.                          */
bool sim_step(Simulation *s);

/* run headless to completion (used by tests / CLI batch mode).        */
void sim_run_to_end(Simulation *s);

/* ------------------------------------------------------------------ */
/*  controller decisions (issued interactively from the render layer)  */
/* ------------------------------------------------------------------ */

/* Expedite: give this flight priority for the next free runway, and
 * attempt an immediate landing assignment if one is available.        */
void sim_cmd_prioritize(Simulation *s, int flight_id);

/* Hold: send an inbound flight into the holding pattern and drop any
 * expedite priority (defer it behind other traffic).                  */
void sim_cmd_hold(Simulation *s, int flight_id);

/* Divert: send an airborne, pre-touchdown flight to an alternate
 * field. It leaves the sector cleanly and is counted as diverted.     */
void sim_cmd_divert(Simulation *s, int flight_id);

/* Go around: wave off a landing aircraft; it re-enters the pattern.   */
void sim_cmd_go_around(Simulation *s, int flight_id);

/* true if the command is currently valid for the given flight.        */
bool sim_can_prioritize(const Simulation *s, int flight_id);
bool sim_can_hold(const Simulation *s, int flight_id);
bool sim_can_divert(const Simulation *s, int flight_id);
bool sim_can_go_around(const Simulation *s, int flight_id);

/* helpers for the render layer */
double sim_runway_utilisation(const Simulation *s);
double sim_gate_utilisation(const Simulation *s);
double sim_mean_wait(const Simulation *s);
double sim_score(const Simulation *s);
const char *event_name(EventType t);
const char *state_name(AircraftState st);
const char *weather_name(Weather w);

/* event-log access: index 0 is the most recent line. returns false
 * when i is past the end of the buffer.                               */
int  sim_log_count(const Simulation *s);
bool sim_log_at(const Simulation *s, int i, SimLogEntry *out);

#endif /* ATC_SIM_H */
