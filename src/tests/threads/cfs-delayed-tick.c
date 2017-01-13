/* Test that:
 * 1) Thread that got extra runtime due to delay in timer interrupt gets its vruntime penalized accordingly
 * 2) If thread reaches ideal_runtime, rescheduling occurs even if its vruntime is smaller than the vruntime of
 * leftmost thread in rq, which would cause it to be picked again immediately. The reason for this is that the
 * responsibility of checking vruntime and deciding which task to run should be the job of pick_next_task, not
 * task_tick
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_delayed_tick ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (6000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  pass ();
  tearDown ();
}