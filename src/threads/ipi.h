#ifndef THREADS_IPI_H_
#define THREADS_IPI_H_

#include "threads/interrupt.h"

void ipi_init (void);
void ipi_shutdown (struct intr_frame *);
void ipi_debug (struct intr_frame *);
void ipi_schedule (struct intr_frame *);

#endif /* THREADS_IPI_H_ */
