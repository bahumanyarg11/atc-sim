#ifndef ATC_TYPES_H
#define ATC_TYPES_H

#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Simulation-wide tunable constants                                  */
/* ------------------------------------------------------------------ */

#define MAX_FLIGHTS        4096
#define MAX_GATES          12
#define MAX_RUNWAYS        4
#define MAX_HOLDING_SLOTS  24
#define MAX_TAXI_SLOTS     24

#define FUEL_INITIAL       100.0
#define FUEL_BURN_RATE     0.85   /* units per simulated minute        */
#define FUEL_EMERGENCY     18.0   /* threshold that forces preemption  */
#define FUEL_CRITICAL      4.0    /* below this an aircraft is lost     */

#define RUNWAY_LANDING_T   2.0    /* minutes a runway is held to land  */
#define RUNWAY_TAKEOFF_T   1.5
#define TAXI_DURATION      3.0
#define GATE_TURNAROUND    8.0    /* deplane + service + board         */
#define TAKEOFF_PREP       2.5

/* ------------------------------------------------------------------ */
/*  Event taxonomy                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    EVENT_AIRCRAFT_ARRIVE,
    EVENT_RUNWAY_REQUEST,
    EVENT_TOUCHDOWN,
    EVENT_TAXI_TO_GATE,
    EVENT_GATE_RELEASE,
    EVENT_TAKEOFF_READY,
    EVENT_TAKEOFF_CLEAR,
    EVENT_EMERGENCY_FUEL,
    EVENT_DEPART_SECTOR,
    EVENT_WEATHER_CHANGE,
    EVENT_TYPE_COUNT
} EventType;

/* Lower numeric weight == higher scheduling priority on ties.        */
typedef enum {
    PRIO_EMERGENCY = 0,
    PRIO_LANDING   = 1,
    PRIO_TAKEOFF   = 2,
    PRIO_ROUTINE   = 3
} PriorityWeight;

typedef struct {
    unsigned int   flight_id;
    EventType      type;
    double         execution_time;   /* absolute sim minute            */
    int            priority_weight;
    unsigned long  seq;              /* insertion order, tie-breaker   */
} AtcEvent;

/* ------------------------------------------------------------------ */
/*  Aircraft lifecycle                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    AC_INBOUND,        /* in sector, not yet cleared to land           */
    AC_HOLDING,        /* in a holding pattern, runway contended       */
    AC_LANDING,        /* on final / occupying runway                  */
    AC_TAXI_IN,        /* runway -> gate                               */
    AC_AT_GATE,        /* turnaround                                   */
    AC_TAXI_OUT,       /* gate -> runway                               */
    AC_DEPARTING,      /* occupying runway for takeoff                 */
    AC_AIRBORNE_OUT,   /* climbing out, leaving sector                 */
    AC_DONE,           /* left the simulation cleanly                  */
    AC_LOST,           /* ran out of fuel                              */
    AC_DIVERTED        /* sent to an alternate by controller decision  */
} AircraftState;

typedef struct {
    unsigned int   id;
    char           callsign[8];

    /* real-world flight identity (see dataset.c)                      */
    char           airline[24];      /* "British Airways"              */
    char           ac_type[8];       /* ICAO type, e.g. "A320"         */
    char           origin[5];        /* origin airport IATA, "LHR"     */
    char           origin_city[16];  /* "London"                       */
    char           wake;             /* wake category L/M/H/J          */

    AircraftState  state;
    double         fuel;
    double         spawn_time;
    double         last_update;      /* last sim time fuel was decayed */
    bool           emergency;
    bool           user_priority;    /* controller expedited this flight */
    bool           force_go_around;  /* controller waved off this landing  */
    int            go_arounds;       /* missed approaches flown            */

    /* radar geometry (display only, advanced by the render layer)    */
    double         bearing_deg;      /* position around the tower       */
    double         range_nm;         /* distance from tower             */
    double         heading_deg;

    /* smooth interpolation anchors                                    */
    double         x, y;             /* current screen-space px         */
    double         tx, ty;           /* target screen-space px          */
    double         move_start;       /* sim time the current leg began  */
    double         move_dur;         /* leg duration in sim minutes     */

    int            assigned_gate;    /* -1 if none                      */
    int            assigned_runway;  /* -1 if none                      */
    double         queue_enter_time; /* for wait-time accounting        */
    double         total_wait;
} Aircraft;

#endif /* ATC_TYPES_H */
