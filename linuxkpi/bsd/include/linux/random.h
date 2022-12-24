#ifndef _LINUX_RANDOM_H_
#define _LINUX_RANDOM_H_

#include_next <linux/random.h>

static inline u64
get_random_u64(void)
{
        u64 val;

        get_random_bytes(&val, sizeof(val));
        return (val);
}

#endif