/*
 * Yield the current task, and make sure it was added to rq
 * and that its vruntime was recorded correctly
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_yield ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (2000000);
  driver_yield ();
  check_current (t1);
  advancetime (1000000);
  driver_yield ();
  check_current (t1);
  pass ();
  tearDown ();

}