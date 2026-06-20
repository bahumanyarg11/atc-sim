#include "sim.h"
#include "dataset.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* ================================================================== */
/*  small lookup tables                                                */
/* ================================================================== */

static const char *EVENT_NAMES[EVENT_TYPE_COUNT] = {
    "ARRIVE", "RUNWAY_REQ", "TOUCHDOWN", "TAXI_TO_GATE",
    "GATE_RELEASE", "TAKEOFF_READY", "TAKEOFF_CLEAR",
    "EMERGENCY_FUEL", "DEPART_SECTOR", "WEATHER_CHANGE"
};

const char *event_name(EventType t)
{
    if (t < 0 || t >= EVENT_TYPE_COUNT) return "?";
    return EVENT_NAMES[t];
}

const char *weather_name(Weather w)
{
    switch (w) {
        case WX_CLEAR: return "CLEAR";
        case WX_WINDY: return "WINDY";
        case WX_STORM: return "STORM";
    }
    return "?";
}

/* ================================================================== */
/*  rolling event log                                                  */
/* ================================================================== */

static void sim_log(Simulation *s, int severity, const char *fmt, ...)
{
    int slot = (s->log_head + s->log_len) % SIM_LOG_CAP;
    if (s->log_len < SIM_LOG_CAP) {
        s->log_len++;
    } else {
        s->log_head = (s->log_head + 1) % SIM_LOG_CAP;  /* overwrite oldest */
    }
    SimLogEntry *e = &s->log[slot];
    e->t = s->now;
    e->severity = severity;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->msg, sizeof(e->msg), fmt, ap);
    va_end(ap);
}

int sim_log_count(const Simulation *s)
{
    return s->log_len;
}

bool sim_log_at(const Simulation *s, int i, SimLogEntry *out)
{
    if (i < 0 || i >= s->log_len) return false;
    /* index 0 = newest */
    int slot = (s->log_head + s->log_len - 1 - i) % SIM_LOG_CAP;
    *out = s->log[slot];
    return true;
}

/* ================================================================== */
/*  environment + separation models                                    */
/* ================================================================== */

/* wake-turbulence separation: heavier aircraft hold a runway longer.  */
static double wake_factor(char wake)
{
    switch (wake) {
        case 'L': return 0.85;
        case 'H': return 1.25;
        case 'J': return 1.55;   /* super (A380) */
        default:  return 1.00;   /* medium       */
    }
}

/* weather lengthens runway occupancy and raises the go-around rate.   */
static double weather_runway_factor(Weather w)
{
    switch (w) {
        case WX_WINDY: return 1.15;
        case WX_STORM: return 1.40;
        default:       return 1.00;
    }
}

static double weather_go_around_prob(Weather w)
{
    switch (w) {
        case WX_WINDY: return 0.06;
        case WX_STORM: return 0.18;
        default:       return 0.02;
    }
}

const char *state_name(AircraftState st)
{
    switch (st) {
        case AC_INBOUND:      return "INBOUND";
        case AC_HOLDING:      return "HOLDING";
        case AC_LANDING:      return "LANDING";
        case AC_TAXI_IN:      return "TAXI-IN";
        case AC_AT_GATE:      return "AT GATE";
        case AC_TAXI_OUT:     return "TAXI-OUT";
        case AC_DEPARTING:    return "DEPARTING";
        case AC_AIRBORNE_OUT: return "OUTBOUND";
        case AC_DONE:         return "DONE";
        case AC_LOST:         return "LOST";
        case AC_DIVERTED:     return "DIVERTED";
    }
    return "?";
}

/* ================================================================== */
/*  event scheduling helpers                                           */
/* ================================================================== */

static void schedule(Simulation *s, unsigned int fid, EventType type,
                     double delay, int weight)
{
    AtcEvent ev;
    ev.flight_id       = fid;
    ev.type            = type;
    ev.execution_time  = s->now + delay;
    ev.priority_weight = weight;
    ev.seq             = 0;          /* set inside pq_push             */
    pq_push(&s->pq, ev);
}

