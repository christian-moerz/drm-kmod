#ifndef _LINUX_XARRAY_H_
#define _LINUX_XARRAY_H_

#include_next <linux/xarray.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#ifdef BSDTNG
#if defined(XARRAY_EXPERIMENTAL)
/* NOTE cm 2023/01/02 original experiment failed; spinlocks just don't go 
	together with current linuxkpi implementation of xarrays */

/* FIXME LINUXKPI */
/* NOTE cm 2023/01/02 linuxkpi currently implements xa_destroy incorrectly */
void linuxkpi_xa_destroy(struct xarray *xa);
#endif

/* FIXME BSD this needs to be resolved with spin locks */
#define xa_trylock(xa)		mtx_trylock(&(xa)->mtx)
#define xa_lock_bh(xa)		mtx_lock(&(xa)->mtx)
#define xa_unlock_bh(xa)	mtx_unlock(&(xa)->mtx)
#define xa_lock_irq(xa)		mtx_lock(&(xa)->mtx)
#define xa_unlock_irq(xa)	mtx_unlock(&(xa)->mtx)
#define xa_lock_irqsave(xa, flags) \
				mtx_lock(&(xa)->mtx); if (flags==flags) { flags=flags; }
#define xa_unlock_irqrestore(xa, flags) \
				mtx_unlock(&(xa)->mtx); if (flags==flags) { flags=flags; }
#endif /* BSDTNG */

#endif