/*
 * Copyright (C) 2012-2014 Canonical Ltd (Maarten Lankhorst)
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#include <linux/dma-resv.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>
#ifdef BSDTNG
#include <linux/seq_file.h>
#endif

/**
 * DOC: Reservation Object Overview
 *
 * The reservation object provides a mechanism to manage shared and
 * exclusive fences associated with a buffer.  A reservation object
 * can have attached one exclusive fence (normally associated with
 * write operations) or N shared fences (read operations).  The RCU
 * mechanism is used to protect read access to fences from locked
 * write-side updates.
 */

DEFINE_WW_CLASS(reservation_ww_class);
EXPORT_SYMBOL(reservation_ww_class);


#ifdef BSDTNG
/* Mask for the lower fence pointer bits */
#define DMA_RESV_LIST_MASK	0x3

/* Extract the fence and usage flags from an RCU protected entry in the list. */
static void dma_resv_list_entry(struct dma_resv_list *list, unsigned int index,
				struct dma_resv *resv, struct dma_fence **fence,
				enum dma_resv_usage *usage)
{
	long tmp;

	tmp = (long)rcu_dereference_check(list->table[index],
					  resv ? dma_resv_held(resv) : true);
	*fence = (struct dma_fence *)(tmp & ~DMA_RESV_LIST_MASK);
	if (usage)
		*usage = tmp & DMA_RESV_LIST_MASK;
}

/* Set the fence and usage flags at the specific index in the list. */
static void dma_resv_list_set(struct dma_resv_list *list,
			      unsigned int index,
			      struct dma_fence *fence,
			      enum dma_resv_usage usage)
{
	long tmp = ((long)fence) | usage;

	RCU_INIT_POINTER(list->table[index], (struct dma_fence *)tmp);
}
#endif

/**
 * dma_resv_list_alloc - allocate fence list
 * @shared_max: number of fences we need space for
 *
 * Allocate a new dma_resv_list and make sure to correctly initialize
 * shared_max.
 */
static struct dma_resv_list *dma_resv_list_alloc(unsigned int shared_max)
{
	struct dma_resv_list *list;

	list = kmalloc(struct_size(list, 
#ifdef BSDTNG
		table, shared_max
#else
		shared, shared_max
#endif
	), GFP_KERNEL);
	if (!list)
		return NULL;

	list->shared_max = (ksize(list) - offsetof(typeof(*list), 
#ifndef BSDTNG
		shared)) / sizeof(*list->shared);
#else
		table)) / sizeof(*list->table);
#endif

	return list;
}

/**
 * dma_resv_list_free - free fence list
 * @list: list to free
 *
 * Free a dma_resv_list and make sure to drop all references.
 */
static void dma_resv_list_free(struct dma_resv_list *list)
{
	unsigned int i;

	if (!list)
		return;

#ifndef BSDTNG
	for (i = 0; i < list->shared_count; ++i)
		dma_fence_put(rcu_dereference_protected(list->shared[i], true));
#else
	for (i = 0; i < list->num_fences; ++i) {
		struct dma_fence *fence;

		dma_resv_list_entry(list, i, NULL, &fence, NULL);
		dma_fence_put(fence);
	}
#endif

	kfree_rcu(list, rcu);
}

#if IS_ENABLED(CONFIG_LOCKDEP)
static int __init dma_resv_lockdep(void)
{
	struct mm_struct *mm = mm_alloc();
	struct ww_acquire_ctx ctx;
	struct dma_resv obj;
	struct address_space mapping;
	int ret;

	if (!mm)
		return -ENOMEM;

	dma_resv_init(&obj);
	address_space_init_once(&mapping);

	mmap_read_lock(mm);
	ww_acquire_init(&ctx, &reservation_ww_class);
	ret = dma_resv_lock(&obj, &ctx);
	if (ret == -EDEADLK)
		dma_resv_lock_slow(&obj, &ctx);
	fs_reclaim_acquire(GFP_KERNEL);
	/* for unmap_mapping_range on trylocked buffer objects in shrinkers */
	i_mmap_lock_write(&mapping);
	i_mmap_unlock_write(&mapping);
#ifdef CONFIG_MMU_NOTIFIER
	lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
	__dma_fence_might_wait();
	lock_map_release(&__mmu_notifier_invalidate_range_start_map);
#else
	__dma_fence_might_wait();
#endif
	fs_reclaim_release(GFP_KERNEL);
	ww_mutex_unlock(&obj.lock);
	ww_acquire_fini(&ctx);
	mmap_read_unlock(mm);
	
	mmput(mm);

	return 0;
}
subsys_initcall(dma_resv_lockdep);
#endif

/**
 * dma_resv_init - initialize a reservation object
 * @obj: the reservation object
 */
