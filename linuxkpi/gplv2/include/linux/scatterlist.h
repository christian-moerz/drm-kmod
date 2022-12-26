#ifndef _LINUX_SCATTERLIST_H_
#define _LINUX_SCATTERLIST_H_

#include_next <linux/scatterlist.h>

#ifdef BSDTNG
struct sg_append_table {
	struct sg_table sgt;		/* The scatter list table */
	struct scatterlist *prv;	/* last populated sge in the table */
	unsigned int total_nents;	/* Total entries in the table */
};

/**
 * sg_free_append_table - Free a previously allocated append sg table.
 * @table:	 The mapped sg append table header
 *
 **/
static void sg_free_append_table(struct sg_append_table *table)
{
	__sg_free_table(&table->sgt, SG_MAX_SINGLE_ALLOC, 0, sg_kfree);
}


static struct scatterlist *get_next_sg(struct sg_append_table *table,
				       struct scatterlist *cur,
				       unsigned long needed_sges,
				       gfp_t gfp_mask)
{
	struct scatterlist *new_sg, *next_sg;
	unsigned int alloc_size;

	if (cur) {
		next_sg = sg_next(cur);
		/* Check if last entry should be keeped for chainning */
		if (!sg_is_last(next_sg) || needed_sges == 1)
			return next_sg;
	}

	alloc_size = min_t(unsigned long, needed_sges, SG_MAX_SINGLE_ALLOC);
	new_sg = sg_kmalloc(alloc_size, gfp_mask);
	if (!new_sg)
		return ERR_PTR(-ENOMEM);
	sg_init_table(new_sg, alloc_size);
	if (cur) {
		table->total_nents += alloc_size - 1;
		sg_chain(next_sg, table->total_nents, new_sg);
	} else {
		table->sgt.sgl = new_sg;
		table->total_nents = alloc_size;
	}
	return new_sg;
}

/**
 * sg_alloc_append_table_from_pages - Allocate and initialize an append sg
 *                                    table from an array of pages
 * @sgt_append:  The sg append table to use
 * @pages:       Pointer to an array of page pointers
 * @n_pages:     Number of pages in the pages array
 * @offset:      Offset from start of the first page to the start of a buffer
 * @size:        Number of valid bytes in the buffer (after offset)
 * @max_segment: Maximum size of a scatterlist element in bytes
 * @left_pages:  Left pages caller have to set after this call
 * @gfp_mask:	 GFP allocation mask
 *
 * Description:
 *    In the first call it allocate and initialize an sg table from a list of
 *    pages, else reuse the scatterlist from sgt_append. Contiguous ranges of
 *    the pages are squashed into a single scatterlist entry up to the maximum
 *    size specified in @max_segment.  A user may provide an offset at a start
 *    and a size of valid data in a buffer specified by the page array. The
 *    returned sg table is released by sg_free_append_table
 *
 * Returns:
 *   0 on success, negative error on failure
 *
 * Notes:
 *   If this function returns non-0 (eg failure), the caller must call
 *   sg_free_append_table() to cleanup any leftover allocations.
 *
 *   In the fist call, sgt_append must by initialized.
 */
