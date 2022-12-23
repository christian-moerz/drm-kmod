#ifndef _LINUX_MM_H_
#define _LINUX_MM_H_

#include_next <linux/mm.h>

static inline bool is_cow_mapping(unsigned long flags)
{
	return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}

#endif