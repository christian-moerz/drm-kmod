/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * Copyright (c) 2023 Jean-Sébastien Pédron <dumbbell@FreeBSD.org>
 *
 * This initial software `sys/dev/vt/hw/vt_fb.c` was developed by Aleksandr
 * Rybalko under sponsorship from the FreeBSD Foundation.
 * This file is a copy of the initial file and is modified by Jean-Sébastien
 * Pédron.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/reboot.h>
#include <sys/fbio.h>
#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <linux/fb.h>

#include <drm/drm_fb_helper.h>

/*
 * `drm_fb_helper.h` redefines `fb_info` to be `linux_fb_info` to manage the
 * name conflict between the Linux and FreeBSD structures, while avoiding a
 * extensive rewrite and use of macros in the original drm_fb_helper.[ch]
 * files.
 *
 * We need to undo this here because we use both structures.
 */
#undef	fb_info

#include <drm/drm_os_freebsd.h>

#include "vt_drmfb.h"

#define	to_drm_fb_helper(fbio) ((struct drm_fb_helper *)fbio->fb_priv)
#define	to_linux_fb_info(fbio) (to_drm_fb_helper(fbio)->fbdev)

static struct vt_driver vt_drmfb_driver = {
	.vd_name = "drmfb",
	.vd_init = vt_drmfb_init,
	.vd_fini = vt_drmfb_fini,
	.vd_blank = vt_drmfb_blank,
	.vd_bitblt_text = vt_drmfb_bitblt_text,
	.vd_bitblt_bmp = vt_drmfb_bitblt_bitmap,
	.vd_drawrect = vt_drmfb_drawrect,
	.vd_setpixel = vt_drmfb_setpixel,
	.vd_postswitch = vt_drmfb_postswitch,
	.vd_priority = VD_PRIORITY_GENERIC+20,
	.vd_suspend = vt_drmfb_suspend,
	.vd_resume = vt_drmfb_resume,

	/* Use vt_fb implementation */
	.vd_invalidate_text = vt_fb_invalidate_text,
	.vd_fb_ioctl = vt_fb_ioctl,
	.vd_fb_mmap = vt_fb_mmap,
};

static bool already_switching_inside_panic = false;

VT_DRIVER_DECLARE(vt_drmfb, vt_drmfb_driver);

void
vt_drmfb_setpixel(struct vt_device *vd, int x, int y, term_color_t color)
{
	vt_drmfb_drawrect(vd, x, y, x, y, 1, color);
}

void
vt_drmfb_drawrect(
    struct vt_device *vd,
    int x1, int y1, int x2, int y2, int fill,
    term_color_t color)
{
	struct fb_info *fbio;
	struct linux_fb_info *info;
	struct fb_fillrect rect;

	fbio = vd->vd_softc;
	info = to_linux_fb_info(fbio);
	if (info->fbops->fb_fillrect == NULL) {
		log(LOG_ERR, "No fb_fillrect callback defined\n");
		return;
	}

	KASSERT(
	    (x2 >= x1),
	    ("Invalid rectangle X coordinates passed to vd_drawrect: "
	     "x1=%d > x2=%d", x1, x2));
	KASSERT(
	    (y2 >= y1),
	    ("Invalid rectangle Y coordinates passed to vd_drawrect: "
	     "y1=%d > y2=%d", y1, y2));
	KASSERT(
	    (fill != 0),
	    ("`fill=0` argument to vd_drawrect unsupported in vt_drmfb"));

	rect.dx = x1;
	rect.dy = y1;
	rect.width = x2 - x1 + 1;
	rect.height = y2 - y1 + 1;
	rect.color = fbio->fb_cmap[color];
	rect.rop = ROP_COPY;

	info->fbops->fb_fillrect(info, &rect);

	printf("vt_drmfb: drawrect completed\n");
}

void
vt_drmfb_blank(struct vt_device *vd, term_color_t color)
{
	struct fb_info *fbio;
	struct linux_fb_info *info;
	int x1, y1, x2, y2;

	fbio = vd->vd_softc;
	info = to_linux_fb_info(fbio);

	x1 = info->var.xoffset;
	y1 = info->var.yoffset;
	x2 = info->var.xres - 1;
	y2 = info->var.yres - 1;

	vt_drmfb_drawrect(vd, x1, y1, x2, y2, 1, color);
	printf("vt_drmfb: blank completed\n");
}

void
vt_drmfb_bitblt_bitmap(struct vt_device *vd, const struct vt_window *vw,
    const uint8_t *pattern, const uint8_t *mask,
    unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, term_color_t fg, term_color_t bg)
{
	struct fb_info *fbio;
	struct linux_fb_info *info;
	struct fb_image image;

	KASSERT(
	    (mask != NULL),
	    ("`mask!=NULL` argument to vd_bitblt_bitmap unsupported in "
	     "vt_drmfb"));

	fbio = vd->vd_softc;
	info = to_linux_fb_info(fbio);
	if (info->fbops->fb_imageblit == NULL) {
		log(LOG_ERR, "No fb_imageblit callback defined\n");
		return;
	}

	image.dx = x;
	image.dy = y;
	image.width = width;
	image.height = height;
	image.fg_color = fbio->fb_cmap[fg];
	image.bg_color = fbio->fb_cmap[bg];
	image.depth = 1;
	image.data = pattern;

	info->fbops->fb_imageblit(info, &image);
	printf("vt_drmfb: bitmap completed\n");
}