void dma_resv_init(struct dma_resv *obj)
{
	ww_mutex_init(&obj->lock, &reservation_ww_class);
#ifdef __FreeBSD__
	rw_init_flags(&obj->rw, "dma_resv_rw", RW_NEW);
#endif
	seqcount_init(&obj->seq);

	RCU_INIT_POINTER(obj->fences, NULL);
#ifndef BSDTNG
	RCU_INIT_POINTER(obj->fence_excl, NULL);
#endif
}
EXPORT_SYMBOL(dma_resv_init);

/**
 * dma_resv_fini - destroys a reservation object
 * @obj: the reservation object
 */
void dma_resv_fini(struct dma_resv *obj)
{
	struct dma_resv_list *fobj;
	struct dma_fence *excl;

	/*
	 * This object should be dead and all references must have
	 * been released to it, so no need to be protected with rcu.
	 */
#ifndef BSDTNG
	excl = rcu_dereference_protected(obj->fence_excl, 1);
	if (excl)
		dma_fence_put(excl);
#endif

	fobj = rcu_dereference_protected(obj->fence, 1);
	dma_resv_list_free(fobj);
#ifdef __FreeBSD__
	rw_destroy(&obj->rw);
#endif
	ww_mutex_destroy(&obj->lock);
}
EXPORT_SYMBOL(dma_resv_fini);

#ifdef BSDTNG
/* Dereference the fences while ensuring RCU rules */
static inline struct dma_resv_list *dma_resv_fences_list(struct dma_resv *obj)
{
	return rcu_dereference_check(obj->fences, dma_resv_held(obj));
}

/**
 * dma_resv_reserve_fences - Reserve space to add fences to a dma_resv object.
 * @obj: reservation object
 * @num_fences: number of fences we want to add
 *
 * Should be called before dma_resv_add_fence().  Must be called with @obj
 * locked through dma_resv_lock().
 *
 * Note that the preallocated slots need to be re-reserved if @obj is unlocked
 * at any time before calling dma_resv_add_fence(). This is validated when
 * CONFIG_DEBUG_MUTEXES is enabled.
 *
 * RETURNS
 * Zero for success, or -errno
 */
int dma_resv_reserve_fences(struct dma_resv *obj, unsigned int num_fences)
{
	struct dma_resv_list *old, *new;
	unsigned int i, j, k, max;

	dma_resv_assert_held(obj);

	old = dma_resv_fences_list(obj);
	if (old && old->max_fences) {
		if ((old->num_fences + num_fences) <= old->max_fences)
			return 0;
		max = max(old->num_fences + num_fences, old->max_fences * 2);
	} else {
		max = max(4ul, roundup_pow_of_two(num_fences));
	}

	new = dma_resv_list_alloc(max);
	if (!new)
		return -ENOMEM;

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	for (i = 0, j = 0, k = max; i < (old ? old->num_fences : 0); ++i) {
		enum dma_resv_usage usage;
		struct dma_fence *fence;

		dma_resv_list_entry(old, i, obj, &fence, &usage);
		if (dma_fence_is_signaled(fence))
			RCU_INIT_POINTER(new->table[--k], fence);
		else
			dma_resv_list_set(new, j++, fence, usage);
	}
	new->num_fences = j;

	/*
	 * We are not changing the effective set of fences here so can
	 * merely update the pointer to the new array; both existing
	 * readers and new readers will see exactly the same set of
	 * active (unsignaled) fences. Individual fences and the
	 * old array are protected by RCU and so will not vanish under
	 * the gaze of the rcu_read_lock() readers.
	 */
	rcu_assign_pointer(obj->fences, new);

	if (!old)
		return 0;

	/* Drop the references to the signaled fences */
	for (i = k; i < max; ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(new->table[i],
						  dma_resv_held(obj));
		dma_fence_put(fence);
	}
	kfree_rcu(old, rcu);

	return 0;
}
EXPORT_SYMBOL(dma_resv_reserve_fences);

#ifdef CONFIG_DEBUG_MUTEXES
/**
 * dma_resv_reset_max_fences - reset fences for debugging
 * @obj: the dma_resv object to reset
 *
 * Reset the number of pre-reserved fence slots to test that drivers do
 * correct slot allocation using dma_resv_reserve_fences(). See also
 * &dma_resv_list.max_fences.
 */
void dma_resv_reset_max_fences(struct dma_resv *obj)
{
	struct dma_resv_list *fences = dma_resv_fences_list(obj);

	dma_resv_assert_held(obj);

	/* Test fence slot reservation */
	if (fences)
		fences->max_fences = fences->num_fences;
}
EXPORT_SYMBOL(dma_resv_reset_max_fences);
#endif /* CONFIG_DEBUG_MUTEXES */

/**
 * dma_resv_add_fence - Add a fence to the dma_resv obj
 * @obj: the reservation object
 * @fence: the fence to add
 * @usage: how the fence is used, see enum dma_resv_usage
 *
 * Add a fence to a slot, @obj must be locked with dma_resv_lock(), and
 * dma_resv_reserve_fences() has been called.
 *
 * See also &dma_resv.fence for a discussion of the semantics.
 */
