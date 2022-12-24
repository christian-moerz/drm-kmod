#ifndef _LINUX_XARRAY_H_
#define _LINUX_XARRAY_H_

#include_next <linux/xarray.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#define xa_trylock(xa)		mtx_trylock_spin(&(xa)->mtx)
#define xa_lock_bh(xa)		mtx_lock_spin(&(xa)->mtx)
#define xa_unlock_bh(xa)	mtx_unlock_spin(&(xa)->mtx)
#define xa_lock_irq(xa)		mtx_lock_spin(&(xa)->mtx)
#define xa_unlock_irq(xa)	mtx_unlock_spin(&(xa)->mtx)
#define xa_lock_irqsave(xa, flags) \
				mtx_lock_spin_flags(&(xa)->mtx, flags)
#define xa_unlock_irqrestore(xa, flags) \
				mtx_unlock_spin_flags(&(xa)->mtx, flags)

#endif