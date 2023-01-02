/* Public domain. */

#include <drm/drm_gem.h>
#include <drm/drm_framebuffer.h>

MALLOC_DECLARE(DRM_MEM_KMS);

#ifdef BSDTNG
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#if defined(__FreeBSD__)
#include <linux/iosys-map.h>
#include <drm/drm_fourcc.h>
#endif

#include "drm_internal.h"

MODULE_IMPORT_NS(DMA_BUF);

#define AFBC_HEADER_SIZE		16
#define AFBC_TH_LAYOUT_ALIGNMENT	8
#define AFBC_HDR_ALIGN			64
#define AFBC_SUPERBLOCK_PIXELS		256
#define AFBC_SUPERBLOCK_ALIGNMENT	128
#define AFBC_TH_BODY_START_ALIGNMENT	4096

/**
 * DOC: overview
 *
 * This library provides helpers for drivers that don't subclass
 * &drm_framebuffer and use &drm_gem_object for their backing storage.
 *
 * Drivers without additional needs to validate framebuffers can simply use
 * drm_gem_fb_create() and everything is wired up automatically. Other drivers
 * can use all parts independently.
 */

/**
 * drm_gem_fb_get_obj() - Get GEM object backing the framebuffer
 * @fb: Framebuffer
 * @plane: Plane index
 *
 * No additional reference is taken beyond the one that the &drm_frambuffer
 * already holds.
 *
 * Returns:
 * Pointer to &drm_gem_object for the given framebuffer and plane index or NULL
 * if it does not exist.
 */
struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,
					  unsigned int plane)
{
	struct drm_device *dev = fb->dev;

	if (drm_WARN_ON_ONCE(dev, plane >= ARRAY_SIZE(fb->obj)))
		return NULL;
	else if (drm_WARN_ON_ONCE(dev, !fb->obj[plane]))
		return NULL;

	return fb->obj[plane];
}
EXPORT_SYMBOL_GPL(drm_gem_fb_get_obj);

static int
drm_gem_fb_init(struct drm_device *dev,
		 struct drm_framebuffer *fb,
		 const struct drm_mode_fb_cmd2 *mode_cmd,
		 struct drm_gem_object **obj, unsigned int num_planes,
		 const struct drm_framebuffer_funcs *funcs)
{
	unsigned int i;
	int ret;

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = obj[i];

	ret = drm_framebuffer_init(dev, fb, funcs);
	if (ret)
		drm_err(dev, "Failed to init framebuffer: %d\n", ret);

	return ret;
}
#endif /* BSDTNG */

void
drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	int i;

	for (i = 0; i < 4; i++)
		drm_gem_object_put(fb->obj[i]);
	drm_framebuffer_cleanup(fb);
	free(fb, DRM_MEM_KMS);
}

int
drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
    unsigned int *handle)
{
	return drm_gem_handle_create(file, fb->obj[0], handle);
}

#ifdef BSDTNG
/**
 * drm_gem_fb_vmap - maps all framebuffer BOs into kernel address space
 * @fb: the framebuffer
 * @map: returns the mapping's address for each BO
 * @data: returns the data address for each BO, can be NULL
 *
 * This function maps all buffer objects of the given framebuffer into
 * kernel address space and stores them in struct iosys_map. If the
 * mapping operation fails for one of the BOs, the function unmaps the
 * already established mappings automatically.
 *
 * Callers that want to access a BO's stored data should pass @data.
 * The argument returns the addresses of the data stored in each BO. This
 * is different from @map if the framebuffer's offsets field is non-zero.
 *
 * Both, @map and @data, must each refer to arrays with at least
 * fb->format->num_planes elements.
 *
 * See drm_gem_fb_vunmap() for unmapping.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_fb_vmap(struct drm_framebuffer *fb, struct iosys_map *map,
		    struct iosys_map *data)
{
	struct drm_gem_object *obj;
	unsigned int i;
	int ret;

	for (i = 0; i < fb->format->num_planes; ++i) {
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj) {
			ret = -EINVAL;
			goto err_drm_gem_vunmap;
		}
		ret = drm_gem_vmap(obj, &map[i]);
		if (ret)
			goto err_drm_gem_vunmap;
	}

	if (data) {
		for (i = 0; i < fb->format->num_planes; ++i) {
			memcpy(&data[i], &map[i], sizeof(data[i]));
			if (iosys_map_is_null(&data[i]))
				continue;
			iosys_map_incr(&data[i], fb->offsets[i]);
		}
	}

	return 0;

err_drm_gem_vunmap:
	while (i) {
		--i;
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj)
			continue;
		drm_gem_vunmap(obj, &map[i]);
	}
	return ret;
}
EXPORT_SYMBOL(drm_gem_fb_vmap);

/**
 * drm_gem_fb_vunmap - unmaps framebuffer BOs from kernel address space
 * @fb: the framebuffer
 * @map: mapping addresses as returned by drm_gem_fb_vmap()
 *
 * This function unmaps all buffer objects of the given framebuffer.
 *
 * See drm_gem_fb_vmap() for more information.
 */
void drm_gem_fb_vunmap(struct drm_framebuffer *fb, struct iosys_map *map)
{
	unsigned int i = fb->format->num_planes;
	struct drm_gem_object *obj;

	while (i) {
		--i;
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj)
			continue;
		if (iosys_map_is_null(&map[i]))
			continue;
		drm_gem_vunmap(obj, &map[i]);
	}
}
EXPORT_SYMBOL(drm_gem_fb_vunmap);
#endif /* BSDTNG */