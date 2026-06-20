#include "dataset.h"
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/*  Real-world reference data                                          */
/* ------------------------------------------------------------------ */
/*  Sources are public, well-known identifiers: airline IATA codes,    */
/*  ICAO aircraft type designators with their wake categories, and     */
/*  major international airport IATA codes.                             */
/* ================================================================== */

typedef struct {
    const char *iata;     /* 2-char flight-number prefix      */
    const char *name;     /* marketing name                   */
    int         lo, hi;   /* typical flight-number band       */
} Airline;

static const Airline AIRLINES[] = {
    { "UA", "United",             1,  2400 },
    { "DL", "Delta",              1,  2900 },
    { "AA", "American",           1,  2700 },
    { "WN", "Southwest",          1,  4000 },
    { "BA", "British Airways",    1,   900 },
    { "LH", "Lufthansa",          1,  1500 },
    { "AF", "Air France",         1,  1700 },
    { "KL", "KLM",                1,  1900 },
    { "EK", "Emirates",           1,   900 },
    { "QR", "Qatar Airways",      1,  1300 },
    { "SQ", "Singapore Airlines", 1,   900 },
    { "QF", "Qantas",             1,   700 },
    { "CX", "Cathay Pacific",     1,   900 },
    { "JL", "Japan Airlines",     1,  1100 },
    { "NH", "ANA",                1,  1200 },
    { "TK", "Turkish Airlines",   1,  1900 },
    { "AC", "Air Canada",         1,  1800 },
    { "6E", "IndiGo",           100,  2300 },
    { "AI", "Air India",          1,   900 },
    { "EY", "Etihad",             1,   500 },
};

typedef struct {
    const char *code;     /* ICAO type designator             */
    char        wake;     /* L / M / H / J (super)            */
} AircraftType;

static const AircraftType TYPES[] = {
    { "A319", 'M' }, { "A320", 'M' }, { "A321", 'M' },
    { "A332", 'H' }, { "A333", 'H' }, { "A359", 'H' }, { "A388", 'J' },
    { "B737", 'M' }, { "B738", 'M' }, { "B739", 'M' }, { "B752", 'M' },
    { "B763", 'H' }, { "B772", 'H' }, { "B77W", 'H' },
    { "B788", 'H' }, { "B789", 'H' },
    { "E190", 'M' }, { "CRJ9", 'M' },
};

typedef struct {
    const char *iata;     /* 3-char airport code              */
    const char *city;     /* human-readable origin            */
} Airport;

static const Airport AIRPORTS[] = {
    { "JFK", "New York" },   { "LAX", "Los Angeles" }, { "ORD", "Chicago" },
    { "ATL", "Atlanta" },    { "SFO", "San Francisco"},{ "LHR", "London" },
    { "CDG", "Paris" },      { "FRA", "Frankfurt" },   { "AMS", "Amsterdam" },
    { "MAD", "Madrid" },     { "DXB", "Dubai" },       { "DOH", "Doha" },
    { "SIN", "Singapore" },  { "HKG", "Hong Kong" },   { "HND", "Tokyo" },
    { "ICN", "Seoul" },      { "SYD", "Sydney" },      { "DEL", "Delhi" },
    { "BOM", "Mumbai" },     { "IST", "Istanbul" },    { "YYZ", "Toronto" },
    { "GRU", "Sao Paulo" },  { "JNB", "Johannesburg"},
};

#define NELEMS(x) ((int)(sizeof(x) / sizeof((x)[0])))

int dataset_airline_count(void)  { return NELEMS(AIRLINES); }
int dataset_aircraft_count(void) { return NELEMS(TYPES);    }
int dataset_airport_count(void)  { return NELEMS(AIRPORTS); }

void dataset_assign(Aircraft *a, Rng *rng)
{
    const Airline      *al = &AIRLINES[rng_range(rng, 0, NELEMS(AIRLINES) - 1)];
    const AircraftType *ty = &TYPES[rng_range(rng, 0, NELEMS(TYPES) - 1)];
    const Airport      *ap = &AIRPORTS[rng_range(rng, 0, NELEMS(AIRPORTS) - 1)];

    int fnum = rng_range(rng, al->lo, al->hi);
    snprintf(a->callsign, sizeof(a->callsign), "%s%d", al->iata, fnum);

    snprintf(a->airline, sizeof(a->airline), "%s", al->name);
    snprintf(a->ac_type, sizeof(a->ac_type), "%s", ty->code);
    snprintf(a->origin,  sizeof(a->origin),  "%s", ap->iata);
    snprintf(a->origin_city, sizeof(a->origin_city), "%s", ap->city);
    a->wake = ty->wake;
}