/* ================================================================== */
/*  fuel model: F(t) = F0 - alpha * dt                                 */
/* ================================================================== */

static void decay_fuel(Simulation *s, Aircraft *a)
{
    double dt = s->now - a->last_update;
    if (dt > 0) {
        a->fuel -= FUEL_BURN_RATE * dt;
        if (a->fuel < 0) a->fuel = 0;
        a->last_update = s->now;
    }
}

/* ================================================================== */
/*  FIFO ring buffers for the two contention queues                    */
/* ================================================================== */

static void holding_push(Simulation *s, int fid)
{
    if (s->holding_len >= MAX_HOLDING_SLOTS) return;
    s->holding[s->holding_tail] = fid;
    s->holding_tail = (s->holding_tail + 1) % MAX_HOLDING_SLOTS;
    s->holding_len++;
}

static int holding_pop(Simulation *s)
{
    if (s->holding_len == 0) return -1;
    int fid = s->holding[s->holding_head];
    s->holding_head = (s->holding_head + 1) % MAX_HOLDING_SLOTS;
    s->holding_len--;
    return fid;
}

/* pull the most fuel-critical aircraft out of holding rather than
 * strict FIFO when an emergency is queued. linear scan over a tiny
 * ring is cheap and keeps emergencies from starving.                  */
static int holding_pop_priority(Simulation *s)
{
    if (s->holding_len == 0) return -1;

    int best_slot = -1;
    double worst_fuel = 1e30;
    bool any_emerg = false;
    int  expedite_slot = -1;

    for (int k = 0; k < s->holding_len; k++) {
        int idx = (s->holding_head + k) % MAX_HOLDING_SLOTS;
        int fid = s->holding[idx];
        Aircraft *a = &s->flights[fid];
        if (a->emergency) any_emerg = true;
        if (a->user_priority && expedite_slot < 0) expedite_slot = idx;
        if (a->fuel < worst_fuel) { worst_fuel = a->fuel; best_slot = idx; }
    }

    /* emergencies trump everything; otherwise a controller-expedited
     * flight jumps the queue; otherwise plain FIFO.                    */
    if (!any_emerg) {
        if (expedite_slot < 0) return holding_pop(s);
        best_slot = expedite_slot;
    }

    /* compact the ring around the removed slot                        */
    int removed = s->holding[best_slot];
    int count = s->holding_len;
    int tmp[MAX_HOLDING_SLOTS];
    int n = 0;
    for (int k = 0; k < count; k++) {
        int idx = (s->holding_head + k) % MAX_HOLDING_SLOTS;
        if (idx == best_slot) continue;
        tmp[n++] = s->holding[idx];
    }
    s->holding_head = 0;
    s->holding_tail = n % MAX_HOLDING_SLOTS;
    s->holding_len  = n;
    for (int k = 0; k < n; k++) s->holding[k] = tmp[k];
    return removed;
}

/* returns false if the taxiway hold buffer is full */
static bool taxi_push(Simulation *s, int fid)
{
    if (s->taxi_len >= MAX_TAXI_SLOTS) return false;
    s->taxi_wait[s->taxi_tail] = fid;
    s->taxi_tail = (s->taxi_tail + 1) % MAX_TAXI_SLOTS;
    s->taxi_len++;
    return true;
}

static int taxi_pop(Simulation *s)
{
    if (s->taxi_len == 0) return -1;
    int fid = s->taxi_wait[s->taxi_head];
    s->taxi_head = (s->taxi_head + 1) % MAX_TAXI_SLOTS;
    s->taxi_len--;
    return fid;
}

/* ================================================================== */
/*  resource lookups                                                   */
/* ================================================================== */

static int find_idle_runway(Simulation *s)
{
    for (int i = 0; i < s->num_runways; i++)
        if (s->runways[i].state == RW_IDLE) return i;
    return -1;
}

static int find_vacant_gate(Simulation *s)
{
    for (int i = 0; i < s->num_gates; i++)
        if (s->gates[i].state == GATE_VACANT) return i;
    return -1;
}

