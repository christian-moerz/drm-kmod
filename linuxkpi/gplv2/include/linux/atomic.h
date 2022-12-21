
#ifndef _LINUX_ATOMIC_H_
#define _LINUX_ATOMIC_H_

#include_next <linux/atomic.h>

#define cmpxchg64(p, o, n)	__sync_val_compare_and_swap(p, o, n)

#endif
