/*
 * Test that
 * 1) CPU not yielding due to empty runqueue does not mean thread
 * is not penalized for its time spent running
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_tick2 ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (2000000);
  driver_interrupt_tick ();
  check_current (t1);
  pass ();
  tearDown ();

}