/* ================================================================== */
/*  wait-time accounting                                               */
/* ================================================================== */

static void record_wait(Simulation *s, Aircraft *a)
{
    double w = s->now - a->queue_enter_time;
    if (w < 0) w = 0;
    a->total_wait += w;
    s->stats.cumulative_wait += w;
    if (w > s->stats.max_wait) s->stats.max_wait = w;
    s->stats.wait_samples++;
}

/* ================================================================== */
/*  spawning                                                           */
/* ================================================================== */

static void spawn_aircraft(Simulation *s)
{
    if (s->flight_count >= MAX_FLIGHTS) return;

    Aircraft *a = &s->flights[s->flight_count];
    memset(a, 0, sizeof(*a));
    a->id    = (unsigned int)s->flight_count;
    a->state = AC_INBOUND;
    a->fuel  = FUEL_INITIAL - rng_double(&s->rng) * 25.0; /* varied     */
    a->spawn_time   = s->now;
    a->last_update  = s->now;
    a->assigned_gate   = -1;
    a->assigned_runway = -1;
    a->bearing_deg = rng_double(&s->rng) * 360.0;
    a->range_nm    = 9.0 + rng_double(&s->rng) * 2.0;
    a->heading_deg = a->bearing_deg + 180.0;

    /* draw a real-world flight identity (airline, type, origin)       */
    dataset_assign(a, &s->rng);

    s->flight_count++;
    s->stats.flights_spawned++;
    sim_log(s, 0, "%s (%s) inbound from %s", a->callsign, a->ac_type,
            a->origin);

    schedule(s, a->id, EVENT_RUNWAY_REQUEST, 0.0, PRIO_LANDING);

    /* schedule the next arrival unless we are past the horizon        */
    if (!s->spawning_done) {
        double gap = rng_exp(&s->rng, s->mean_interarrival);
        if (s->now + gap <= s->horizon) {
            schedule(s, 0, EVENT_AIRCRAFT_ARRIVE, gap, PRIO_ROUTINE);
        } else {
            s->spawning_done = true;
        }
    }
}

/* ================================================================== */
/*  event handlers                                                     */
/* ================================================================== */

static void try_assign_landing(Simulation *s, Aircraft *a);

static void on_runway_request(Simulation *s, Aircraft *a)
{
    decay_fuel(s, a);
    a->queue_enter_time = s->now;

    if (a->fuel <= FUEL_EMERGENCY && !a->emergency) {
        a->emergency = true;
        s->stats.emergencies++;
        s->stats.score -= 6.0;
        sim_log(s, 2, "%s declared FUEL EMERGENCY", a->callsign);
        schedule(s, a->id, EVENT_EMERGENCY_FUEL, 0.0, PRIO_EMERGENCY);
        return;
    }
    try_assign_landing(s, a);
}

static void try_assign_landing(Simulation *s, Aircraft *a)
{
    int rw = find_idle_runway(s);
    if (rw < 0) {
        /* no runway: enter holding pattern (FIFO ring). schedule a
         * periodic fuel re-check so a stuck aircraft can escalate to
         * an emergency rather than silently burning down.            */
        a->state = AC_HOLDING;
        holding_push(s, (int)a->id);
        schedule(s, a->id, EVENT_EMERGENCY_FUEL, 1.0,
                 a->emergency ? PRIO_EMERGENCY : PRIO_LANDING);
        return;
    }
    record_wait(s, a);
    s->runways[rw].state    = RW_LANDING;
    s->runways[rw].occupant = (int)a->id;
    a->assigned_runway      = rw;
    a->state                = AC_LANDING;
    double land_t = RUNWAY_LANDING_T * wake_factor(a->wake)
                                     * weather_runway_factor(s->weather);
    schedule(s, a->id, EVENT_TOUCHDOWN, land_t,
             a->emergency ? PRIO_EMERGENCY : PRIO_LANDING);
}

