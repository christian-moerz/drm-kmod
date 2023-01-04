/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2022 Christian Moerz
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
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