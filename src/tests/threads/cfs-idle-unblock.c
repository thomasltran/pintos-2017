/*
 * Check the case where an idle thread unblocks a thread.
 * May happen from device drivers interrupting the idle thread and
 * unblocking threads
 * Tests that:
 * 1) Idle threads don't yield (they call thread_block in a tight loop anyway)
 * 2) Idle weights don't contribute to the scheduling period calculation
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_idle_unblock ()
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