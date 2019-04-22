/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dal_asic_id.h"
#include "dc_types.h"
#include "dccg.h"
#include "clk_mgr_internal.h"

#include "dce/dce_clk_mgr.h"
#include "dce/dce110_clk_mgr.h"
#include "dce/dce112_clk_mgr.h"
#include "dce/dce120_clk_mgr.h"
#include "rv1_clk_mgr.h"
#include "rv2_clk_mgr.h"

struct clk_mgr *dc_clk_mgr_create(struct dc_context *ctx, struct pp_smu_funcs *pp_smu, struct dccg *dccg)
{
	struct hw_asic_id asic_id = ctx->asic_id;

	struct clk_mgr_internal *clk_mgr = kzalloc(sizeof(*clk_mgr), GFP_KERNEL);

	if (clk_mgr == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	switch (asic_id.chip_family) {
	case FAMILY_CI:
	case FAMILY_KV:
		dce_clk_mgr_construct(ctx, clk_mgr);
		break;
	case FAMILY_CZ:
		dce110_clk_mgr_construct(ctx, clk_mgr);
		break;
	case FAMILY_VI:
		if (ASIC_REV_IS_TONGA_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(asic_id.hw_internal_rev)) {
			dce_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev)) {
			dce112_clk_mgr_construct(ctx, clk_mgr);
			break;
		}
		break;
	case FAMILY_AI:
		if (ASICREV_IS_VEGA20_P(asic_id.hw_internal_rev))
			dce121_clk_mgr_construct(ctx, clk_mgr);
		else
			dce120_clk_mgr_construct(ctx, clk_mgr);
		break;

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	case FAMILY_RV:

#if defined(CONFIG_DRM_AMD_DC_DCN1_01)
		if (ASICREV_IS_RAVEN2(asic_id.hw_internal_rev)) {
			rv2_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			break;
		}
#endif	/* DCN1_01 */

		if (ASICREV_IS_RAVEN(asic_id.hw_internal_rev) ||
				ASICREV_IS_PICASSO(asic_id.hw_internal_rev)) {
			rv1_clk_mgr_construct(ctx, clk_mgr, pp_smu);
			break;
		}
		break;
#endif	/* Family RV */

	default:
		ASSERT(0); /* Unknown Asic */
		break;
	}

	return &clk_mgr->base;
}

void dc_destroy_clk_mgr(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	kfree(clk_mgr);
}