void dma_resv_add_fence(struct dma_resv *obj, struct dma_fence *fence,
			enum dma_resv_usage usage)
{
	struct dma_resv_list *fobj;
	struct dma_fence *old;
	unsigned int i, count;

	dma_fence_get(fence);

	dma_resv_assert_held(obj);

	/* Drivers should not add containers here, instead add each fence
	 * individually.
	 */
	WARN_ON(dma_fence_is_container(fence));

	fobj = dma_resv_fences_list(obj);
	count = fobj->num_fences;

	for (i = 0; i < count; ++i) {
		enum dma_resv_usage old_usage;

		dma_resv_list_entry(fobj, i, obj, &old, &old_usage);
		if ((old->context == fence->context && old_usage >= usage &&
		     dma_fence_is_later(fence, old)) ||
		    dma_fence_is_signaled(old)) {
			dma_resv_list_set(fobj, i, fence, usage);
			dma_fence_put(old);
			return;
		}
	}

	BUG_ON(fobj->num_fences >= fobj->max_fences);
	count++;

	dma_resv_list_set(fobj, i, fence, usage);
	/* pointer update must be visible before we extend the num_fences */
	smp_store_mb(fobj->num_fences, count);
}
EXPORT_SYMBOL(dma_resv_add_fence);

/**
 * dma_resv_replace_fences - replace fences in the dma_resv obj
 * @obj: the reservation object
 * @context: the context of the fences to replace
 * @replacement: the new fence to use instead
 * @usage: how the new fence is used, see enum dma_resv_usage
 *
 * Replace fences with a specified context with a new fence. Only valid if the
 * operation represented by the original fence has no longer access to the
 * resources represented by the dma_resv object when the new fence completes.
 *
 * And example for using this is replacing a preemption fence with a page table
 * update fence which makes the resource inaccessible.
 */
void dma_resv_replace_fences(struct dma_resv *obj, uint64_t context,
			     struct dma_fence *replacement,
			     enum dma_resv_usage usage)
{
	struct dma_resv_list *list;
	unsigned int i;

	dma_resv_assert_held(obj);

	list = dma_resv_fences_list(obj);
	for (i = 0; list && i < list->num_fences; ++i) {
		struct dma_fence *old;

		dma_resv_list_entry(list, i, obj, &old, NULL);
		if (old->context != context)
			continue;

		dma_resv_list_set(list, i, dma_fence_get(replacement), usage);
		dma_fence_put(old);
	}
}
EXPORT_SYMBOL(dma_resv_replace_fences);

/* Restart the unlocked iteration by initializing the cursor object. */
static void dma_resv_iter_restart_unlocked(struct dma_resv_iter *cursor)
{
	cursor->index = 0;
	cursor->num_fences = 0;
	cursor->fences = dma_resv_fences_list(cursor->obj);
	if (cursor->fences)
		cursor->num_fences = cursor->fences->num_fences;
	cursor->is_restarted = true;
}

/* Walk to the next not signaled fence and grab a reference to it */
static void dma_resv_iter_walk_unlocked(struct dma_resv_iter *cursor)
{
	if (!cursor->fences)
		return;

	do {
		/* Drop the reference from the previous round */
		dma_fence_put(cursor->fence);

		if (cursor->index >= cursor->num_fences) {
			cursor->fence = NULL;
			break;

		}

		dma_resv_list_entry(cursor->fences, cursor->index++,
				    cursor->obj, &cursor->fence,
				    &cursor->fence_usage);
		cursor->fence = dma_fence_get_rcu(cursor->fence);
		if (!cursor->fence) {
			dma_resv_iter_restart_unlocked(cursor);
			continue;
		}

		if (!dma_fence_is_signaled(cursor->fence) &&
		    cursor->usage >= cursor->fence_usage)
			break;
	} while (true);
}

/**
 * dma_resv_iter_first_unlocked - first fence in an unlocked dma_resv obj.
 * @cursor: the cursor with the current position
 *
 * Subsequent fences are iterated with dma_resv_iter_next_unlocked().
 *
 * Beware that the iterator can be restarted.  Code which accumulates statistics
 * or similar needs to check for this with dma_resv_iter_is_restarted(). For
 * this reason prefer the locked dma_resv_iter_first() whenver possible.
 *
 * Returns the first fence from an unlocked dma_resv obj.
 */
struct dma_fence *dma_resv_iter_first_unlocked(struct dma_resv_iter *cursor)
{
	rcu_read_lock();
	do {
		dma_resv_iter_restart_unlocked(cursor);
		dma_resv_iter_walk_unlocked(cursor);
	} while (dma_resv_fences_list(cursor->obj) != cursor->fences);
	rcu_read_unlock();

	return cursor->fence;
}
EXPORT_SYMBOL(dma_resv_iter_first_unlocked);

