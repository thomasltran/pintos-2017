/*
 * Test that:
 * 1) When there is only one task running, no rescheduling ever occurs
 * 2) If there are no tasks, then idle thread is scheduled
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_idle ()
{
  setUp ();
  struct thread *initial = driver_current ();
  struct thread *idle = driver_idle ();
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_exit ();
  check_current (idle);
  pass ();
  tearDown ();
}
