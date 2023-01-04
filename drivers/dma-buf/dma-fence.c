/*-
 * Copyright (c) 2022 Beckhoff Automation GmbH & Co. KG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <linux/dma-fence.h>
#ifdef BSDTNG
#include <linux/export.h>
#endif

MALLOC_DECLARE(M_DMABUF);

static struct dma_fence dma_fence_stub;
static DEFINE_SPINLOCK(dma_fence_stub_lock);

static const char *
dma_fence_stub_get_name(struct dma_fence *fence)
{

	return ("stub");
}

static const struct dma_fence_ops dma_fence_stub_ops = {
	.get_driver_name = dma_fence_stub_get_name,
	.get_timeline_name = dma_fence_stub_get_name,
};

/*
 * return a signaled fence
 */
struct dma_fence *
dma_fence_get_stub(void)
{

	spin_lock(&dma_fence_stub_lock);
	if (dma_fence_stub.ops == NULL) {
		dma_fence_init(&dma_fence_stub,
		    &dma_fence_stub_ops,
		    &dma_fence_stub_lock,
		    0,
		    0);
#ifdef BSDTNG
		set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			&dma_fence_stub.flags);
#endif
		dma_fence_signal_locked(&dma_fence_stub);
	}
	spin_unlock(&dma_fence_stub_lock);
	return (dma_fence_get(&dma_fence_stub));
}
#ifdef BSDTNG

/**
 * dma_fence_allocate_private_stub - return a private, signaled fence
 *
 * Return a newly allocated and signaled stub fence.
 */
struct dma_fence *dma_fence_allocate_private_stub(void)
{
	struct dma_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL)
		return ERR_PTR(-ENOMEM);

	dma_fence_init(fence,
		       &dma_fence_stub_ops,
		       &dma_fence_stub_lock,
		       0, 0);

	set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
		&dma_fence_stub.flags);

	dma_fence_signal(fence);

	return fence;
}
EXPORT_SYMBOL(dma_fence_allocate_private_stub);
#endif /* BSDTNG */

static atomic64_t dma_fence_context_counter = ATOMIC64_INIT(1);

/*
 * allocate an array of fence contexts
 */
u64
dma_fence_context_alloc(unsigned num)
{

	return (atomic64_fetch_add(num, &dma_fence_context_counter));
}

