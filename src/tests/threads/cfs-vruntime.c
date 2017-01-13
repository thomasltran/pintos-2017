/*
 * More fine grained test on how vruntime affects scheduling decisions.
 * Unlike other tests which mostly preempts at gran intervals (causing a
 * guaranteed context switch) this test will make interrupts arrive at more non-uniform
 * intervals
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_vruntime ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *t1 = driver_create ("t1", 0);
  check_current (initial);
  advancetime (1000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (1000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (1000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (1000000);
  driver_interrupt_tick ();
  check_current (t1);
  advancetime (10000000);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (1000000);
  struct thread *t2 = driver_create ("t2", 0);
  check_current (initial);
  advancetime (3000000);
  driver_interrupt_tick ();
  check_current (t2);
  advancetime (6000000);
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