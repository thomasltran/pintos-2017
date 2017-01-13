#ifndef __LIB_ATOMIC_OPS_H
#define __LIB_ATOMIC_OPS_H

#include <stdbool.h>
#include <stdint.h>

inline int atomic_xchg (int *, int);
inline int atomic_inci (int *);
inline int atomic_deci (int *);

inline int atomic_addi (int *, int);
inline bool atomic_cmpxchg (int *, int *, int *);
inline int atomic_load (int *);
inline void atomic_store (int *, int);

#endif /* lib/atomic-ops.h */
