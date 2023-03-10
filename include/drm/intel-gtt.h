/* Public Domain */
#ifndef _DRM_INTEL_GTT_H
#define	_DRM_INTEL_GTT_H

#include <linux/agp_backend.h>
#include <linux/kernel.h>
#if defined(__FreeBSD__)
#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#endif

struct agp_bridge_data;
struct intel_gtt;
struct intel_gtt *intel_gtt_get(void);

int intel_gmch_probe(struct pci_dev *bridge_pdev, struct pci_dev *gpu_pdev,
		     struct agp_bridge_data *bridge);
void intel_gmch_remove(void);
bool intel_enable_gtt(void);
int intel_gtt_chipset_flush(void);
void intel_gtt_insert_page(dma_addr_t addr,
			   unsigned int pg,
			   unsigned int flags);
#ifdef BSDTNG
bool intel_gmch_enable_gtt(void);
void intel_gmch_gtt_flush(void);
#endif
void linux_intel_gtt_insert_sg_entries(struct sg_table *st,
    unsigned int pg_start, unsigned int flags);
void intel_gtt_clear_range(unsigned int first_entry, unsigned int num_entries);

#endif
