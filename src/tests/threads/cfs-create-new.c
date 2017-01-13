/*
 * Create a new task and check that it is added to the queue
 * Requires pick_next_task
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_create_new ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (0);
  driver_exit ();
  check_current (t1);
  pass ();
  tearDown ();
}