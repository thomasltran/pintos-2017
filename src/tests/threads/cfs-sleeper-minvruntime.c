/*
 * Tests that if a new thread is created right after a thread is unblocked, then that
 * new thread doesn't get the sleeper bonus (cfs_rq->min_vruntime should never go backwards)
 * Also makes sure that current thread's vruntime is updated prior to setting min_vruntime
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_sleeper_minvruntime ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (0);
  driver_block ();
  check_current (initial);
  advancetime (20000000);
  driver_unblock (t1);
  check_current (initial);
  advancetime (0);
  struct thread *t2 = driver_create ("t2", 0);
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  pass ();
  tearDown ();

}