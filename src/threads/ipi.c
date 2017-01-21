#include "threads/ipi.h"
#include "devices/lapic.h"
#include <stdbool.h>
#include "threads/interrupt.h"
#include <stdio.h>
#include "threads/cpu.h"
#include <debug.h>
#include "lib/kernel/x86.h"
#include "threads/synch.h"
#include "lib/atomic-ops.h"
#include "threads/mp.h"

/* Register interrupt handlers for the inter-processor
   interrupts that we support */
void
ipi_init ()
{
  intr_register_ipi (T_IPI + IPI_SHUTDOWN, ipi_shutdown,
                     "#IPI SHUTDOWN");
  intr_register_ipi (T_IPI + IPI_DEBUG, ipi_debug,
                     "#IPI DEBUG");
  intr_register_ipi (T_IPI + IPI_SCHEDULE, ipi_schedule,
                     "#IPI SCHEDULE");
}

/* Received a shutdown signal from another CPU
   For now, it's just going to disable interrupts and spin so that
   the CPU stats remain consistent for the CPU that called shutdown() */
void
ipi_shutdown (struct intr_frame *f UNUSED)
{
  ASSERT(cpu_started_others);
  /* CPU0 needs to stay on in order to handle interrupts. Otherwise
     if an AP calls shutdown, it may get stuck trying to print to console */
  if (get_cpu ()->id == 0)
    return;
  intr_disable ();
  while (1)
    ;
}

/* Preempt the currently running thread */
void
ipi_schedule (struct intr_frame *f UNUSED)
{
  ASSERT (cpu_started_others);
  intr_yield_on_return ();
}

/* For debugging. Prints the backtrace of the thread running on the current CPU  */
void
ipi_debug (struct intr_frame *f UNUSED)
{
  ASSERT (cpu_started_others);
  debug_backtrace_with_lock ();
}
