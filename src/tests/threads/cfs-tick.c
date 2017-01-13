/*
 * Tests that:
 * 1) If current task hasn't run for ideal_runtime, no rescheduling occurs
 * 2) If current task has been run for ideal_runtime, then rescheduling occurs
 * Requires yield_fair working
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_tick ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (2000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (2000000);
  driver_interrupt_tick ();
  check_current (t1);
  pass ();
  tearDown ();

}