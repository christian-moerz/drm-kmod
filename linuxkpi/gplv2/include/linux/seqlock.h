#ifndef _SEQLOCK_H_
#define _SEQLOCK_H_

#include <linux/types.h>
#include <linux/rwlock.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include_next <linux/seqlock.h>

static inline unsigned __seqprop_sequence(const seqcount_t *s)
{
	return READ_ONCE(s->seqc);
}

#define seqcount_ww_mutex_t seqcount_mutex_t

#endif