static void holding_remove(Simulation *s, int fid)
{
    int tmp[MAX_HOLDING_SLOTS], n = 0;
    for (int j = 0; j < s->holding_len; j++) {
        int jdx = (s->holding_head + j) % MAX_HOLDING_SLOTS;
        if (s->holding[jdx] == fid) continue;
        tmp[n++] = s->holding[jdx];
    }
    s->holding_head = 0;
    s->holding_tail = n % MAX_HOLDING_SLOTS;
    s->holding_len  = n;
    for (int j = 0; j < n; j++) s->holding[j] = tmp[j];
}

static void on_emergency(Simulation *s, Aircraft *a)
{
    decay_fuel(s, a);

    /* ignore stale fuel checks for aircraft that already moved on     */
    if (a->state != AC_HOLDING && a->state != AC_INBOUND) return;

    if (a->fuel <= FUEL_CRITICAL) {
        if (a->state == AC_HOLDING) holding_remove(s, (int)a->id);
        a->state = AC_LOST;
        s->stats.flights_lost++;
        s->stats.score -= 50.0;
        sim_log(s, 2, "%s LOST — fuel exhausted", a->callsign);
        return;
    }

    if (a->fuel <= FUEL_EMERGENCY && !a->emergency) {
        a->emergency = true;
        s->stats.emergencies++;
        s->stats.score -= 6.0;
        sim_log(s, 2, "%s declared FUEL EMERGENCY", a->callsign);
    }

    int rw = find_idle_runway(s);
    if (rw >= 0) {
        if (a->state == AC_HOLDING) holding_remove(s, (int)a->id);
        record_wait(s, a);
        s->runways[rw].state    = RW_LANDING;
        s->runways[rw].occupant = (int)a->id;
        a->assigned_runway      = rw;
        a->state                = AC_LANDING;
        double land_t = RUNWAY_LANDING_T * wake_factor(a->wake)
                                         * weather_runway_factor(s->weather);
        schedule(s, a->id, EVENT_TOUCHDOWN, land_t, PRIO_EMERGENCY);
    } else {
        /* no runway yet: ensure we are in holding and keep polling.    */
        if (a->state == AC_INBOUND) {
            a->state = AC_HOLDING;
            holding_push(s, (int)a->id);
        }
        schedule(s, a->id, EVENT_EMERGENCY_FUEL, 0.5,
                 a->emergency ? PRIO_EMERGENCY : PRIO_LANDING);
    }
}

static void free_runway(Simulation *s, int rw)
{
    s->runways[rw].state    = RW_IDLE;
    s->runways[rw].occupant = -1;

    /* a freed runway first serves emergencies / holding landings      */
    int next = holding_pop_priority(s);
    if (next >= 0) {
        Aircraft *na = &s->flights[next];
        try_assign_landing(s, na);
    }
}

static void on_touchdown(Simulation *s, Aircraft *a)
{
    int rw = a->assigned_runway;
    decay_fuel(s, a);

    /* missed approach: forced by the controller, or a weather-driven
     * random wave-off. the aircraft re-enters the holding pattern.    */
    bool go = a->force_go_around ||
              (rng_double(&s->rng) < weather_go_around_prob(s->weather));
    if (go && a->fuel > FUEL_CRITICAL) {
        a->force_go_around = false;
        a->assigned_runway = -1;
        a->go_arounds++;
        s->stats.go_arounds++;
        s->stats.score -= 3.0;
        a->state = AC_HOLDING;
        a->queue_enter_time = s->now;
        holding_push(s, (int)a->id);
        sim_log(s, 1, "%s GO-AROUND (%s)", a->callsign,
                weather_name(s->weather));
        schedule(s, a->id, EVENT_EMERGENCY_FUEL, 0.8,
                 a->emergency ? PRIO_EMERGENCY : PRIO_LANDING);
        free_runway(s, rw);   /* may serve the next holding arrival     */
        return;
    }

    a->assigned_runway = -1;
    s->stats.flights_landed++;
    a->state = AC_TAXI_IN;
    a->queue_enter_time = s->now;
    free_runway(s, rw);
    schedule(s, a->id, EVENT_TAXI_TO_GATE, TAXI_DURATION, PRIO_ROUTINE);
}

