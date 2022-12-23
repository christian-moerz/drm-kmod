// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>

#include <drm/drm_gem_ttm_helper.h>

/**
 * DOC: overview
 *
 * This library provides helper functions for gem objects backed by
 * ttm.
 */

/**
 * drm_gem_ttm_print_info() - Print &ttm_buffer_object info for debugfs
 * @p: DRM printer
 * @indent: Tab indentation level
 * @gem: GEM object
 *
 * This function can be used as &drm_gem_object_funcs.print_info
 * callback.
 */
void drm_gem_ttm_print_info(struct drm_printer *p, unsigned int indent,
			    const struct drm_gem_object *gem)
{
	static const char * const plname[] = {
		[ TTM_PL_SYSTEM ] = "system",
		[ TTM_PL_TT     ] = "tt",
		[ TTM_PL_VRAM   ] = "vram",
		[ TTM_PL_PRIV   ] = "priv",

		[ 16 ]            = "cached",
		[ 17 ]            = "uncached",
		[ 18 ]            = "wc",
		[ 19 ]            = "contig",

		[ 21 ]            = "pinned", /* NO_EVICT */
		[ 22 ]            = "topdown",
	};
	const struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);

	drm_printf_indent(p, indent, "placement=");
#ifdef BSDTNG
	drm_print_bits(p, bo->resource->placement, plname, ARRAY_SIZE(plname));
#else
	drm_print_bits(p, bo->mem.placement, plname, ARRAY_SIZE(plname));
#endif	
	drm_printf(p, "\n");

#ifdef BSDTNG
	if (bo->resource->bus.is_iomem)
#else
	if (bo->mem.bus.is_iomem)
#endif
		drm_printf_indent(p, indent, "bus.offset=%lx\n",
#ifdef BSDTNG
				  (unsigned long)bo->resource->bus.offset);
#else				  
				  (unsigned long)bo->mem.bus.offset);
#endif
}
EXPORT_SYMBOL(drm_gem_ttm_print_info);

/**
 * drm_gem_ttm_vmap() - vmap &ttm_buffer_object
 * @gem: GEM object.
 * @map: [out] returns the dma-buf mapping.
 *
 * Maps a GEM object with ttm_bo_vmap(). This function can be used as
 * &drm_gem_object_funcs.vmap callback.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
#ifdef BSDTNG
int drm_gem_ttm_vmap(struct drm_gem_object *gem,
		     struct iosys_map *map)
#else
int drm_gem_ttm_vmap(struct drm_gem_object *gem,
		     struct dma_buf_map *map)
#endif
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	int ret;

#ifdef BSDTNG
	dma_resv_lock(gem->resv, NULL);
#endif
	ret = ttm_bo_vmap(bo, map);
#ifdef BSDTNG
	dma_resv_unlock(gem->resv);
#endif

	return ret;
}
EXPORT_SYMBOL(drm_gem_ttm_vmap);

/**
 * drm_gem_ttm_vunmap() - vunmap &ttm_buffer_object
 * @gem: GEM object.
 * @map: dma-buf mapping.
 *
 * Unmaps a GEM object with ttm_bo_vunmap(). This function can be used as
 * &drm_gem_object_funcs.vmap callback.
 */
#ifdef BSDTNG
void drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			struct iosys_map *map)
#else
void drm_gem_ttm_vunmap(struct drm_gem_object *gem,
			struct dma_buf_map *map)
#endif
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);

#ifdef BSDTNG
	dma_resv_lock(gem->resv, NULL);
#endif
	ttm_bo_vunmap(bo, map);
#ifdef BSDTNG
	dma_resv_unlock(gem->resv);
#endif
}
EXPORT_SYMBOL(drm_gem_ttm_vunmap);

/**
 * drm_gem_ttm_mmap() - mmap &ttm_buffer_object
 * @gem: GEM object.
 * @vma: vm area.
 *
 * This function can be used as &drm_gem_object_funcs.mmap
 * callback.
 */
#ifdef BSDTNG
int drm_gem_ttm_mmap(struct drm_gem_object *gem,
		     struct vm_area_struct *vma)
#else
int drm_gem_ttm_mmap(struct drm_gem_object *gem,
		     struct vm_area_struct *vma)
#endif
{
	struct ttm_buffer_object *bo = drm_gem_ttm_of_gem(gem);
	int ret;

	ret = ttm_bo_mmap_obj(vma, bo);
	if (ret < 0)
		return ret;

	/*
	 * ttm has its own object refcounting, so drop gem reference
	 * to avoid double accounting counting.
	 */
	drm_gem_object_put(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_ttm_mmap);

#ifdef BSDTNG
/**
 * drm_gem_ttm_dumb_map_offset() - Implements struct &drm_driver.dumb_map_offset
 * @file:	DRM file pointer.
 * @dev:	DRM device.
 * @handle:	GEM handle
 * @offset:	Returns the mapping's memory offset on success
 *
 * Provides an implementation of struct &drm_driver.dumb_map_offset for
 * TTM-based GEM drivers. TTM allocates the offset internally and
 * drm_gem_ttm_dumb_map_offset() returns it for dumb-buffer implementations.
 *
 * See struct &drm_driver.dumb_map_offset.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_ttm_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
				uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem)
		return -ENOENT;

	*offset = drm_vma_node_offset_addr(&gem->vma_node);

	drm_gem_object_put(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_ttm_dumb_map_offset);
#endif

MODULE_DESCRIPTION("DRM gem ttm helpers");
MODULE_LICENSE("GPL");
