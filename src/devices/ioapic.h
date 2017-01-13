#ifndef DEVICES_IOAPIC_H_
#define DEVICES_IOAPIC_H_

#include <stdint.h>

uint8_t ioapic_id;

void ioapic_init (void);
void ioapicenable (int irq, int cpu);


#endif /* DEVICES_IOAPIC_H_ */
