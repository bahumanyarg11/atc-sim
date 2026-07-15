#include "pqueue.h"
#include "rng.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else         { printf("  ok:   %s\n", msg); } \
} while (0)

static void test_pqueue_order(void)
{
    printf("[pqueue ordering]\n");
    EventPriorityQueue pq;
    pq_init(&pq, 4);

    double times[] = {5.0, 1.0, 3.0, 2.0, 4.0, 1.0};
    for (int i = 0; i < 6; i++) {
        AtcEvent e = {0};
        e.flight_id = (unsigned)i;
        e.execution_time = times[i];
        e.priority_weight = PRIO_ROUTINE;
        pq_push(&pq, e);
    }
    CHECK(pq_count(&pq) == 6, "count after 6 pushes");

    double last = -1.0;
    int popped = 0;
    AtcEvent out;
    while (pq_pop(&pq, &out)) {
        CHECK(out.execution_time >= last - 1e-9, "monotonic non-decreasing pop");
        last = out.execution_time;
        popped++;
    }
    CHECK(popped == 6, "popped all events");

    /* the two events at t=1.0 must come out FIFO (flight 1 before 5) */
    pq_free(&pq);
}

static void test_pqueue_tiebreak(void)
{
    printf("[pqueue tie-break: time, then priority, then FIFO]\n");
    EventPriorityQueue pq;
    pq_init(&pq, 8);

    AtcEvent a = {0}; a.flight_id = 10; a.execution_time = 2.0; a.priority_weight = PRIO_ROUTINE;
    AtcEvent b = {0}; b.flight_id = 11; b.execution_time = 2.0; b.priority_weight = PRIO_EMERGENCY;
    AtcEvent c = {0}; c.flight_id = 12; c.execution_time = 2.0; c.priority_weight = PRIO_EMERGENCY;
    pq_push(&pq, a);
    pq_push(&pq, b);
    pq_push(&pq, c);

    AtcEvent out;
    pq_pop(&pq, &out); CHECK(out.flight_id == 11, "emergency b before routine a");
    pq_pop(&pq, &out); CHECK(out.flight_id == 12, "emergency c after b (FIFO on equal prio)");
    pq_pop(&pq, &out); CHECK(out.flight_id == 10, "routine a last");
    pq_free(&pq);
}

static void test_pqueue_stress(void)
{
    printf("[pqueue stress 100k]\n");
    EventPriorityQueue pq;
    pq_init(&pq, 16);
    Rng r; rng_seed(&r, 12345);

    for (int i = 0; i < 100000; i++) {
        AtcEvent e = {0};
        /* integer-valued keys: exact comparison, no float epsilon games */
        e.execution_time = (double)(rng_next(&r) % 1000000);
        e.priority_weight = PRIO_ROUTINE;
        pq_push(&pq, e);
    }
    double last = -1.0;
    AtcEvent out;
    int ok = 1, n = 0;
    while (pq_pop(&pq, &out)) {
        if (out.execution_time < last) ok = 0;
        last = out.execution_time;
        n++;
    }
    CHECK(ok && n == 100000, "100k events popped in sorted order");
    pq_free(&pq);
}

static void test_rng_determinism(void)
{
    printf("[rng determinism]\n");
    Rng a, b;
    rng_seed(&a, 42);
    rng_seed(&b, 42);
    int same = 1;
    for (int i = 0; i < 1000; i++)
        if (rng_next(&a) != rng_next(&b)) same = 0;
    CHECK(same, "same seed -> identical stream");

    Rng c; rng_seed(&c, 43);
    rng_seed(&a, 42);
    CHECK(rng_next(&a) != rng_next(&c), "different seed -> different stream");
}

static void test_sim_determinism(void)
{
    printf("[sim determinism]\n");
    /* Simulation embeds a MAX_FLIGHTS-sized array; heap-allocate to avoid
     * blowing the default thread stack when two are live at once. */
    Simulation *s1 = malloc(sizeof(Simulation));
    Simulation *s2 = malloc(sizeof(Simulation));
    sim_init(s1, 777, 2, 8, 1.4, 600.0);
    sim_init(s2, 777, 2, 8, 1.4, 600.0);
    sim_run_to_end(s1);
    sim_run_to_end(s2);

    CHECK(s1->stats.flights_spawned  == s2->stats.flights_spawned,  "spawned equal");
    CHECK(s1->stats.flights_departed == s2->stats.flights_departed, "departed equal");
    CHECK(s1->stats.events_processed == s2->stats.events_processed, "event count equal");
    CHECK(fabs(s1->now - s2->now) < 1e-9, "final clock equal");
    sim_free(s1);
    sim_free(s2);
    free(s1);
    free(s2);
}

