#include "devices/input.h"
#include <debug.h>
#include "devices/intq.h"
#include "devices/serial.h"
#include "threads/synch.h"

/* Stores keys from the keyboard and serial port.
 * buffer has a spinlock to protect buffer
 * Writers: Callers to input_putc(), such as keyboard interrupt handler
 * and serial interrupt handler
 * Readers: Callers to input_getc(), who must call input_lock() prior to
 * any input_getc()
 */
static struct intq buffer;

/* Initializes the input buffer. */
void
input_init (void) 
{
  intq_init (&buffer);
}

/* Adds a key to the input buffer.
   Interrupts must be off and the buffer must not be full. */
void
input_putc (uint8_t key) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (!intq_full (&buffer));
  intq_putc (&buffer, key);
  serial_notify ();
}

/* Retrieves a key from the input buffer.
   If the buffer is empty, waits for a key to be pressed. */
uint8_t
input_getc (void) 
{
  uint8_t key;
  key = intq_getc (&buffer);
  serial_notify ();
  return key;
}

/* Returns true if the input buffer is full,
   false otherwise.
   Interrupts must be off. */
bool
input_full (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
  return intq_full (&buffer);
}

void
input_acquire (void)
{
  intq_acquire(&buffer);
}

void
input_release (void)
{
  intq_release(&buffer);
}