/**
 * dma_resv_iter_next_unlocked - next fence in an unlocked dma_resv obj.
 * @cursor: the cursor with the current position
 *
 * Beware that the iterator can be restarted.  Code which accumulates statistics
 * or similar needs to check for this with dma_resv_iter_is_restarted(). For
 * this reason prefer the locked dma_resv_iter_next() whenver possible.
 *
 * Returns the next fence from an unlocked dma_resv obj.
 */
struct dma_fence *dma_resv_iter_next_unlocked(struct dma_resv_iter *cursor)
{
	bool restart;

	rcu_read_lock();
	cursor->is_restarted = false;
	restart = dma_resv_fences_list(cursor->obj) != cursor->fences;
	do {
		if (restart)
			dma_resv_iter_restart_unlocked(cursor);
		dma_resv_iter_walk_unlocked(cursor);
		restart = true;
	} while (dma_resv_fences_list(cursor->obj) != cursor->fences);
	rcu_read_unlock();

	return cursor->fence;
}
EXPORT_SYMBOL(dma_resv_iter_next_unlocked);

/**
 * dma_resv_iter_first - first fence from a locked dma_resv object
 * @cursor: cursor to record the current position
 *
 * Subsequent fences are iterated with dma_resv_iter_next_unlocked().
 *
 * Return the first fence in the dma_resv object while holding the
 * &dma_resv.lock.
 */
struct dma_fence *dma_resv_iter_first(struct dma_resv_iter *cursor)
{
	struct dma_fence *fence;

	dma_resv_assert_held(cursor->obj);

	cursor->index = 0;
	cursor->fences = dma_resv_fences_list(cursor->obj);

	fence = dma_resv_iter_next(cursor);
	cursor->is_restarted = true;
	return fence;
}
EXPORT_SYMBOL_GPL(dma_resv_iter_first);

/**
 * dma_resv_iter_next - next fence from a locked dma_resv object
 * @cursor: cursor to record the current position
 *
 * Return the next fences from the dma_resv object while holding the
 * &dma_resv.lock.
 */
struct dma_fence *dma_resv_iter_next(struct dma_resv_iter *cursor)
{
	struct dma_fence *fence;

	dma_resv_assert_held(cursor->obj);

	cursor->is_restarted = false;

	do {
		if (!cursor->fences ||
		    cursor->index >= cursor->fences->num_fences)
			return NULL;

		dma_resv_list_entry(cursor->fences, cursor->index++,
				    cursor->obj, &fence, &cursor->fence_usage);
	} while (cursor->fence_usage > cursor->usage);

	return fence;
}
EXPORT_SYMBOL_GPL(dma_resv_iter_next);

/**
 * dma_resv_get_fences - Get an object's fences
 * fences without update side lock held
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @num_fences: the number of fences returned
 * @fences: the array of fence ptrs returned (array is krealloc'd to the
 * required size, and must be freed by caller)
 *
 * Retrieve all fences from the reservation object.
 * Returns either zero or -ENOMEM.
 */
int dma_resv_get_fences(struct dma_resv *obj, enum dma_resv_usage usage,
			unsigned int *num_fences, struct dma_fence ***fences)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	*num_fences = 0;
	*fences = NULL;

	dma_resv_iter_begin(&cursor, obj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {

		if (dma_resv_iter_is_restarted(&cursor)) {
			unsigned int count;

			while (*num_fences)
				dma_fence_put((*fences)[--(*num_fences)]);

			count = cursor.num_fences + 1;

			/* Eventually re-allocate the array */
			*fences = krealloc_array(*fences, count,
						 sizeof(void *),
						 GFP_KERNEL);
			if (count && !*fences) {
				dma_resv_iter_end(&cursor);
				return -ENOMEM;
			}
		}

		(*fences)[(*num_fences)++] = dma_fence_get(fence);
	}
	dma_resv_iter_end(&cursor);

	return 0;
}
EXPORT_SYMBOL_GPL(dma_resv_get_fences);

/**
 * dma_resv_get_singleton - Get a single fence for all the fences
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @fence: the resulting fence
 *
 * Get a single fence representing all the fences inside the resv object.
 * Returns either 0 for success or -ENOMEM.
 *
 * Warning: This can't be used like this when adding the fence back to the resv
 * object since that can lead to stack corruption when finalizing the
 * dma_fence_array.
 *
 * Returns 0 on success and negative error values on failure.
 */
int dma_resv_get_singleton(struct dma_resv *obj, enum dma_resv_usage usage,
			   struct dma_fence **fence)
{
	struct dma_fence_array *array;
	struct dma_fence **fences;
	unsigned count;
	int r;

	r = dma_resv_get_fences(obj, usage, &count, &fences);
        if (r)
		return r;

	if (count == 0) {
		*fence = NULL;
		return 0;
	}

	if (count == 1) {
		*fence = fences[0];
		kfree(fences);
		return 0;
	}

