/* SPDX-License-Identifier: GPL-2.0-or-later or MIT */
/*
 * Copyright (C) 2016 Noralf Trønnes
 */

#ifndef __LINUX_DRM_FORMAT_HELPER_H
#define __LINUX_DRM_FORMAT_HELPER_H

#ifdef BSDTNG
#include <linux/types.h>

struct drm_device;
struct drm_format_info;
#endif /* BSDTNG */
struct drm_framebuffer;
struct drm_rect;

#ifdef BSDTNG
struct iosys_map;

unsigned int drm_fb_clip_offset(unsigned int pitch, const struct drm_format_info *format,
				const struct drm_rect *clip);

void drm_fb_memcpy(struct iosys_map *dst, const unsigned int *dst_pitch,
		   const struct iosys_map *src, const struct drm_framebuffer *fb,
		   const struct drm_rect *clip);
void drm_fb_swab(struct iosys_map *dst, const unsigned int *dst_pitch,
		 const struct iosys_map *src, const struct drm_framebuffer *fb,
		 const struct drm_rect *clip, bool cached);
void drm_fb_xrgb8888_to_rgb332(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip);
void drm_fb_xrgb8888_to_rgb565(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb888(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip);
void drm_fb_xrgb8888_to_xrgb2101010(struct iosys_map *dst, const unsigned int *dst_pitch,
				    const struct iosys_map *src, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip);
void drm_fb_xrgb8888_to_gray8(struct iosys_map *dst, const unsigned int *dst_pitch,
			      const struct iosys_map *src, const struct drm_framebuffer *fb,
			      const struct drm_rect *clip);

int drm_fb_blit(struct iosys_map *dst, const unsigned int *dst_pitch, uint32_t dst_format,
		const struct iosys_map *src, const struct drm_framebuffer *fb,
		const struct drm_rect *rect);

void drm_fb_xrgb8888_to_mono(struct iosys_map *dst, const unsigned int *dst_pitch,
			     const struct iosys_map *src, const struct drm_framebuffer *fb,
			     const struct drm_rect *clip);

size_t drm_fb_build_fourcc_list(struct drm_device *dev,
				const u32 *native_fourccs, size_t native_nfourccs,
				const u32 *extra_fourccs, size_t extra_nfourccs,
				u32 *fourccs_out, size_t nfourccs_out);
#else
void drm_fb_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip);
void drm_fb_memcpy_dstclip(void __iomem *dst, void *vaddr,
			   struct drm_framebuffer *fb,
			   struct drm_rect *clip);
void drm_fb_swab(void *dst, void *src, struct drm_framebuffer *fb,
		 struct drm_rect *clip, bool cached);
void drm_fb_xrgb8888_to_rgb565(void *dst, void *vaddr,
			       struct drm_framebuffer *fb,
			       struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb565_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip, bool swab);
void drm_fb_xrgb8888_to_rgb888_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip);
void drm_fb_xrgb8888_to_gray8(u8 *dst, void *vaddr, struct drm_framebuffer *fb,
			      struct drm_rect *clip);
#endif /* !BSDTNG */
#endif /* __LINUX_DRM_FORMAT_HELPER_H */
