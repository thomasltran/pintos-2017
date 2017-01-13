/*
 * Test that:
 * 1) Priority boost does not let short sleepers gain time
 * 2) Sleepers vruntime is updated prior to sleeping
 * A "short sleep" is defined as a sleep for less than sched_latency (20ms by default)
 * In this test, even though initial thread went blocked at t=20ms, 
 * it only blocked for 10ms, which is not long enough to receive a bonus.
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_short_sleeper ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (20000000);
  driver_block ();
  check_current (t1);
  advancetime (10000000);
  driver_unblock (initial);
  check_current (t1);
  advancetime (20000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (20000000);
  driver_interrupt_tick ();
  check_current (t1);
  pass ();
  tearDown ();

}