	array = dma_fence_array_create(count, fences,
				       dma_fence_context_alloc(1),
				       1, false);
	if (!array) {
		while (count--)
			dma_fence_put(fences[count]);
		kfree(fences);
		return -ENOMEM;
	}

	*fence = &array->base;
	return 0;
}
EXPORT_SYMBOL_GPL(dma_resv_get_singleton);

/**
 * dma_resv_wait_timeout - Wait on reservation's objects fences
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @intr: if true, do interruptible wait
 * @timeout: timeout value in jiffies or zero to return immediately
 *
 * Callers are not required to hold specific locks, but maybe hold
 * dma_resv_lock() already
 * RETURNS
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or
 * greater than zer on success.
 */
long dma_resv_wait_timeout(struct dma_resv *obj, enum dma_resv_usage usage,
			   bool intr, unsigned long timeout)
{
	long ret = timeout ? timeout : 1;
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, obj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {

		ret = dma_fence_wait_timeout(fence, intr, ret);
		if (ret <= 0) {
			dma_resv_iter_end(&cursor);
			return ret;
		}
	}
	dma_resv_iter_end(&cursor);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_resv_wait_timeout);

/**
 * dma_resv_test_signaled - Test if a reservation object's fences have been
 * signaled.
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 *
 * Callers are not required to hold specific locks, but maybe hold
 * dma_resv_lock() already.
 *
 * RETURNS
 *
 * True if all fences signaled, else false.
 */
bool dma_resv_test_signaled(struct dma_resv *obj, enum dma_resv_usage usage)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, obj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		dma_resv_iter_end(&cursor);
		return false;
	}
	dma_resv_iter_end(&cursor);
	return true;
}
EXPORT_SYMBOL_GPL(dma_resv_test_signaled);

/**
 * dma_resv_describe - Dump description of the resv object into seq_file
 * @obj: the reservation object
 * @seq: the seq_file to dump the description into
 *
 * Dump a textual description of the fences inside an dma_resv object into the
 * seq_file.
 */
void dma_resv_describe(struct dma_resv *obj, struct seq_file *seq)
{
	static const char *usage[] = { "kernel", "write", "read", "bookkeep" };
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_for_each_fence(&cursor, obj, DMA_RESV_USAGE_READ, fence) {
		seq_printf(seq, "\t%s fence:",
			   usage[dma_resv_iter_usage(&cursor)]);
		dma_fence_describe(fence, seq);
	}
}
EXPORT_SYMBOL_GPL(dma_resv_describe);

#endif /* BSDTNG */

/**
 * dma_resv_reserve_shared - Reserve space to add shared fences to
 * a dma_resv.
 * @obj: reservation object
 * @num_fences: number of fences we want to add
 *
 * Should be called before dma_resv_add_shared_fence().  Must
 * be called with obj->lock held.
 *
 * RETURNS
 * Zero for success, or -errno
 */
int dma_resv_reserve_shared(struct dma_resv *obj, unsigned int num_fences)
{
#ifndef BSDTNG
	struct dma_resv_list *old, *new;
	unsigned int i, j, k, max;

	dma_resv_assert_held(obj);

	old = dma_resv_get_list(obj);

	if (old && old->shared_max) {
		if ((old->shared_count + num_fences) <= old->shared_max)
			return 0;
		else
			max = max(old->shared_count + num_fences,
				  old->shared_max * 2);
	} else {
		max = max(4ul, roundup_pow_of_two(num_fences));
	}

	new = dma_resv_list_alloc(max);
	if (!new)
		return -ENOMEM;

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	for (i = 0, j = 0, k = max; i < (old ? old->shared_count : 0); ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(old->shared[i],
						  dma_resv_held(obj));
		if (dma_fence_is_signaled(fence))
			RCU_INIT_POINTER(new->shared[--k], fence);
		else
			RCU_INIT_POINTER(new->shared[j++], fence);
	}
	new->shared_count = j;

	/*
	 * We are not changing the effective set of fences here so can
	 * merely update the pointer to the new array; both existing
	 * readers and new readers will see exactly the same set of
	 * active (unsignaled) shared fences. Individual fences and the
	 * old array are protected by RCU and so will not vanish under
	 * the gaze of the rcu_read_lock() readers.
	 */
	rcu_assign_pointer(obj->fence, new);

	if (!old)
		return 0;

	/* Drop the references to the signaled fences */
	for (i = k; i < max; ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(new->shared[i],
						  dma_resv_held(obj));
		dma_fence_put(fence);
	}
	kfree_rcu(old, rcu);

	return 0;
#else
	return dma_resv_reserve_fences(obj, num_fences);
#endif
}
EXPORT_SYMBOL(dma_resv_reserve_shared);

/**
 * dma_resv_add_shared_fence - Add a fence to a shared slot
 * @obj: the reservation object
 * @fence: the shared fence to add
 *
 * Add a fence to a shared slot, obj->lock must be held, and
 * dma_resv_reserve_shared() has been called.
 */
