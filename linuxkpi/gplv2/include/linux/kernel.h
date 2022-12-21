
#ifndef _LINUX_KERNEL_H_
#define _LINUX_KERNEL_H_

#include_next <linux/kernel.h>

#define typeof_member(s, e)        typeof(((s *)0)->e)

#endif
