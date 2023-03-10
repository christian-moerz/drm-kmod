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

#ifndef _LINUX_DMA_FENCE_ARRAY_H_
#define _LINUX_DMA_FENCE_ARRAY_H_

#include <linux/dma-fence.h>
#include <linux/irq_work.h>

#ifdef BSDTNG
#include <linux/export.h>

/**
 * dma_fence_array_for_each - iterate over all fences in array
 * @fence: current fence
 * @index: index into the array
 * @head: potential dma_fence_array object
 *
 * Test if @array is a dma_fence_array object and if yes iterate over all fences
 * in the array. If not just iterate over the fence in @array itself.
 *
 * For a deep dive iterator see dma_fence_unwrap_for_each().
 */
#define dma_fence_array_for_each(fence, index, head)			\
	for (index = 0, fence = dma_fence_array_first(head); fence;	\
	     ++(index), fence = dma_fence_array_next(head, index))
#endif

struct dma_fence_array_cb {
	struct dma_fence_cb cb;
	struct dma_fence_array *array;
};

struct dma_fence_array {
	struct dma_fence base;
	spinlock_t lock;
	unsigned int num_fences;
	atomic_t num_pending;
	struct dma_fence **fences;
	struct irq_work work;
};

#ifndef BSDTNG
bool dma_fence_is_array(struct dma_fence *fence);
struct dma_fence_array *to_dma_fence_array(struct dma_fence *fence);
#endif

struct dma_fence_array *dma_fence_array_create(int num_fences,
    struct dma_fence **fences, u64 context, unsigned seqno,
    bool signal_on_any);

#ifdef BSDTNG
/**
 * to_dma_fence_array - cast a fence to a dma_fence_array
 * @fence: fence to cast to a dma_fence_array
 *
 * Returns NULL if the fence is not a dma_fence_array,
 * or the dma_fence_array otherwise.
 */
static inline struct dma_fence_array *
to_dma_fence_array(struct dma_fence *fence)
{
	if (!fence || !dma_fence_is_array(fence))
		return NULL;

	return container_of(fence, struct dma_fence_array, base);
}

#define dma_fence_array_for_each(fence, index, head)			\
	for (index = 0, fence = dma_fence_array_first(head); fence;	\
	     ++(index), fence = dma_fence_array_next(head, index))


bool dma_fence_match_context(struct dma_fence *fence, u64 context);

struct dma_fence *dma_fence_array_first(struct dma_fence *head);
struct dma_fence *dma_fence_array_next(struct dma_fence *head,
				       unsigned int index);
#endif /* BSDTNG */

#endif /* _LINUX_DMA_FENCE_ARRAY_H_ */