static void test_sim_conservation(void)
{
    printf("[sim flow conservation]\n");
    Simulation *s = malloc(sizeof(Simulation));
    sim_init(s, 2024, 2, 8, 1.5, 800.0);
    sim_run_to_end(s);

    /* every spawned aircraft must end DONE or LOST                    */
    int accounted = 0;
    for (int i = 0; i < s->flight_count; i++) {
        AircraftState st = s->flights[i].state;
        if (st == AC_DONE || st == AC_LOST) accounted++;
    }
    CHECK(accounted == s->flight_count, "all aircraft terminate cleanly");
    CHECK(s->stats.flights_departed + s->stats.flights_lost == s->stats.flights_spawned,
          "departed + lost == spawned");
    printf("    spawned=%d departed=%d lost=%d emerg=%d events=%ld\n",
           s->stats.flights_spawned, s->stats.flights_departed,
           s->stats.flights_lost, s->stats.emergencies, s->stats.events_processed);
    sim_free(s);
    free(s);
}

static void test_sim_saturation(void)
{
    printf("[sim saturation: emergencies + losses + conservation]\n");
    Simulation *s = malloc(sizeof(Simulation));
    sim_init(s, 3, 1, 4, 0.3, 600.0);   /* deliberately overwhelmed   */
    sim_run_to_end(s);

    int accounted = 0;
    for (int i = 0; i < s->flight_count; i++) {
        AircraftState st = s->flights[i].state;
        if (st == AC_DONE || st == AC_LOST) accounted++;
    }
    CHECK(accounted == s->flight_count, "all aircraft terminate under saturation");
    CHECK(s->stats.flights_departed + s->stats.flights_lost == s->stats.flights_spawned,
          "departed + lost == spawned under saturation");
    CHECK(s->stats.emergencies > 0, "emergencies fire under saturation");
    CHECK(s->stats.flights_lost > 0, "fuel-starved aircraft are lost under saturation");
    printf("    spawned=%d departed=%d lost=%d emerg=%d\n",
           s->stats.flights_spawned, s->stats.flights_departed,
           s->stats.flights_lost, s->stats.emergencies);
    sim_free(s);
    free(s);
}

static void test_sim_commands(void)
{
    printf("[sim controller commands + diversion conservation]\n");
    Simulation *s = malloc(sizeof(Simulation));
    sim_init(s, 123, 1, 2, 0.5, 400.0);   /* single runway -> holdings */

    /* advance until some traffic is airborne and divertable */
    int target = -1;
    for (int i = 0; i < 400 && sim_step(s); i++) {
        for (int j = 0; j < s->flight_count; j++)
            if (sim_can_divert(s, j)) { target = j; break; }
        if (target >= 0) break;
    }
    CHECK(target >= 0, "found a divertable flight in flight");

    if (target >= 0) {
        int before = s->stats.flights_diverted;
        sim_cmd_divert(s, target);
        CHECK(s->flights[target].state == AC_DIVERTED, "divert sets DIVERTED state");
        CHECK(s->stats.flights_diverted == before + 1, "divert is tallied");
        /* a terminal flight ignores further commands */
        sim_cmd_prioritize(s, target);
        CHECK(s->flights[target].state == AC_DIVERTED, "terminal flight ignores commands");
    }

    sim_run_to_end(s);

    int accounted = 0;
    for (int i = 0; i < s->flight_count; i++) {
        AircraftState st = s->flights[i].state;
        if (st == AC_DONE || st == AC_LOST || st == AC_DIVERTED) accounted++;
    }
    CHECK(accounted == s->flight_count, "all aircraft terminate (incl. diverted)");
    CHECK(s->stats.flights_departed + s->stats.flights_lost +
          s->stats.flights_diverted == s->stats.flights_spawned,
          "departed + lost + diverted == spawned");
    printf("    spawned=%d departed=%d lost=%d diverted=%d go_arounds=%d score=%.0f\n",
           s->stats.flights_spawned, s->stats.flights_departed, s->stats.flights_lost,
           s->stats.flights_diverted, s->stats.go_arounds, sim_score(s));
    sim_free(s);
    free(s);
}

int main(void)
{
    printf("==== ATC simulator core tests ====\n");
    test_pqueue_order();
    test_pqueue_tiebreak();
    test_pqueue_stress();
    test_rng_determinism();
    test_sim_determinism();
    test_sim_conservation();
    test_sim_saturation();
    test_sim_commands();
    printf("==================================\n");
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