/**
 * DOC: fence signalling annotation
 *
 * Proving correctness of all the kernel code around &dma_fence through code
 * review and testing is tricky for a few reasons:
 *
 * * It is a cross-driver contract, and therefore all drivers must follow the
 *   same rules for lock nesting order, calling contexts for various functions
 *   and anything else significant for in-kernel interfaces. But it is also
 *   impossible to test all drivers in a single machine, hence brute-force N vs.
 *   N testing of all combinations is impossible. Even just limiting to the
 *   possible combinations is infeasible.
 *
 * * There is an enormous amount of driver code involved. For render drivers
 *   there's the tail of command submission, after fences are published,
 *   scheduler code, interrupt and workers to process job completion,
 *   and timeout, gpu reset and gpu hang recovery code. Plus for integration
 *   with core mm with have &mmu_notifier, respectively &mmu_interval_notifier,
 *   and &shrinker. For modesetting drivers there's the commit tail functions
 *   between when fences for an atomic modeset are published, and when the
 *   corresponding vblank completes, including any interrupt processing and
 *   related workers. Auditing all that code, across all drivers, is not
 *   feasible.
 *
 * * Due to how many other subsystems are involved and the locking hierarchies
 *   this pulls in there is extremely thin wiggle-room for driver-specific
 *   differences. &dma_fence interacts with almost all of the core memory
 *   handling through page fault handlers via &dma_resv, dma_resv_lock() and
 *   dma_resv_unlock(). On the other side it also interacts through all
 *   allocation sites through &mmu_notifier and &shrinker.
 *
 * Furthermore lockdep does not handle cross-release dependencies, which means
 * any deadlocks between dma_fence_wait() and dma_fence_signal() can't be caught
 * at runtime with some quick testing. The simplest example is one thread
 * waiting on a &dma_fence while holding a lock::
 *
 *     lock(A);
 *     dma_fence_wait(B);
 *     unlock(A);
 *
 * while the other thread is stuck trying to acquire the same lock, which
 * prevents it from signalling the fence the previous thread is stuck waiting
 * on::
 *
 *     lock(A);
 *     unlock(A);
 *     dma_fence_signal(B);
 *
 * By manually annotating all code relevant to signalling a &dma_fence we can
 * teach lockdep about these dependencies, which also helps with the validation
 * headache since now lockdep can check all the rules for us::
 *
 *    cookie = dma_fence_begin_signalling();
 *    lock(A);
 *    unlock(A);
 *    dma_fence_signal(B);
 *    dma_fence_end_signalling(cookie);
 *
 * For using dma_fence_begin_signalling() and dma_fence_end_signalling() to
 * annotate critical sections the following rules need to be observed:
 *
 * * All code necessary to complete a &dma_fence must be annotated, from the
 *   point where a fence is accessible to other threads, to the point where
 *   dma_fence_signal() is called. Un-annotated code can contain deadlock issues,
 *   and due to the very strict rules and many corner cases it is infeasible to
 *   catch these just with review or normal stress testing.
 *
 * * &struct dma_resv deserves a special note, since the readers are only
 *   protected by rcu. This means the signalling critical section starts as soon
 *   as the new fences are installed, even before dma_resv_unlock() is called.
 *
 * * The only exception are fast paths and opportunistic signalling code, which
 *   calls dma_fence_signal() purely as an optimization, but is not required to
 *   guarantee completion of a &dma_fence. The usual example is a wait IOCTL
 *   which calls dma_fence_signal(), while the mandatory completion path goes
 *   through a hardware interrupt and possible job completion worker.
 *
 * * To aid composability of code, the annotations can be freely nested, as long
 *   as the overall locking hierarchy is consistent. The annotations also work
 *   both in interrupt and process context. Due to implementation details this
 *   requires that callers pass an opaque cookie from
 *   dma_fence_begin_signalling() to dma_fence_end_signalling().
 *
 * * Validation against the cross driver contract is implemented by priming
 *   lockdep with the relevant hierarchy at boot-up. This means even just
 *   testing with a single device is enough to validate a driver, at least as
 *   far as deadlocks with dma_fence_wait() against dma_fence_signal() are
 *   concerned.
 */
#ifdef CONFIG_LOCKDEP
#ifdef BSDTNG
static struct lockdep_map dma_fence_lockdep_map = {
	.name = "dma_fence_map"
};

/**
 * dma_fence_begin_signalling - begin a critical DMA fence signalling section
 *
 * Drivers should use this to annotate the beginning of any code section
 * required to eventually complete &dma_fence by calling dma_fence_signal().
 *
 * The end of these critical sections are annotated with
 * dma_fence_end_signalling().
 *
 * Returns:
 *
 * Opaque cookie needed by the implementation, which needs to be passed to
 * dma_fence_end_signalling().
 */
bool dma_fence_begin_signalling(void)
{
	/* explicitly nesting ... */
	if (lock_is_held_type(&dma_fence_lockdep_map, 1))
		return true;

	/* rely on might_sleep check for soft/hardirq locks */
	if (in_atomic())
		return true;

	/* ... and non-recursive readlock */
	lock_acquire(&dma_fence_lockdep_map, 0, 0, 1, 1, NULL, _RET_IP_);

	return false;
}
EXPORT_SYMBOL(dma_fence_begin_signalling);

/**
 * dma_fence_end_signalling - end a critical DMA fence signalling section
 * @cookie: opaque cookie from dma_fence_begin_signalling()
 *
 * Closes a critical section annotation opened by dma_fence_begin_signalling().
 */
void dma_fence_end_signalling(bool cookie)
{
	if (cookie)
		return;

	lock_release(&dma_fence_lockdep_map, _RET_IP_);
}
EXPORT_SYMBOL(dma_fence_end_signalling);

void __dma_fence_might_wait(void)
{
	bool tmp;

	tmp = lock_is_held_type(&dma_fence_lockdep_map, 1);
	if (tmp)
		lock_release(&dma_fence_lockdep_map, _THIS_IP_);
	lock_map_acquire(&dma_fence_lockdep_map);
	lock_map_release(&dma_fence_lockdep_map);
	if (tmp)
		lock_acquire(&dma_fence_lockdep_map, 0, 0, 1, 1, NULL, _THIS_IP_);
}
#endif /* BSDTNG */
#endif

