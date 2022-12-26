#ifndef _LINUX_MM_H_
#define _LINUX_MM_H_

#include_next <linux/mm.h>

#ifdef BSDTNG
#ifdef MAPLE
#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

/* FIXME LINUXKPI */

#if defined(CONFIG_64BIT) || defined(BUILD_VDSO32_64)
/* 64bit sizes */
#define MAPLE_NODE_SLOTS	31	/* 256 bytes including ->parent */
#define MAPLE_RANGE64_SLOTS	16	/* 256 bytes */
#define MAPLE_ARANGE64_SLOTS	10	/* 240 bytes */
#define MAPLE_ARANGE64_META_MAX	15	/* Out of range for metadata */
#define MAPLE_ALLOC_SLOTS	(MAPLE_NODE_SLOTS - 1)
#else
/* 32bit sizes */
#define MAPLE_NODE_SLOTS	63	/* 256 bytes including ->parent */
#define MAPLE_RANGE64_SLOTS	32	/* 256 bytes */
#define MAPLE_ARANGE64_SLOTS	21	/* 240 bytes */
#define MAPLE_ARANGE64_META_MAX	31	/* Out of range for metadata */
#define MAPLE_ALLOC_SLOTS	(MAPLE_NODE_SLOTS - 2)
#endif /* defined(CONFIG_64BIT) || defined(BUILD_VDSO32_64) */

#define MAPLE_NODE_MASK		255UL

#define mtree_lock(mt)		spin_lock((&(mt)->ma_lock))
#define mtree_unlock(mt)	spin_unlock((&(mt)->ma_lock))

/*
 * If the tree contains a single entry at index 0, it is usually stored in
 * tree->ma_root.  To optimise for the page cache, an entry which ends in '00',
 * '01' or '11' is stored in the root, but an entry which ends in '10' will be
 * stored in a node.  Bits 3-6 are used to store enum maple_type.
 *
 * The flags are used both to store some immutable information about this tree
 * (set at tree creation time) and dynamic information set under the spinlock.
 *
 * Another use of flags are to indicate global states of the tree.  This is the
 * case with the MAPLE_USE_RCU flag, which indicates the tree is currently in
 * RCU mode.  This mode was added to allow the tree to reuse nodes instead of
 * re-allocating and RCU freeing nodes when there is a single user.
 */
struct maple_tree {
	union {
		spinlock_t	ma_lock;
		lockdep_map_p	ma_external_lock;
	};
	void __rcu      *ma_root;
	unsigned int	ma_flags;
};

struct maple_alloc {
	unsigned long total;
	unsigned char node_count;
	unsigned int request_count;
	struct maple_alloc *slot[MAPLE_ALLOC_SLOTS];
};

struct ma_state {
	struct maple_tree *tree;	/* The tree we're operating in */
	unsigned long index;		/* The index we're operating on - range start */
	unsigned long last;		/* The last index we're operating on - range end */
	struct maple_enode *node;	/* The node containing this entry */
	unsigned long min;		/* The minimum index of this node - implied pivot min */
	unsigned long max;		/* The maximum index of this node - implied pivot max */
	struct maple_alloc *alloc;	/* Allocated nodes for this operation */
	unsigned char depth;		/* depth of tree descent during write */
	unsigned char offset;
	unsigned char mas_flags;
};

struct vma_iterator {
	struct ma_state mas;
};

#define VMA_ITERATOR(name, __mm, __addr)				\
	struct vma_iterator name = {					\
		.mas = {						\
			.tree = &(__mm)->mm_mt,				\
			.index = __addr,				\
			.node = MAS_START,				\
		},							\
	}

static inline void vma_iter_init(struct vma_iterator *vmi,
		struct mm_struct *mm, unsigned long addr)
{
	vmi->mas.tree = &mm->mm_mt;
	vmi->mas.index = addr;
	vmi->mas.node = MAS_START;
}
#endif /* MAPLE */

static inline bool is_cow_mapping(unsigned long flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

static inline void
might_alloc(const unsigned int flags)
{
	/* FIXME BSD this is how OpenBSD does it...
	if (flags & M_WAITOK)
		assertwaitok();
	*/
}

#define IOMEM_ERR_PTR(err) (__force void __iomem *)ERR_PTR(err)
#endif /* BSDTNG */
#endif