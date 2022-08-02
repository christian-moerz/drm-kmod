/* Public domain. */

#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include_next <linux/pm.h>

#if __FreeBSD_version < 1400065

#define	PM_EVENT_FREEZE		0x0001
#define	PM_EVENT_SUSPEND	0x0002

struct dev_pm_domain {
};
#endif

#endif
