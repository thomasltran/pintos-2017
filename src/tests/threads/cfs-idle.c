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
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (0);
  driver_block ();
  check_current (t1);
  advancetime (0);
  driver_block ();
  check_current (idle);
  advancetime (4000000);
  driver_unblock (t1);
  check_current (idle);
  advancetime (0);
  driver_block ();
  check_current (t1);
  advancetime (0);
  struct thread *t2 = driver_create ("t2", 0);
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  pass ();
  tearDown ();

}