void dma_resv_add_shared_fence(struct dma_resv *obj, struct dma_fence *fence)
{
#ifndef BSDTNG
	struct dma_resv_list *fobj;
	struct dma_fence *old;
	unsigned int i, count;

	dma_fence_get(fence);

	dma_resv_assert_held(obj);

	fobj = dma_resv_get_list(obj);
	count = fobj->shared_count;

#ifdef __linux__
	preempt_disable();
#elif defined(__FreeBSD__)
	/*
	 * Under FreeBSD dma_fence_is_signaled can be blocked. Prevent readers
	 * from spinning on seqlock in that case with blocking on rwlock.
	 */
	rw_wlock(&obj->rw);
#endif
	write_seqcount_begin(&obj->seq);

	for (i = 0; i < count; ++i) {

		old = rcu_dereference_protected(fobj->shared[i],
						dma_resv_held(obj));
		if (old->context == fence->context ||
		    dma_fence_is_signaled(old))
			goto replace;
	}

	BUG_ON(fobj->shared_count >= fobj->shared_max);
	old = NULL;
	count++;

replace:
	RCU_INIT_POINTER(fobj->shared[i], fence);
	/* pointer update must be visible before we extend the shared_count */
	smp_store_mb(fobj->shared_count, count);

	write_seqcount_end(&obj->seq);
#ifdef __linux__
	preempt_enable();
#elif defined(__FreeBSD__)
	rw_wunlock(&obj->rw);
#endif
	dma_fence_put(old);
#else /* BSDTNG */
	/* FIXME BSD might also require additional write fence? */
	dma_resv_add_fence(obj, fence, DMA_RESV_USAGE_READ);
#endif /* BSDTNG */
}
EXPORT_SYMBOL(dma_resv_add_shared_fence);

/**
 * dma_resv_add_excl_fence - Add an exclusive fence.
 * @obj: the reservation object
 * @fence: the shared fence to add
 *
 * Add a fence to the exclusive slot.  The obj->lock must be held.
 */
void dma_resv_add_excl_fence(struct dma_resv *obj, struct dma_fence *fence)
{
#ifdef BSDTNG
	/* FIXME BSD this might be insufficient? */
	dma_resv_add_fence(obj, fence, DMA_RESV_USAGE_WRITE);
#else
	struct dma_fence *old_fence = dma_resv_get_excl(obj);
	struct dma_resv_list *old;
	u32 i = 0;

	dma_resv_assert_held(obj);

	old = dma_resv_get_list(obj);
	if (old)
		i = old->shared_count;

	if (fence)
		dma_fence_get(fence);

	preempt_disable();
	write_seqcount_begin(&obj->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(obj->fence_excl, fence);
	if (old)
		old->shared_count = 0;
	write_seqcount_end(&obj->seq);
	preempt_enable();

	/* inplace update, no shared fences */
	while (i--)
		dma_fence_put(rcu_dereference_protected(old->shared[i],
						dma_resv_held(obj)));

	dma_fence_put(old_fence);
#endif /* BSDTNG */
}
EXPORT_SYMBOL(dma_resv_add_excl_fence);

/**
* dma_resv_copy_fences - Copy all fences from src to dst.
* @dst: the destination reservation object
* @src: the source reservation object
*
* Copy all fences from src to dst. dst-lock must be held.
*/
int dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src)
{
#ifdef BSDTNG
	struct dma_resv_iter cursor;
	struct dma_resv_list *list;
	struct dma_fence *f;

	dma_resv_assert_held(dst);

	list = NULL;

	dma_resv_iter_begin(&cursor, src, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, f) {

		if (dma_resv_iter_is_restarted(&cursor)) {
			dma_resv_list_free(list);

			list = dma_resv_list_alloc(cursor.num_fences);
			if (!list) {
				dma_resv_iter_end(&cursor);
				return -ENOMEM;
			}
			list->num_fences = 0;
		}

		dma_fence_get(f);
		dma_resv_list_set(list, list->num_fences++, f,
				  dma_resv_iter_usage(&cursor));
	}
	dma_resv_iter_end(&cursor);

	list = rcu_replace_pointer(dst->fences, list, dma_resv_held(dst));
	dma_resv_list_free(list);

#else /* !BSDTNG */
	struct dma_resv_list *src_list, *dst_list;
	struct dma_fence *old, *new;
	unsigned i;

	dma_resv_assert_held(dst);

	rcu_read_lock();
	src_list = rcu_dereference(src->fence);

retry:
	if (src_list) {
		unsigned shared_count = src_list->shared_count;

		rcu_read_unlock();

		dst_list = dma_resv_list_alloc(shared_count);
		if (!dst_list)
			return -ENOMEM;

		rcu_read_lock();
		src_list = rcu_dereference(src->fence);
		if (!src_list || src_list->shared_count > shared_count) {
			kfree(dst_list);
			goto retry;
		}

		dst_list->shared_count = 0;
		for (i = 0; i < src_list->shared_count; ++i) {
			struct dma_fence *fence;

			fence = rcu_dereference(src_list->shared[i]);
			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &fence->flags))
				continue;

			if (!dma_fence_get_rcu(fence)) {
				dma_resv_list_free(dst_list);
				src_list = rcu_dereference(src->fence);
				goto retry;
			}

			if (dma_fence_is_signaled(fence)) {
				dma_fence_put(fence);
				continue;
			}

			rcu_assign_pointer(dst_list->shared[dst_list->shared_count++], fence);
		}
	} else {
		dst_list = NULL;
	}

	new = dma_fence_get_rcu_safe(&src->fence_excl);
	rcu_read_unlock();

	src_list = dma_resv_get_list(dst);
	old = dma_resv_get_excl(dst);

	preempt_disable();
	write_seqcount_begin(&dst->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(dst->fence_excl, new);
	RCU_INIT_POINTER(dst->fence, dst_list);
	write_seqcount_end(&dst->seq);
	preempt_enable();

	dma_resv_list_free(src_list);
	dma_fence_put(old);
#endif /* BSDTNG */
	return 0;
}
EXPORT_SYMBOL(dma_resv_copy_fences);

