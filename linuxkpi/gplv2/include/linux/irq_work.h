/*	$OpenBSD: irq_work.h,v 1.9 2022/07/27 07:08:34 jsg Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <linux/llist.h>
#include <linux/workqueue.h>

#define	LKPI_IRQ_WORK_STD_TQ	system_wq->taskqueue
#define	LKPI_IRQ_WORK_FAST_TQ	linux_irq_work_tq

#ifdef LKPI_IRQ_WORK_USE_FAST_TQ
#define	LKPI_IRQ_WORK_TQ	LKPI_IRQ_WORK_FAST_TQ
#else
#define	LKPI_IRQ_WORK_TQ	LKPI_IRQ_WORK_STD_TQ
#endif

struct workqueue_struct;

struct irq_node {
	struct llist_node llist;
};

struct irq_work;
typedef void (*irq_work_func_t)(struct irq_work *);

struct irq_work {
	struct task task;
	irq_work_func_t func;
	struct irq_node node;
};

extern struct taskqueue *linux_irq_work_tq;

#define	DEFINE_IRQ_WORK(name, _func)	struct irq_work name = {	\
	.irq_task = TASK_INITIALIZER(0, linux_irq_work_fn, &(name)),	\
	.func  = (_func),						\
}

void	linux_irq_work_fn(void *, int);

static inline void
init_irq_work(struct irq_work *irqw, irq_work_func_t func)
{
	TASK_INIT(&irqw->task, 0, linux_irq_work_fn, irqw);
	irqw->func = func;
}

static inline bool
irq_work_queue(struct irq_work *irqw)
{
	return (taskqueue_enqueue_flags(LKPI_IRQ_WORK_TQ, &irqw->task,
	    TASKQUEUE_FAIL_IF_PENDING) == 0);
}

static inline void
irq_work_sync(struct irq_work *irqw)
{
	taskqueue_drain(LKPI_IRQ_WORK_TQ, &irqw->task);
}

#endif
