#ifndef ATC_PQUEUE_H
#define ATC_PQUEUE_H

#include "types.h"

/*
 * Binary min-heap priority queue for discrete events.
 *
 * Ordering is total and deterministic:
 *   1. earliest execution_time wins
 *   2. on equal time, lower priority_weight wins
 *   3. on a full tie, lower insertion sequence wins (FIFO)
 *
 * push / pop are O(log N). peek is O(1).
 */

typedef struct {
    AtcEvent     *nodes;
    int           capacity;
    int           size;
    unsigned long seq_counter;
} EventPriorityQueue;

void      pq_init(EventPriorityQueue *pq, int initial_capacity);
void      pq_free(EventPriorityQueue *pq);
void      pq_push(EventPriorityQueue *pq, AtcEvent ev);
bool      pq_pop(EventPriorityQueue *pq, AtcEvent *out);
bool      pq_peek(const EventPriorityQueue *pq, AtcEvent *out);
bool      pq_empty(const EventPriorityQueue *pq);
int       pq_count(const EventPriorityQueue *pq);

/* Assign the global tie-break sequence; called internally by push    */
/* but exposed for testing determinism.                               */

#endif /* ATC_PQUEUE_H */