static void try_assign_gate(Simulation *s, Aircraft *a)
{
    int g = find_vacant_gate(s);
    if (g < 0) {
        /* deadlock-avoidance: hold on the taxiway, do NOT lock a
         * runway. runways stay free for inbound traffic. if the hold
         * buffer is full, keep the aircraft alive by retrying shortly
         * rather than dropping it (which would strand it).            */
        if (!taxi_push(s, (int)a->id))
            schedule(s, a->id, EVENT_TAXI_TO_GATE, 1.0, PRIO_ROUTINE);
        return;
    }
    record_wait(s, a);
    s->gates[g].state    = GATE_OCCUPIED;
    s->gates[g].occupant = (int)a->id;
    a->assigned_gate     = g;
    a->state             = AC_AT_GATE;
    schedule(s, a->id, EVENT_GATE_RELEASE, GATE_TURNAROUND, PRIO_ROUTINE);
}

static void on_taxi_to_gate(Simulation *s, Aircraft *a)
{
    try_assign_gate(s, a);
}

static void on_gate_release(Simulation *s, Aircraft *a)
{
    int g = a->assigned_gate;
    a->assigned_gate = -1;
    s->gates[g].state    = GATE_VACANT;
    s->gates[g].occupant = -1;
    a->state = AC_TAXI_OUT;
    schedule(s, a->id, EVENT_TAKEOFF_READY, TAXI_DURATION, PRIO_TAKEOFF);

    /* a freed gate lets a taxi-waiting arrival in                     */
    int waiting = taxi_pop(s);
    if (waiting >= 0) {
        Aircraft *wa = &s->flights[waiting];
        try_assign_gate(s, wa);
    }
}

static void on_takeoff_ready(Simulation *s, Aircraft *a)
{
    a->queue_enter_time = s->now;
    int rw = find_idle_runway(s);
    if (rw < 0) {
        /* departures yield to landings; re-poll shortly               */
        schedule(s, a->id, EVENT_TAKEOFF_READY, 0.8, PRIO_TAKEOFF);
        return;
    }
    record_wait(s, a);
    s->runways[rw].state    = RW_TAKEOFF;
    s->runways[rw].occupant = (int)a->id;
    a->assigned_runway      = rw;
    a->state                = AC_DEPARTING;
    double prep = TAKEOFF_PREP * wake_factor(a->wake)
                              * weather_runway_factor(s->weather);
    schedule(s, a->id, EVENT_TAKEOFF_CLEAR, prep, PRIO_TAKEOFF);
}

static void on_takeoff_clear(Simulation *s, Aircraft *a)
{
    int rw = a->assigned_runway;
    a->assigned_runway = -1;
    a->state = AC_AIRBORNE_OUT;
    free_runway(s, rw);
    schedule(s, a->id, EVENT_DEPART_SECTOR, RUNWAY_TAKEOFF_T, PRIO_ROUTINE);
}

static void on_depart_sector(Simulation *s, Aircraft *a)
{
    a->state = AC_DONE;
    s->stats.flights_departed++;
    /* reward a clean turnaround, with a bonus for low total delay      */
    s->stats.score += 10.0;
    if (a->total_wait < 5.0)  s->stats.score += 5.0;
    if (a->go_arounds == 0 && !a->emergency) s->stats.score += 2.0;
    sim_log(s, 0, "%s departed for sector exit", a->callsign);
}

/* ================================================================== */
/*  weather: a slow Markov drift between CLEAR / WINDY / STORM          */
/* ================================================================== */

static void schedule_weather(Simulation *s)
{
    /* re-roll every ~90 sim minutes on average. bounded by the spawn
     * horizon so the event queue still drains to empty at end of run. */
    double gap = rng_exp(&s->rng, 90.0);
    if (s->now + gap <= s->horizon)
        schedule(s, 0, EVENT_WEATHER_CHANGE, gap, PRIO_ROUTINE);
}

