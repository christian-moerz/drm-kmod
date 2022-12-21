
#ifndef _LINUX_SEQLOCK_H_
#define _LINUX_SEQLOCK_H_

#include_next <linux/seqlock.h>

typedef struct {
	seqcount_t seqc;
	struct rwlock lock;
} seqcount_mutex_t;

#define seqcount_mutex_init(s, l)	seqcount_init(&(s)->seqc)

#endif
