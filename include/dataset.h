#ifndef ATC_DATASET_H
#define ATC_DATASET_H

#include "types.h"
#include "rng.h"

/*
 * Real-world flight dataset.
 *
 * A curated table of actual airlines (IATA prefixes), in-service aircraft
 * types with their ICAO wake-turbulence categories, and real origin
 * airports. dataset_assign() draws a plausible, self-consistent flight
 * identity for a freshly spawned aircraft using the seeded PRNG, so runs
 * stay fully reproducible.
 */

/* Fill callsign, airline, ac_type, origin (and wake category) on a
 * spawning aircraft. Consumes a fixed number of PRNG draws.            */
void dataset_assign(Aircraft *a, Rng *rng);

/* Count of each table, exposed for tests / introspection.             */
int  dataset_airline_count(void);
int  dataset_aircraft_count(void);
int  dataset_airport_count(void);

#endif /* ATC_DATASET_H */