static void on_weather_change(Simulation *s)
{
    Weather prev = s->weather;
    /* biased toward CLEAR, occasional WINDY, rare STORM               */
    double r = rng_double(&s->rng);
    Weather next = r < 0.62 ? WX_CLEAR : r < 0.88 ? WX_WINDY : WX_STORM;
    s->weather = next;
    if (next != prev)
        sim_log(s, next == WX_STORM ? 1 : 0, "Weather now %s",
                weather_name(next));
    schedule_weather(s);
}

/* ================================================================== */
/*  public API                                                         */
/* ================================================================== */

void sim_init(Simulation *s, uint64_t seed, int num_runways,
              int num_gates, double mean_interarrival, double horizon)
{
    memset(s, 0, sizeof(*s));
    s->seed    = seed;
    s->horizon = horizon;
    s->mean_interarrival = mean_interarrival;

    if (num_runways < 1) num_runways = 1;
    if (num_runways > MAX_RUNWAYS) num_runways = MAX_RUNWAYS;
    if (num_gates   < 1) num_gates = 1;
    if (num_gates   > MAX_GATES)   num_gates = MAX_GATES;

    s->num_runways = num_runways;
    s->num_gates   = num_gates;

    for (int i = 0; i < s->num_runways; i++) {
        s->runways[i].state = RW_IDLE; s->runways[i].occupant = -1;
    }
    for (int i = 0; i < s->num_gates; i++) {
        s->gates[i].state = GATE_VACANT; s->gates[i].occupant = -1;
    }

    s->weather = WX_CLEAR;

    pq_init(&s->pq, 256);
    rng_seed(&s->rng, seed);

    /* prime the pump with the first arrival and the weather clock */
    schedule(s, 0, EVENT_AIRCRAFT_ARRIVE, rng_exp(&s->rng, mean_interarrival),
             PRIO_ROUTINE);
    schedule_weather(s);
}

void sim_free(Simulation *s)
{
    pq_free(&s->pq);
}

bool sim_step(Simulation *s)
{
    AtcEvent ev;
    if (!pq_pop(&s->pq, &ev)) return false;

    s->now = ev.execution_time;        /* non-linear clock advance     */
    s->stats.events_processed++;

    if (ev.type == EVENT_AIRCRAFT_ARRIVE) {
        spawn_aircraft(s);
        return true;
    }
    if (ev.type == EVENT_WEATHER_CHANGE) {
        on_weather_change(s);
        return true;
    }

    Aircraft *a = &s->flights[ev.flight_id];

    /* drop stale events for aircraft that have left, been lost, or
     * been diverted out of the sector by the controller              */
    if (a->state == AC_DONE || a->state == AC_LOST ||
        a->state == AC_DIVERTED) return true;

    switch (ev.type) {
        case EVENT_RUNWAY_REQUEST: on_runway_request(s, a); break;
        case EVENT_EMERGENCY_FUEL: on_emergency(s, a);      break;
        case EVENT_TOUCHDOWN:      on_touchdown(s, a);      break;
        case EVENT_TAXI_TO_GATE:   on_taxi_to_gate(s, a);   break;
        case EVENT_GATE_RELEASE:   on_gate_release(s, a);   break;
        case EVENT_TAKEOFF_READY:  on_takeoff_ready(s, a);  break;
        case EVENT_TAKEOFF_CLEAR:  on_takeoff_clear(s, a);  break;
        case EVENT_DEPART_SECTOR:  on_depart_sector(s, a);  break;
        default: break;
    }
    return true;
}

void sim_run_to_end(Simulation *s)
{
    while (sim_step(s)) { /* spin */ }
}

/* ================================================================== */
/*  controller decisions                                               */
/* ================================================================== */

static Aircraft *cmd_target(Simulation *s, int fid)
{
    if (fid < 0 || fid >= s->flight_count) return NULL;
    Aircraft *a = &s->flights[fid];
    if (a->state == AC_DONE || a->state == AC_LOST ||
        a->state == AC_DIVERTED) return NULL;
    return a;
}

