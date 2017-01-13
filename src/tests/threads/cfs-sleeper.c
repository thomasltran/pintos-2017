/*
 * Test that sleepers get their due priority boost. They should be the first ones to be scheduled after waking up
 * block() should cause a vruntime update
 * unblock() should then cause another vruntime update (setting to min_vruntime) minus sleeper bonus
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_sleeper ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (0);
  struct thread *t2 = driver_create ("t2", 0);
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (2000000);
  driver_block ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (0);
  driver_unblock (initial);
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (24000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (4000000);
  driver_interrupt_tick ();
  check_current (t2);
  pass ();
  tearDown ();

}