/*
 * signal completion of a fence
 */
int
dma_fence_signal_timestamp_locked(struct dma_fence *fence,
				  ktime_t timestamp)
{
	struct dma_fence_cb *cur, *tmp;
	struct list_head cb_list;

	if (fence == NULL)
		return (-EINVAL);
	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
	      &fence->flags))
		return (-EINVAL);

	list_replace(&fence->cb_list, &cb_list);

	fence->timestamp = timestamp;
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);

	list_for_each_entry_safe(cur, tmp, &cb_list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}

	return (0);
}

/*
 * signal completion of a fence
 */
int
dma_fence_signal_timestamp(struct dma_fence *fence, ktime_t timestamp)
{
	int rv;

	if (fence == NULL)
		return (-EINVAL);

	spin_lock(fence->lock);
	rv = dma_fence_signal_timestamp_locked(fence, timestamp);
	spin_unlock(fence->lock);
	return (rv);
}

/*
 * signal completion of a fence
 */
int
dma_fence_signal_locked(struct dma_fence *fence)
{
	return dma_fence_signal_timestamp_locked(fence, ktime_get());
}

/*
 * signal completion of a fence
 */
int
dma_fence_signal(struct dma_fence *fence)
{
	int rv;
#ifdef BSDTNG
	bool sig;
#endif

	if (fence == NULL)
		return (-EINVAL);

#ifdef BSDTNG
	sig = dma_fence_begin_signalling();
#endif

	spin_lock(fence->lock);
	rv = dma_fence_signal_timestamp_locked(fence, ktime_get());
	spin_unlock(fence->lock);

#ifdef BSDTNG
	dma_fence_end_signalling(sig);
#endif
	return (rv);
}

/*
 * sleep until the fence gets signaled or until timeout elapses
 */
signed long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	int rv;

	if (fence == NULL)
		return (-EINVAL);

#ifdef BSDTNG
	might_sleep();

	__dma_fence_might_wait();

	dma_fence_enable_sw_signaling(fence);
#endif

	if (fence->ops && fence->ops->wait != NULL)
		rv = fence->ops->wait(fence, intr, timeout);
	else
		rv = dma_fence_default_wait(fence, intr, timeout);
	return (rv);
}

/*
 * default relese function for fences
 */
void
dma_fence_release(struct kref *kref)
{
	struct dma_fence *fence;

	fence = container_of(kref, struct dma_fence, refcount);
#ifdef BSDTNG
	if (WARN(!list_empty(&fence->cb_list) &&
		 !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags),
		 "Fence %s:%s:%lx:%lx released with pending signals!\n",
		 fence->ops->get_driver_name(fence),
		 fence->ops->get_timeline_name(fence),
		 fence->context, fence->seqno)) {

		/*
		 * Failed to signal before release, likely a refcounting issue.
		 *
		 * This should never happen, but if it does make sure that we
		 * don't leave chains dangling. We set the error flag first
		 * so that the callbacks know this signal is due to an error.
		 */
		spin_lock(fence->lock);
		fence->error = -EDEADLK;
		dma_fence_signal_locked(fence);
		spin_unlock(fence->lock);
	}
#endif

	if (fence->ops && fence->ops->release)
		fence->ops->release(fence);
	else
		dma_fence_free(fence);
}

/*
 * default release function for dma_fence
 */
void
dma_fence_free(struct dma_fence *fence)
{

	kfree_rcu(fence, rcu);
}

#ifdef BSDTNG

static bool __dma_fence_enable_signaling(struct dma_fence *fence)
{
	bool was_enabled;

	lockdep_assert_held(fence->lock);

	was_enabled = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	    &fence->flags);
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return false;

	if (was_enabled == false &&
	    fence->ops && fence->ops->enable_signaling) {
		if (fence->ops->enable_signaling(fence) == false) {
			dma_fence_signal_locked(fence);
			return false;
		}
	}

	return true;
}
#endif /* BSDTNG */

/*
 * enable signaling on fence
 */
void
dma_fence_enable_sw_signaling(struct dma_fence *fence)
{

	spin_lock(fence->lock);
	__dma_fence_enable_signaling(fence);
	spin_unlock(fence->lock);
}

/*
 * add a callback to be called when the fence is signaled
 */
