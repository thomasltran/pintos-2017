/*
 * Tests that:
 * 1) Tasks has their time slices adjusted based on nice
 * 2) Tasks have their vruntimes adjusted based on nice
 * 3) Both positive and negative nice values are respected
 */

#include "threads/thread.h"
#include "tests/threads/schedtest.h"
#include "tests/threads/simulator.h"
#include "tests/threads/tests.h"

void
test_nice ()
{
  setUp ();
  struct thread *initial = driver_current ();
  advancetime (0);
  struct thread *n20 = driver_create ("n20", -20);
  check_current (initial);
  advancetime (0);
  struct thread *p19 = driver_create ("p19", 19);
  check_current (initial);
  advancetime (136900);
  driver_interrupt_tick ();
  check_current (n20);
  advancetime (5930600);
  driver_interrupt_tick ();
  check_current (n20);
  advancetime (5930600);
  driver_interrupt_tick ();
  check_current (p19);
  advancetime (2100);
  driver_interrupt_tick ();
  check_current (n20);
  advancetime (11861200);
  driver_interrupt_tick ();
  check_current (initial);
  advancetime (136900);
  driver_interrupt_tick ();
  check_current (p19);
  pass ();
  tearDown ();

}