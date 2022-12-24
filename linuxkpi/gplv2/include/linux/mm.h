#ifndef _LINUX_MM_H_
#define _LINUX_MM_H_

#include_next <linux/mm.h>

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

#endif