int
dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb,
			   dma_fence_func_t func)
{
	int rv = -ENOENT;

	if (fence == NULL || func == NULL)
		return (-EINVAL);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		INIT_LIST_HEAD(&cb->node);
		return (-ENOENT);
	}

	spin_lock(fence->lock);
#ifndef BSDTNG
	bool was_enabled;

	was_enabled = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	    &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		rv = -ENOENT;
	else if (was_enabled == false && fence->ops
	    && fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			rv = -ENOENT;
		}
	}

	if (!rv) {
#else
	if (__dma_fence_enable_signaling(fence)) {
#endif
		cb->func = func;
		list_add_tail(&cb->node, &fence->cb_list);
		rv = 0;
	} else
		INIT_LIST_HEAD(&cb->node);
	spin_unlock(fence->lock);

	return (rv);
}

/*
 * returns the status upon completion
 */
int
dma_fence_get_status(struct dma_fence *fence)
{
	int rv;

	spin_lock(fence->lock);
	rv = dma_fence_get_status_locked(fence);
	spin_unlock(fence->lock);
	return (rv);
}

/*
 * remove a callback from the signaling list
 */
bool
dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	int rv;

	spin_lock(fence->lock);
	rv = !list_empty(&cb->node);
	if (rv)
		list_del_init(&cb->node);
	spin_unlock(fence->lock);
	return (rv);
}


struct default_wait_cb {
	struct dma_fence_cb base;
	struct task_struct *task;
};

static void
dma_fence_default_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct default_wait_cb *wait =
		container_of(cb, struct default_wait_cb, base);

	wake_up_state(wait->task, TASK_NORMAL);
}

/*
 * default sleep until the fence gets signaled or until timeout elapses
 */
signed long
dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	struct default_wait_cb cb;
	signed long rv = timeout ? timeout : 1;

#ifndef BSDTNG
	bool was_enabled;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return (rv);
#endif

	spin_lock(fence->lock);

#ifndef BSDTNG
	was_enabled = 
#endif
	test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	    &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

#ifdef BSDTNG
	if (intr && signal_pending(current)) {
		rv = -ERESTARTSYS;
#else
	if (was_enabled == false && fence->ops &&
	    fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
#endif
			goto out;
#ifndef BSDTNG
		}
#endif
	}

	if (timeout == 0) {
		rv = 0;
		goto out;
	}

	cb.base.func = dma_fence_default_wait_cb;
	cb.task = current;
	list_add(&cb.base.node, &fence->cb_list);

	while (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) && rv > 0) {
		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock(fence->lock);

		rv = schedule_timeout(rv);

		spin_lock(fence->lock);
		if (rv > 0 && intr && signal_pending(current))
			rv = -ERESTARTSYS;
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
	__set_current_state(TASK_RUNNING);

out:
	spin_unlock(fence->lock);
	return (rv);
}

static bool
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count,
    uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

/*
 * sleep until any fence gets signaled or until timeout elapses
 */
