#ifndef DEVICES_SERIAL_H
#define DEVICES_SERIAL_H

#include <stdint.h>
#include <stdbool.h>

void serial_init_queue (void);
void serial_txq_acquire(void);
void serial_txq_release(void);
void serial_putc (uint8_t);
void serial_flush (void);
void serial_notify (void);
void serial_set_poll_mode (bool);

#endif /* devices/serial.h */
