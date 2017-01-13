#ifndef TESTS_THREADS_SCHEDTEST_H_
#define TESTS_THREADS_SCHEDTEST_H_

#include <stdint.h>
#include "threads/thread.h"

static const unsigned int gran = 4000000ULL; /* 4ms */
static const unsigned int latency_gran_ratio = 5;
/* Keep it at gran * latency_gran_ratio, but C only lets you initialize constants */
static const unsigned int latency = 20000000ULL; /* 20 ms */

void check_vruntime (struct thread *t, uint64_t expect);
void check_minvruntime (uint64_t expect);
void check_current (struct thread *expect);
void driver_interrupt_tick (void);
void advancetime (int64_t advance);
void setUp (void);
void tearDown (void);

#endif /* TESTS_THREADS_SCHEDTEST_H_ */
