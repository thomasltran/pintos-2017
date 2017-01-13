#ifndef DEVICES_SERIAL_H
#define DEVICES_SERIAL_H

#include <stdint.h>
#include <stdbool.h>

/* Defined in devices/serial.c
   This boolean toggles whether or not
   a call to printf can put a thread to 
   sleep. 
   False is recommended, especially in
   Project 1 where print statements
   will often cause infinite recursion.
   Also allows you to print in interrupt
   handlers. 
   When false, putc will poll the serial
   port rather than queueing bytes and
   going to sleep until the arrival of 
   the serial interrupt. The difference
   in performance is not noticeable */
extern bool serial_putc_sleep;

void serial_init_queue (void);
void serial_txq_acquire(void);
void serial_txq_release(void);
void serial_putc (uint8_t);
void serial_flush (void);
void serial_notify (void);

#endif /* devices/serial.h */
