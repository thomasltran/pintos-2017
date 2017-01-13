#ifndef DEVICES_LAPIC_H_
#define DEVICES_LAPIC_H_

#include "lib/kernel/bitmap.h"

/* The lapic is implemented as a memory mapped I/O device.
   Each CPU accesses its own lapic through these memory addresses.
   Each lapic is mapped to the same addresses, which are not shared.
   A CPU cannot access another CPU's lapic. 
   lapic_base_addr stores the base address for the 
   local APIC. 
   This is initialized in mp.c, by parsing the MP Configuration Table */
volatile uint32_t *lapic_base_addr;

#define T_IPI 0xFB
#define NUM_IPI 5
#define IPI_SHUTDOWN 0
#define IPI_TLB 1
#define IPI_DEBUG 2
#define IPI_SCHEDULE 3

int lapic_get_cpuid (void);
void lapic_ack (void);
void lapic_init (void);
void lapicstartap (uint8_t, uint32_t);
void lapicsendcpu (int, uint8_t);
void lapicsendallbutself (int);
void lapicsendmask (int, struct bitmap *);
void lapicsendall (int);
void lapic_set_next_event (uint32_t);

#endif /* DEVICES_LAPIC_H_ */