/**
 * dma_resv_get_fences_rcu - Get an object's shared and exclusive
 * fences without update side lock held
 * @obj: the reservation object
 * @pfence_excl: the returned exclusive fence (or NULL)
 * @pshared_count: the number of shared fences returned
 * @pshared: the array of shared fence ptrs returned (array is krealloc'd to
 * the required size, and must be freed by caller)
 *
 * Retrieve all fences from the reservation object. If the pointer for the
 * exclusive fence is not specified the fence is put into the array of the
 * shared fences as well. Returns either zero or -ENOMEM.
 */
int dma_resv_get_fences_rcu(struct dma_resv *obj,
			    struct dma_fence **pfence_excl,
			    unsigned *pshared_count,
			    struct dma_fence ***pshared)
{
#ifdef BSDTNG
	if (!pfence_excl) {
		/* include write locks into shared */
		return dma_resv_get_fences(
			obj, DMA_RESV_USAGE_WRITE, pshared_count, pshared
		);
	}
	unsigned int count = 0;
	int ret = dma_resv_get_fences(
		obj, DMA_RESV_USAGE_WRITE, &count, pfence_excl
	);

	return ret || dma_resv_get_fences(
		obj, DMA_RESV_USAGE_READ, pshared_count, pshared
	);
#else
	struct dma_fence **shared = NULL;
	struct dma_fence *fence_excl;
	unsigned int shared_count;
	int ret = 1;

	do {
		struct dma_resv_list *fobj;
		unsigned int i, seq;
		size_t sz = 0;

		shared_count = i = 0;

		rcu_read_lock();
		seq = read_seqcount_begin(&obj->seq);

		fence_excl = rcu_dereference(obj->fence_excl);
		if (fence_excl && !dma_fence_get_rcu(fence_excl))
			goto unlock;

		fobj = rcu_dereference(obj->fence);
		if (fobj)
			sz += sizeof(*shared) * fobj->shared_max;

		if (!pfence_excl && fence_excl)
			sz += sizeof(*shared);

		if (sz) {
			struct dma_fence **nshared;

			nshared = krealloc(shared, sz,
					   GFP_NOWAIT | __GFP_NOWARN);
			if (!nshared) {
				rcu_read_unlock();

				dma_fence_put(fence_excl);
				fence_excl = NULL;

				nshared = krealloc(shared, sz, GFP_KERNEL);
				if (nshared) {
					shared = nshared;
					continue;
				}

				ret = -ENOMEM;
				break;
			}
			shared = nshared;
			shared_count = fobj ? fobj->shared_count : 0;
			for (i = 0; i < shared_count; ++i) {
				shared[i] = rcu_dereference(fobj->shared[i]);
				if (!dma_fence_get_rcu(shared[i]))
					break;
			}
		}

		if (i != shared_count || read_seqcount_retry(&obj->seq, seq)) {
			while (i--)
				dma_fence_put(shared[i]);
			dma_fence_put(fence_excl);
			goto unlock;
		}

		ret = 0;
unlock:
		rcu_read_unlock();
#ifdef __FreeBSD__
		/*
		 * On FreeBSD, thread holding reservation object seqcount lock
		 * for write may be blocked. In that case reader thread should
		 * be blocked too.
		 */
		if (ret != 0) {
			rw_rlock(&obj->rw);
			rw_runlock(&obj->rw);
		}
#endif
	} while (ret);

	if (pfence_excl)
		*pfence_excl = fence_excl;
	else if (fence_excl)
		shared[shared_count++] = fence_excl;

	if (!shared_count) {
		kfree(shared);
		shared = NULL;
	}

	*pshared_count = shared_count;
	*pshared = shared;
	return ret;
#endif /* BSDTNG */
}
EXPORT_SYMBOL_GPL(dma_resv_get_fences_rcu);