void
vt_drmfb_bitblt_text(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area)
{
	unsigned int col, row, x, y;
	struct vt_font *vf;
	term_char_t c;
	term_color_t fg, bg;
	const uint8_t *pattern;
	size_t z;

	vf = vw->vw_font;

	for (row = area->tr_begin.tp_row; row < area->tr_end.tp_row; ++row) {
		for (col = area->tr_begin.tp_col; col < area->tr_end.tp_col;
		    ++col) {
			x = col * vf->vf_width +
			    vw->vw_draw_area.tr_begin.tp_col;
			y = row * vf->vf_height +
			    vw->vw_draw_area.tr_begin.tp_row;

			c = VTBUF_GET_FIELD(&vw->vw_buf, row, col);
			pattern = vtfont_lookup(vf, c);
			vt_determine_colors(c,
			    VTBUF_ISCURSOR(&vw->vw_buf, row, col), &fg, &bg);

			z = row * PIXEL_WIDTH(VT_FB_MAX_WIDTH) + col;
			if (z >= PIXEL_HEIGHT(VT_FB_MAX_HEIGHT) *
			    PIXEL_WIDTH(VT_FB_MAX_WIDTH))
				continue;
			if (vd->vd_drawn && (vd->vd_drawn[z] == c) &&
			    vd->vd_drawnfg && (vd->vd_drawnfg[z] == fg) &&
			    vd->vd_drawnbg && (vd->vd_drawnbg[z] == bg))
				continue;

			vt_drmfb_bitblt_bitmap(vd, vw,
			    pattern, NULL, vf->vf_width, vf->vf_height,
			    x, y, fg, bg);

			if (vd->vd_drawn)
				vd->vd_drawn[z] = c;
			if (vd->vd_drawnfg)
				vd->vd_drawnfg[z] = fg;
			if (vd->vd_drawnbg)
				vd->vd_drawnbg[z] = bg;
		}
	}

#ifndef SC_NO_CUTPASTE
	if (!vd->vd_mshown)
		return;

	term_rect_t drawn_area;

	drawn_area.tr_begin.tp_col = area->tr_begin.tp_col * vf->vf_width;
	drawn_area.tr_begin.tp_row = area->tr_begin.tp_row * vf->vf_height;
	drawn_area.tr_end.tp_col = area->tr_end.tp_col * vf->vf_width;
	drawn_area.tr_end.tp_row = area->tr_end.tp_row * vf->vf_height;

	if (vt_is_cursor_in_area(vd, &drawn_area)) {
		vt_drmfb_bitblt_bitmap(vd, vw,
		    vd->vd_mcursor->map, vd->vd_mcursor->mask,
		    vd->vd_mcursor->width, vd->vd_mcursor->height,
		    vd->vd_mx_drawn + vw->vw_draw_area.tr_begin.tp_col,
		    vd->vd_my_drawn + vw->vw_draw_area.tr_begin.tp_row,
		    vd->vd_mcursor_fg, vd->vd_mcursor_bg);
	}
#endif
	printf("vt_drmfb: text completed\n");
}

void
vt_drmfb_postswitch(struct vt_device *vd)
{
	struct fb_info *fbio;
	struct linux_fb_info *info;

	printf("vt_drmfb: starting postswitch\n");

	fbio = vd->vd_softc;

	/* taken on from vt_fb */
	printf("vt_drmfb: calling fbio enter\n");
	if (fbio->enter != NULL)
		fbio->enter(fbio->fb_priv);

	info = to_linux_fb_info(fbio);
	if (info->fbops->fb_set_par == NULL) {
		log(LOG_ERR, "No fb_set_par callback defined\n");
		return;
	}

	if (!kdb_active && !KERNEL_PANICKED()) {
		linux_set_current(curthread);
		printf("vt_drmfb: postswitch calling set_par\n");
		info->fbops->fb_set_par(info);
	} else {
#ifdef DDB
		db_trace_self_depth(10);
		mdelay(1000);
#endif
		if (already_switching_inside_panic || skip_ddb) {
			spinlock_enter();
			doadump(false);
			EVENTHANDLER_INVOKE(shutdown_final, RB_NOSYNC);
		}

		already_switching_inside_panic = true;
		linux_set_current(curthread);
		info->fbops->fb_set_par(info);
		already_switching_inside_panic = false;
	}

	printf("vt_drmfb: finishing postswitch\n");
}

static int
vt_drmfb_init_colors(struct fb_info *info)
{

	switch (FBTYPE_GET_BPP(info)) {
	case 8:
		return (vt_config_cons_colors(info, COLOR_FORMAT_RGB,
		    0x7, 5, 0x7, 2, 0x3, 0));
	case 15:
		return (vt_config_cons_colors(info, COLOR_FORMAT_RGB,
		    0x1f, 10, 0x1f, 5, 0x1f, 0));
	case 16:
		return (vt_config_cons_colors(info, COLOR_FORMAT_RGB,
		    0x1f, 11, 0x3f, 5, 0x1f, 0));
	case 24:
	case 32: /* Ignore alpha. */
		return (vt_config_cons_colors(info, COLOR_FORMAT_RGB,
		    0xff, 16, 0xff, 8, 0xff, 0));
	default:
		return (1);
	}
}

int
vt_drmfb_init(struct vt_device *vd)
{
	printf("vt_drmfb: calling vt_fb_init\n");
	return vt_fb_init(vd);
}

void
vt_drmfb_fini(struct vt_device *vd, void *softc)
{
	return vt_fb_fini(vd, softc);
}

int
vt_drmfb_attach(struct fb_info *info)
{
	int ret;

	ret = vt_allocate(&vt_drmfb_driver, info);

	return (ret);
}

int
vt_drmfb_detach(struct fb_info *info)
{
	int ret;

	ret = vt_deallocate(&vt_drmfb_driver, info);

	return (ret);
}

void
vt_drmfb_suspend(struct vt_device *vd)
{
	vt_suspend(vd);
}

void
vt_drmfb_resume(struct vt_device *vd)
{
	vt_resume(vd);
}