static int sg_alloc_append_table_from_pages(struct sg_append_table *sgt_append,
		struct page **pages, unsigned int n_pages, unsigned int offset,
		unsigned long size, unsigned int max_segment,
		unsigned int left_pages, gfp_t gfp_mask)
{
	unsigned int chunks, cur_page, seg_len, i, prv_len = 0;
	unsigned int added_nents = 0;
	struct scatterlist *s = sgt_append->prv;

	/*
	 * The algorithm below requires max_segment to be aligned to PAGE_SIZE
	 * otherwise it can overshoot.
	 */
	max_segment = ALIGN_DOWN(max_segment, PAGE_SIZE);
	if (WARN_ON(max_segment < PAGE_SIZE))
		return -EINVAL;

	if (IS_ENABLED(CONFIG_ARCH_NO_SG_CHAIN) && sgt_append->prv)
		return -EOPNOTSUPP;

	if (sgt_append->prv) {
		unsigned long paddr =
			(page_to_pfn(sg_page(sgt_append->prv)) * PAGE_SIZE +
			 sgt_append->prv->offset + sgt_append->prv->length) /
			PAGE_SIZE;

		if (WARN_ON(offset))
			return -EINVAL;

		/* Merge contiguous pages into the last SG */
		prv_len = sgt_append->prv->length;
		while (n_pages && page_to_pfn(pages[0]) == paddr) {
			if (sgt_append->prv->length + PAGE_SIZE > max_segment)
				break;
			sgt_append->prv->length += PAGE_SIZE;
			paddr++;
			pages++;
			n_pages--;
		}
		if (!n_pages)
			goto out;
	}

	/* compute number of contiguous chunks */
	chunks = 1;
	seg_len = 0;
	for (i = 1; i < n_pages; i++) {
		seg_len += PAGE_SIZE;
		if (seg_len >= max_segment ||
		    page_to_pfn(pages[i]) != page_to_pfn(pages[i - 1]) + 1) {
			chunks++;
			seg_len = 0;
		}
	}

	/* merging chunks and putting them into the scatterlist */
	cur_page = 0;
	for (i = 0; i < chunks; i++) {
		unsigned int j, chunk_size;

		/* look for the end of the current chunk */
		seg_len = 0;
		for (j = cur_page + 1; j < n_pages; j++) {
			seg_len += PAGE_SIZE;
			if (seg_len >= max_segment ||
			    page_to_pfn(pages[j]) !=
			    page_to_pfn(pages[j - 1]) + 1)
				break;
		}

		/* Pass how many chunks might be left */
		s = get_next_sg(sgt_append, s, chunks - i + left_pages,
				gfp_mask);
		if (IS_ERR(s)) {
			/*
			 * Adjust entry length to be as before function was
			 * called.
			 */
			if (sgt_append->prv)
				sgt_append->prv->length = prv_len;
			return PTR_ERR(s);
		}
		chunk_size = ((j - cur_page) << PAGE_SHIFT) - offset;
		sg_set_page(s, pages[cur_page],
			    min_t(unsigned long, size, chunk_size), offset);
		added_nents++;
		size -= chunk_size;
		offset = 0;
		cur_page = j;
	}
	sgt_append->sgt.nents += added_nents;
	sgt_append->sgt.orig_nents = sgt_append->sgt.nents;
	sgt_append->prv = s;
out:
	if (!left_pages)
		sg_mark_end(s);
	return 0;
}

/* FIXME LINUXKPI I suppose... */
/**
 * sg_alloc_table_from_pages_segment - Allocate and initialize an sg table from
 *                                     an array of pages and given maximum
 *                                     segment.
 * @sgt:	 The sg table header to use
 * @pages:	 Pointer to an array of page pointers
 * @n_pages:	 Number of pages in the pages array
 * @offset:      Offset from start of the first page to the start of a buffer
 * @size:        Number of valid bytes in the buffer (after offset)
 * @max_segment: Maximum size of a scatterlist element in bytes
 * @gfp_mask:	 GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table from a list of pages. Contiguous
 *    ranges of the pages are squashed into a single scatterlist node up to the
 *    maximum size specified in @max_segment. A user may provide an offset at a
 *    start and a size of valid data in a buffer specified by the page array.
 *
 *    The returned sg table is released by sg_free_table.
 *
 *  Returns:
 *   0 on success, negative error on failure
 */
static int sg_alloc_table_from_pages_segment(struct sg_table *sgt, struct page **pages,
				unsigned int n_pages, unsigned int offset,
				unsigned long size, unsigned int max_segment,
				gfp_t gfp_mask)
{
	struct sg_append_table append = {};
	int err;

	err = sg_alloc_append_table_from_pages(&append, pages, n_pages, offset,
					       size, max_segment, 0, gfp_mask);
	if (err) {
		sg_free_append_table(&append);
		return err;
	}
	memcpy(sgt, &append.sgt, sizeof(*sgt));
	WARN_ON(append.total_nents != sgt->orig_nents);
	return 0;
}

#endif /* BSDTNG */
#endif