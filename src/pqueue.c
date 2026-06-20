#include "pqueue.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Returns true if event a should be served before event b. */
static inline bool ev_less(const AtcEvent *a, const AtcEvent *b)
{
    if (a->execution_time != b->execution_time)
        return a->execution_time < b->execution_time;
    if (a->priority_weight != b->priority_weight)
        return a->priority_weight < b->priority_weight;
    return a->seq < b->seq;
}

void pq_init(EventPriorityQueue *pq, int initial_capacity)
{
    if (initial_capacity < 16) initial_capacity = 16;
    pq->nodes       = malloc(sizeof(AtcEvent) * (size_t)initial_capacity);
    pq->capacity    = initial_capacity;
    pq->size        = 0;
    pq->seq_counter = 0;
    assert(pq->nodes != NULL);
}

void pq_free(EventPriorityQueue *pq)
{
    free(pq->nodes);
    pq->nodes    = NULL;
    pq->capacity = 0;
    pq->size     = 0;
}

static void pq_grow(EventPriorityQueue *pq)
{
    int new_cap = pq->capacity * 2;
    AtcEvent *grown = realloc(pq->nodes, sizeof(AtcEvent) * (size_t)new_cap);
    assert(grown != NULL);
    pq->nodes    = grown;
    pq->capacity = new_cap;
}

static void sift_up(EventPriorityQueue *pq, int i)
{
    AtcEvent tmp = pq->nodes[i];
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!ev_less(&tmp, &pq->nodes[parent])) break;
        pq->nodes[i] = pq->nodes[parent];
        i = parent;
    }
    pq->nodes[i] = tmp;
}

static void sift_down(EventPriorityQueue *pq, int i)
{
    AtcEvent tmp = pq->nodes[i];
    int n = pq->size;
    for (;;) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int best  = i;
        const AtcEvent *best_ev = &tmp;   /* compare against the held value */

        if (left  < n && ev_less(&pq->nodes[left],  best_ev)) {
            best = left;  best_ev = &pq->nodes[left];
        }
        if (right < n && ev_less(&pq->nodes[right], best_ev)) {
            best = right; best_ev = &pq->nodes[right];
        }
        if (best == i) break;

        pq->nodes[i] = pq->nodes[best];
        i = best;
    }
    pq->nodes[i] = tmp;
}

void pq_push(EventPriorityQueue *pq, AtcEvent ev)
{
    if (pq->size == pq->capacity) pq_grow(pq);
    ev.seq = pq->seq_counter++;
    pq->nodes[pq->size] = ev;
    sift_up(pq, pq->size);
    pq->size++;
}

bool pq_pop(EventPriorityQueue *pq, AtcEvent *out)
{
    if (pq->size == 0) return false;
    *out = pq->nodes[0];
    pq->size--;
    if (pq->size > 0) {
        pq->nodes[0] = pq->nodes[pq->size];
        sift_down(pq, 0);
    }
    return true;
}

bool pq_peek(const EventPriorityQueue *pq, AtcEvent *out)
{
    if (pq->size == 0) return false;
    *out = pq->nodes[0];
    return true;
}

bool pq_empty(const EventPriorityQueue *pq) { return pq->size == 0; }
int  pq_count(const EventPriorityQueue *pq) { return pq->size; }