signed long
dma_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
			   bool intr, signed long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	long rv = timeout;
	int i;

	if (timeout == 0) {
#ifdef BSDTNG
		for (i = 0; i < count; ++i) {
#else
		for (i = 0; i < count; i++) {
#endif
			if (dma_fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return (1);
			}
		}
		return (0);
	}

	cb = malloc(sizeof(*cb), M_DMABUF, M_WAITOK | M_ZERO);
#ifdef BSDTNG
	for (i = 0; i < count; ++i) {
#else
	for (i = 0; i < count; i++) {
#endif
		struct dma_fence *fence = fences[i];
		cb[i].task = current;
		if (dma_fence_add_callback(fence, &cb[i].base,
		    dma_fence_default_wait_cb)) {
			if (idx)
				*idx = i;
			goto cb_cleanup;
		}
	}

	while (rv > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		if (dma_fence_test_signaled_any(fences, count, idx))
			break;

		rv = schedule_timeout(rv);

		if (rv > 0 && intr && signal_pending(current))
			rv = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

cb_cleanup:
	while (i-- > 0)
		dma_fence_remove_callback(fences[i], &cb[i].base);
	free(cb, M_DMABUF);
	return (rv);
}

#ifdef BSDTNG
/**
 * dma_fence_describe - Dump fence describtion into seq_file
 * @fence: the 6fence to describe
 * @seq: the seq_file to put the textual description into
 *
 * Dump a textual description of the fence and it's state into the seq_file.
 */
void dma_fence_describe(struct dma_fence *fence, struct seq_file *seq)
{
	seq_printf(seq, "%s %s seq %llu %ssignalled\n",
		   fence->ops->get_driver_name(fence),
		   fence->ops->get_timeline_name(fence), fence->seqno,
		   dma_fence_is_signaled(fence) ? "" : "un");
}
EXPORT_SYMBOL(dma_fence_describe);
#endif

/*
 * Initialize a custom fence.
 */
void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
    spinlock_t *lock, u64 context, u64 seqno)
{

	kref_init(&fence->refcount);
	INIT_LIST_HEAD(&fence->cb_list);
	fence->ops = ops;
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0;
	fence->error = 0;
}

/*
 * decreases refcount of the fence
 */
void
dma_fence_put(struct dma_fence *fence)
{

	if (fence)
		kref_put(&fence->refcount, dma_fence_release);
}

/* 
 * increases refcount of the fence
 */
struct dma_fence *
dma_fence_get(struct dma_fence *fence)
{

	if (fence)
		kref_get(&fence->refcount);
	return (fence);
}

/*
 * get a fence from a dma_resv_list with rcu read lock
 */
struct dma_fence *
dma_fence_get_rcu(struct dma_fence *fence)
{

	if (kref_get_unless_zero(&fence->refcount))
		return (fence);
	else
		return (NULL);
}

/*
 * acquire a reference to an RCU tracked fence
 */
struct dma_fence *
dma_fence_get_rcu_safe(struct dma_fence __rcu **fencep)
{

	do {
		struct dma_fence *fence;

		fence = rcu_dereference(*fencep);
		if (!fence)
			return (NULL);

		if (!dma_fence_get_rcu(fence))
			continue;

		if (fence == rcu_access_pointer(*fencep))
			return rcu_pointer_handoff(fence);

		dma_fence_put(fence);
	} while (1);
}

/*
 * Return an indication if the fence is signaled yet.
 */
bool
dma_fence_is_signaled_locked(struct dma_fence *fence)
{

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return (true);

	if (fence->ops->signaled && fence->ops->signaled(fence)) {
		dma_fence_signal_locked(fence);
		return (true);
	}

	return (false);
}

/*
 * Return an indication if the fence is signaled yet.
 */
bool
dma_fence_is_signaled(struct dma_fence *fence)
{

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return (true);

	if (fence->ops->signaled && fence->ops->signaled(fence)) {
		dma_fence_signal(fence);
		return (true);
	}

	return (false);
}

/*
 * return if f1 is chronologically later than f2
 */
bool
__dma_fence_is_later(u64 f1, u64 f2, const struct dma_fence_ops *ops)
{

	if (ops->use_64bit_seqno)
		return (f1 > f2);

	return (int)(lower_32_bits(f1) - lower_32_bits(f2)) > 0;
}

/*
 * return if f1 is chronologically later than f2
 */
bool
dma_fence_is_later(struct dma_fence *f1,
    struct dma_fence *f2)
{

	if (WARN_ON(f1->context != f2->context))
		return (false);

	return (__dma_fence_is_later(f1->seqno, f2->seqno, f1->ops));
}

/*
 * return the chronologically later fence
 */
struct dma_fence *
dma_fence_later(struct dma_fence *f1,
    struct dma_fence *f2)
{
	if (WARN_ON(f1->context != f2->context))
		return (NULL);

	if (dma_fence_is_later(f1, f2))
		return (dma_fence_is_signaled(f1) ? NULL : f1);
	else
		return (dma_fence_is_signaled(f2) ? NULL : f2);
}

/*
 * returns the status upon completion
 */
int
dma_fence_get_status_locked(struct dma_fence *fence)
{

	assert_spin_locked(fence->lock);
	if (dma_fence_is_signaled_locked(fence))
		return (fence->error ?: 1);
	else
		return (0);
}

/*
 * flag an error condition on the fence
 */
void
dma_fence_set_error(struct dma_fence *fence,
    int error)
{

	fence->error = error;
}

/*
 * sleep until the fence gets signaled
 */
signed long
dma_fence_wait(struct dma_fence *fence, bool intr)
{
	signed long ret;

	ret = dma_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);

	return (ret < 0 ? ret : 0);
}