bool sim_can_prioritize(const Simulation *s, int fid)
{
    if (fid < 0 || fid >= s->flight_count) return false;
    const Aircraft *a = &s->flights[fid];
    return a->state == AC_INBOUND || a->state == AC_HOLDING;
}

bool sim_can_hold(const Simulation *s, int fid)
{
    if (fid < 0 || fid >= s->flight_count) return false;
    const Aircraft *a = &s->flights[fid];
    return a->state == AC_INBOUND;
}

bool sim_can_divert(const Simulation *s, int fid)
{
    if (fid < 0 || fid >= s->flight_count) return false;
    const Aircraft *a = &s->flights[fid];
    return a->state == AC_INBOUND || a->state == AC_HOLDING ||
           a->state == AC_LANDING;
}

bool sim_can_go_around(const Simulation *s, int fid)
{
    if (fid < 0 || fid >= s->flight_count) return false;
    return s->flights[fid].state == AC_LANDING;
}

void sim_cmd_prioritize(Simulation *s, int fid)
{
    Aircraft *a = cmd_target(s, fid);
    if (!a) return;
    if (!sim_can_prioritize(s, fid)) return;

    a->user_priority = true;
    sim_log(s, 0, "%s expedited to the front", a->callsign);
    /* if it is orbiting, pull it out and try to put it on a runway now */
    if (a->state == AC_HOLDING) {
        holding_remove(s, fid);
        try_assign_landing(s, a);   /* re-enters holding if none free   */
    }
}

void sim_cmd_hold(Simulation *s, int fid)
{
    Aircraft *a = cmd_target(s, fid);
    if (!a) return;
    if (a->state != AC_INBOUND) return;

    a->user_priority    = false;
    a->state            = AC_HOLDING;
    a->queue_enter_time = s->now;
    holding_push(s, fid);
    sim_log(s, 0, "%s sent to the holding pattern", a->callsign);
    schedule(s, a->id, EVENT_EMERGENCY_FUEL, 1.0,
             a->emergency ? PRIO_EMERGENCY : PRIO_LANDING);
}

void sim_cmd_divert(Simulation *s, int fid)
{
    Aircraft *a = cmd_target(s, fid);
    if (!a) return;
    if (!sim_can_divert(s, fid)) return;

    if (a->state == AC_HOLDING) holding_remove(s, fid);
    if (a->assigned_runway >= 0) {
        free_runway(s, a->assigned_runway);   /* releases + reseats queue */
        a->assigned_runway = -1;
    }
    a->emergency = false;
    a->state     = AC_DIVERTED;
    s->stats.flights_diverted++;
    s->stats.score -= 12.0;
    sim_log(s, 1, "%s diverted to an alternate", a->callsign);
}

void sim_cmd_go_around(Simulation *s, int fid)
{
    Aircraft *a = cmd_target(s, fid);
    if (!a) return;
    if (a->state != AC_LANDING) return;
    /* takes effect at the scheduled touchdown (short final wave-off)   */
    a->force_go_around = true;
    sim_log(s, 1, "%s instructed to go around", a->callsign);
}

/* ================================================================== */
/*  metrics                                                            */
/* ================================================================== */

double sim_runway_utilisation(const Simulation *s)
{
    int busy = 0;
    for (int i = 0; i < s->num_runways; i++)
        if (s->runways[i].state != RW_IDLE) busy++;
    return s->num_runways ? (double)busy / s->num_runways : 0.0;
}

double sim_gate_utilisation(const Simulation *s)
{
    int busy = 0;
    for (int i = 0; i < s->num_gates; i++)
        if (s->gates[i].state != GATE_VACANT) busy++;
    return s->num_gates ? (double)busy / s->num_gates : 0.0;
}

double sim_mean_wait(const Simulation *s)
{
    if (s->stats.wait_samples == 0) return 0.0;
    return s->stats.cumulative_wait / s->stats.wait_samples;
}

double sim_score(const Simulation *s)
{
    return s->stats.score;
}