/**
 * dma_resv_wait_timeout_rcu - Wait on reservation's objects
 * shared and/or exclusive fences.
 * @obj: the reservation object
 * @wait_all: if true, wait on all fences, else wait on just exclusive fence
 * @intr: if true, do interruptible wait
 * @timeout: timeout value in jiffies or zero to return immediately
 *
 * RETURNS
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or
 * greater than zer on success.
 */
long dma_resv_wait_timeout_rcu(struct dma_resv *obj,
			       bool wait_all, bool intr,
			       unsigned long timeout)
{
#ifdef BSDTNG
	return dma_resv_wait_timeout(
		obj,
		wait_all ? DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ,
		intr,
		timeout
	);
#else
	struct dma_fence *fence;
	unsigned seq, shared_count;
	long ret = timeout ? timeout : 1;
	int i;

retry:
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();
	i = -1;

	fence = rcu_dereference(obj->fence_excl);
	if (fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		if (!dma_fence_get_rcu(fence))
			goto unlock_retry;

		if (dma_fence_is_signaled(fence)) {
			dma_fence_put(fence);
			fence = NULL;
		}

	} else {
		fence = NULL;
	}

	if (wait_all) {
		struct dma_resv_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		for (i = 0; !fence && i < shared_count; ++i) {
			struct dma_fence *lfence = rcu_dereference(fobj->shared[i]);

			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &lfence->flags))
				continue;

			if (!dma_fence_get_rcu(lfence))
				goto unlock_retry;

			if (dma_fence_is_signaled(lfence)) {
				dma_fence_put(lfence);
				continue;
			}

			fence = lfence;
			break;
		}
	}

	rcu_read_unlock();
	if (fence) {
		if (read_seqcount_retry(&obj->seq, seq)) {
			dma_fence_put(fence);
			goto retry;
		}

		ret = dma_fence_wait_timeout(fence, intr, ret);
		dma_fence_put(fence);
		if (ret > 0 && wait_all && (i + 1 < shared_count))
			goto retry;
	}
	return ret;

unlock_retry:
	rcu_read_unlock();
#ifdef __FreeBSD__
	rw_rlock(&obj->rw);
	rw_runlock(&obj->rw);
#endif
	goto retry;
#endif /* BSDTNG */
}
EXPORT_SYMBOL_GPL(dma_resv_wait_timeout_rcu);

#ifndef BSDTNG
static inline int dma_resv_test_signaled_single(struct dma_fence *passed_fence)
{
	struct dma_fence *fence, *lfence = passed_fence;
	int ret = 1;

	if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &lfence->flags)) {
		fence = dma_fence_get_rcu(lfence);
		if (!fence)
			return -1;

		ret = !!dma_fence_is_signaled(fence);
		dma_fence_put(fence);
	}
	return ret;
}
#endif /* BSDTNG */

/**
 * dma_resv_test_signaled_rcu - Test if a reservation object's
 * fences have been signaled.
 * @obj: the reservation object
 * @test_all: if true, test all fences, otherwise only test the exclusive
 * fence
 *
 * RETURNS
 * true if all fences signaled, else false
 */
bool dma_resv_test_signaled_rcu(struct dma_resv *obj, bool test_all)
{
#ifdef BSDTNG
	return dma_resv_test_signaled(
		obj, test_all ? DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ	
	);
#else
	unsigned seq, shared_count;
	int ret;

	rcu_read_lock();
retry:
	ret = true;
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);

	if (test_all) {
		unsigned i;

		struct dma_resv_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		for (i = 0; i < shared_count; ++i) {
			struct dma_fence *fence = rcu_dereference(fobj->shared[i]);

			ret = dma_resv_test_signaled_single(fence);
			if (ret < 0)
				goto retry;
			else if (!ret)
				break;
		}

		if (read_seqcount_retry(&obj->seq, seq))
#ifdef __linux__
			goto retry;
#elif defined(__FreeBSD__)
		{
			rcu_read_unlock();
			rw_rlock(&obj->rw);
			rw_runlock(&obj->rw);
			rcu_read_lock();
			goto retry;
		}
#endif
	}

	if (!shared_count) {
		struct dma_fence *fence_excl = rcu_dereference(obj->fence_excl);

		if (fence_excl) {
			ret = dma_resv_test_signaled_single(fence_excl);
			if (ret < 0)
				goto retry;

			if (read_seqcount_retry(&obj->seq, seq))
#ifdef __linux__
				goto retry;
#elif defined(__FreeBSD__)
			{
				rcu_read_unlock();
				rw_rlock(&obj->rw);
				rw_runlock(&obj->rw);
				rcu_read_lock();
				goto retry;
			}
#endif
		}
	}

	rcu_read_unlock();
	return ret;
#endif /* BSDTNG */
}
EXPORT_SYMBOL_GPL(dma_resv_test_signaled_rcu);
