#ifndef _LINUX_XARRAY_H_
#define _LINUX_XARRAY_H_

#include_next <linux/xarray.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#ifdef BSDTNG
/* FIXME BSD this needs to be resolved with spin locks */
#define xa_trylock(xa)		mtx_trylock(&(xa)->mtx)
#define xa_lock_bh(xa)		mtx_lock(&(xa)->mtx)
#define xa_unlock_bh(xa)	mtx_unlock(&(xa)->mtx)
#define xa_lock_irq(xa)		mtx_lock(&(xa)->mtx)
#define xa_unlock_irq(xa)	mtx_unlock(&(xa)->mtx)
#define xa_lock_irqsave(xa, flags) \
				mtx_lock_flags(&(xa)->mtx, flags)
#define xa_unlock_irqrestore(xa, flags) \
				mtx_unlock_flags(&(xa)->mtx, flags)
#endif

#endif