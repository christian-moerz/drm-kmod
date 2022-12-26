/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_GGTT_GMCH_H__
#define __INTEL_GGTT_GMCH_H__

#include "intel_gtt.h"

/* For x86 platforms */
#if IS_ENABLED(CONFIG_X86)

void intel_ggtt_gmch_flush(void);
int intel_ggtt_gmch_enable_hw(struct drm_i915_private *i915);
int intel_ggtt_gmch_probe(struct i915_ggtt *ggtt);
#ifdef BSDTNG
void intel_gmch_gtt_clear_range(unsigned int first_entry, unsigned int num_entries);
#endif
/* Stubs for non-x86 platforms */
#else

static inline void intel_ggtt_gmch_flush(void) { }
static inline int intel_ggtt_gmch_enable_hw(struct drm_i915_private *i915) { return -ENODEV; }
static inline int intel_ggtt_gmch_probe(struct i915_ggtt *ggtt) { return -ENODEV; }
#ifdef BSDTNG
static void intel_gmch_gtt_clear_range(unsigned int first_entry, unsigned int num_entries) {}
#endif
#endif

#endif /* __INTEL_GGTT_GMCH_H__ */
