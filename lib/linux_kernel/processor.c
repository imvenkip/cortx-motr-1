/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 02/24/2011
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <asm/processor.h>
#include <linux/topology.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/processor.h"
#include "lib/list.h"

/**
   @addtogroup Processor

   This file includes additional data structures and functions for processing
   processors data - for kernel-mode programs.

   This file will also implement Linux kernel-mode processors interfaces.

   @see lib/processor.h

   @{
 */

/** Default values */
enum cache_size {
	DEFAULT_L1_SZ = 32*1024,
	DEFAULT_L2_SZ = 6144*1024
};

#ifdef CONFIG_X86_64
/** Intel CPUID op-code */
#define C2_PROCESSOR_INTEL_CPUID4_OP	4

#define C2_PROCESSOR_INTEL_CTYPE_MASK	0x1f
#define C2_PROCESSOR_INTEL_CTYPE_NULL	0

#define C2_PROCESSOR_INTEL_CLEVEL_MASK	0x7
#define C2_PROCESSOR_INTEL_CLEVEL_SHIFT	5

#define C2_PROCESSOR_INTEL_CSHARE_MASK	0xfff
#define C2_PROCESSOR_INTEL_CSHARE_SHIFT	14

#define C2_PROCESSOR_INTEL_LINESZ_MASK	0xfff

#define C2_PROCESSOR_INTEL_PARTITION_MASK	0x3f
#define C2_PROCESSOR_INTEL_PARTITION_SHIFT	12

#define C2_PROCESSOR_INTEL_ASSOCIATIVITY_MASK	0x3f
#define C2_PROCESSOR_INTEL_ASSOCIATIVITY_SHIFT	22

#define C2_PROCESSOR_L1_CACHE	1
#define C2_PROCESSOR_L2_CACHE	2

/** AMD CPUID op-code */
#define C2_PROCESSOR_AMD_L1_OP		0x80000005

#define C2_PROCESSOR_AMD_CSIZE_SHIFT	24

/**
   A node in the linked list describing processor properties. It
   encapsulates 'struct c2_processor_descr'. This will be used to cache
   attributes of x86 processors.
   @see lib/processor.h
 */
struct c2_processor_node {
	/** Linking structure for node */
	struct c2_list_link	pn_link;

	/** Processor descritor strcture */
	struct c2_processor_descr pn_info;
};

/*
   Overload function names.
 */
#define processor_info(a,b)		processor_find_x86info((a), (b))
#define processor_cache_create()	processor_x86cache_create()
#define processor_cache_destroy()	processor_x86cache_destroy()
#else
#define processor_info(a,b)		processor_get_info((a), (b))
#define processor_cache_create()	0
#define processor_cache_destroy()
#endif

/* Global variables */
static bool processor_init = false;
static struct c2_list x86_cpus;

/**
   Convert bitmap from one format to another. Copy cpumask bitmap to c2_bitmap.

   @param dest -> Processors bitmap for Colibri programs.
   @param src -> Processors bitmap used by Linux kernel.
   @param bmpsz -> Size of cpumask bitmap (src)

   @pre Assumes memory is alloacted for outbmp and it's initialized.

   @see lib/processor.h
   @see lib/bitmap.h
 */
static void processors_bitmap_copy(struct c2_bitmap *dest,
				   const cpumask_t *src,
				   uint32_t bmpsz)
{
	uint32_t bit;
	bool     val;

	C2_PRE(dest->b_nr >= bmpsz);

	for (bit = 0; bit < bmpsz; ++bit) {
		val = cpumask_test_cpu(bit, src);
		c2_bitmap_set(dest, bit, val);
	}
}

/**
   Fetch NUMA node id for a given processor.

   @param id -> id of the processor for which information is requested.

   @return id of the NUMA node to which the processor belongs.
 */
static inline uint32_t processor_get_numanodeid(c2_processor_nr_t id)
{
	return cpu_to_node(id);
}

/**
   Fetch pipeline id for a given processor.
   Curently pipeline id is same as processor id.

   @param id -> id of the processor for which information is requested.

   @return id of pipeline for the given processor.
 */
static inline uint32_t processor_get_pipelineid(c2_processor_nr_t id)
{
	return id;
}

/**
   Fetch the default L1 or L2 cache size for a given processor.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level (L1 or L2) for which id is requested.

   @return size of L1 or L2 cache size, in bytes, for the given processor.
 */
static size_t processor_get_cache_sz(c2_processor_nr_t id, uint32_t cache_level)
{
	uint32_t sz = 0;

	switch (cache_level) {
	case C2_PROCESSOR_L1_CACHE:
		sz = DEFAULT_L1_SZ;
		break;
	case C2_PROCESSOR_L2_CACHE:
		sz = DEFAULT_L2_SZ;
		break;
	default:
		break;
	}
	return sz;
}

#ifdef CONFIG_X86_64
/**
   Obtain cache level for a given INTEL x86 processor.

   @param eax -> value in eax register for INTEL x86.

   @return cache level of an intel x86 processor.
 */
static inline uint32_t processor_get_x86cache_level(uint32_t eax)
{
	uint32_t level;

	level = (eax >> C2_PROCESSOR_INTEL_CLEVEL_SHIFT) &
	        C2_PROCESSOR_INTEL_CLEVEL_MASK;
	return level;
}

/**
   Obtain number of processors sharing a given cache.

   @param eax -> value in eax register for INTEL x86.

   @return number of intel x86 processors sharing the cache (within
           the core or the physical package).
 */
static inline uint32_t processor_get_x86cache_shares(uint32_t eax)
{
	uint32_t shares;

	shares = (eax >> C2_PROCESSOR_INTEL_CSHARE_SHIFT) &
	        C2_PROCESSOR_INTEL_CSHARE_MASK;
	return shares;
}

/**
   Get the number cache leaves for x86 processor. For Intel use cpuid4
   instruction. For AMD (or other x86 vendors) assume that L2 is supported.

   @param id -> id of the processor for which caches leaves are requested.

   @return number of caches leaves.
 */
static uint32_t processor_get_x86cache_leaves(c2_processor_nr_t id)
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t cachetype;
	/*
	 * Assume AMD supports at least L2. For AMD processors this
	 * value is not used later.
	 */
	uint32_t leaves = 3;

	int count = -1;
	struct cpuinfo_x86 *p;

	/*
	 * Get Linux kernel CPU data.
	 */
	p = &cpu_data(id);

	if (p->x86_vendor == X86_VENDOR_INTEL) {
		do {
			count++;
			cpuid_count(C2_PROCESSOR_INTEL_CPUID4_OP, count,
				    &eax, &ebx, &ecx, &edx);
			cachetype = eax & C2_PROCESSOR_INTEL_CTYPE_MASK;

		} while (cachetype != C2_PROCESSOR_INTEL_CTYPE_NULL);
		leaves = count;
	}

	return leaves;
}

/**
   Fetch L1 or L2 cache id for a given x86 processor.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level (L1 or L2) for which id is requested.
   @param cache_leaves -> Number of cache leaves (levels) for the given
                          processor.

   @return id of L2 cache for the given x86 processor.
 */
static uint32_t processor_get_x86_cacheid(c2_processor_nr_t id,
					  uint32_t cache_level,
					  uint32_t cache_leaves)
{
	uint32_t cache_id = id;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t shares;
	uint32_t phys;
	uint32_t core;

	bool l3_present = false;
	bool cache_shared_at_core = false;

	struct cpuinfo_x86 *p;

	/*
	 * Get Linux kernel CPU data.
	 */
	p = &cpu_data(id);

	/*
	 * Get L1/L2 cache id for INTEL cpus. If INTEL cpuid level is less
	 * than 4, then use default value.
	 * For AMD cpus, like Linux kernel, assume that L1/L2 is not shared.
	 */
	if (p->x86_vendor == X86_VENDOR_INTEL &&
	    p->cpuid_level >= C2_PROCESSOR_INTEL_CPUID4_OP &&
	    cache_level < cache_leaves) {

		cpuid_count(C2_PROCESSOR_INTEL_CPUID4_OP, cache_level,
			    &eax, &ebx, &ecx, &edx);
		shares = processor_get_x86cache_shares(eax);

		if (shares > 0) {
			/*
			 * Check if L3 is present. We assume that if L3 is
			 * present then L2 is shared at core. Otherwise L2 is
			 * shared at physical package level.
			 */
			if (cache_leaves > 3) {
				l3_present = true;
			}
			phys = topology_physical_package_id(id);
			core = topology_core_id(id);
			switch (cache_level) {
			case C2_PROCESSOR_L1_CACHE:
				cache_shared_at_core = true;
				break;
			case C2_PROCESSOR_L2_CACHE:
				if (l3_present == true) {
					cache_shared_at_core = true;
				} else {
					cache_id = phys;
				}
				break;
			default:
				break;
			}
			if (cache_shared_at_core == true) {
				cache_id = phys << 16 | core;
			}/* end of if - cache shared at core */

		}/* cache is shared */

	}/* end of if - Intel processor with CPUID4 support */

	return cache_id;
}

/**
   A function to fetch cache size for an AMD x86 processor.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level (L1 or L2) for which id is requested.

   @return size of cache (in bytes) for the given AMD x86 processor.
 */
static uint32_t processor_get_amd_cache_sz(c2_processor_nr_t id,
					   uint32_t cache_level)
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t l1;
	uint32_t sz = 0;

	struct cpuinfo_x86 *p;

	switch (cache_level) {
	case C2_PROCESSOR_L1_CACHE:
		cpuid(C2_PROCESSOR_AMD_L1_OP, &eax, &ebx, &ecx, &l1);
		sz = (l1 >> C2_PROCESSOR_AMD_CSIZE_SHIFT) * 1024;
		break;
	case C2_PROCESSOR_L2_CACHE:
		p = &cpu_data(id);
		sz = p->x86_cache_size;
		break;
	default:
		break;
	}

	return sz;
}

/**
   A generic function to fetch cache size for an INTEL x86 processor.
   If Intel CPU does not support CPUID4, use default values.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level (L1 or L2) for which id is requested.

   @return size of cache (in bytes) for the given INTEL x86 processor.
 */
static uint32_t processor_get_intel_cache_sz(c2_processor_nr_t id,
					     uint32_t cache_level)
{
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t sets;
	uint32_t linesz;
	uint32_t partition;
	uint32_t asso;
	uint32_t level;
	uint32_t sz = 0;

	bool use_defaults = true;
	struct cpuinfo_x86 *p;

	/*
	 * Get Linux kernel CPU data.
	 */
	p = &cpu_data(id);

	if (p->cpuid_level >= C2_PROCESSOR_INTEL_CPUID4_OP) {
		cpuid_count(C2_PROCESSOR_INTEL_CPUID4_OP, cache_level,
			    &eax, &ebx, &ecx, &edx);
		level = processor_get_x86cache_level(eax);
		if (level == cache_level) {
			linesz = ebx & C2_PROCESSOR_INTEL_LINESZ_MASK;
			partition = (ebx >> C2_PROCESSOR_INTEL_PARTITION_SHIFT)
				    & C2_PROCESSOR_INTEL_PARTITION_MASK;
			asso = (ebx >> C2_PROCESSOR_INTEL_ASSOCIATIVITY_SHIFT)
				& C2_PROCESSOR_INTEL_ASSOCIATIVITY_MASK;
			sets = ecx;
			sz = (linesz+1) * (sets+1) * (partition+1) * (asso+1);
			use_defaults = false;
		}
	}

	if (use_defaults == true) {
		switch (cache_level) {
		case C2_PROCESSOR_L1_CACHE:
			sz = DEFAULT_L1_SZ;
			break;
		case C2_PROCESSOR_L2_CACHE:
			sz = DEFAULT_L2_SZ;
			break;
		default:
			break;
		}
	}

	return sz;
}

/**
   Fetch L1 or L2 cache size for a given x86 processor.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level for which information is requested.

   @return size of L1 or L2 cache (in bytes) for the given x86 processor.
 */
static uint32_t processor_get_x86_cache_sz(c2_processor_nr_t id,
					   uint32_t cache_level)
{
	uint32_t sz;
	struct cpuinfo_x86 *p;

	/*
	 * Get Linux kernel CPU data.
	 */
	p = &cpu_data(id);

	switch (p->x86_vendor) {
	case X86_VENDOR_AMD:
		/*
		 * Get L1/L2 cache size for AMD processors.
		 */
		sz = processor_get_amd_cache_sz(id, cache_level);
		break;
	case X86_VENDOR_INTEL:
		/*
		 * Get L1/L2 cache size for INTEL processors.
		 */
		sz = processor_get_intel_cache_sz(id, cache_level);
		break;
	default:
		/*
		 * Use default function for all other x86 vendors.
		 */
		sz = processor_get_cache_sz(id, cache_level);
		break;
	}/* end of switch - vendor name */

	return sz;
}

/**
   Fetch attributes for the x86 processor.

   @param arg -> argument passed to this function.

   @see processor_x86cache_create
   @see smp_call_function_single (Linux kernel)
 */
static void processor_x86_info(void *arg)
{
	uint32_t c_leaves;
	c2_processor_nr_t cpu;
	struct c2_processor_node *pinfo = (struct c2_processor_node *)arg;

	cpu = smp_processor_id();

	/*
	 * Fetch other generic properties.
	 */
	pinfo->pn_info.pd_id = cpu;
	pinfo->pn_info.pd_numa_node = processor_get_numanodeid(cpu);
	pinfo->pn_info.pd_pipeline = processor_get_pipelineid(cpu);

	c_leaves = processor_get_x86cache_leaves(cpu);
	/*
	 * Now fetch the x86 cache information.
	 */
	pinfo->pn_info.pd_l1 =
	    processor_get_x86_cacheid(cpu, C2_PROCESSOR_L1_CACHE, c_leaves);
	pinfo->pn_info.pd_l2 =
	    processor_get_x86_cacheid(cpu, C2_PROCESSOR_L2_CACHE, c_leaves);

	pinfo->pn_info.pd_l1_sz =
	    processor_get_x86_cache_sz(cpu, C2_PROCESSOR_L1_CACHE);
	pinfo->pn_info.pd_l2_sz =
	    processor_get_x86_cache_sz(cpu, C2_PROCESSOR_L2_CACHE);

}

/**
   Obtain information on the processor with a given id.
   @param id -> id of the processor for which information is requested.
   @param pd -> processor descripto structure. Memory for this should be
                allocated by the calling function. Interface does not allocate
                memory.

   @retval 0 if processor information is found
   @retval -EINVAL if processor information is not found

   @pre  Memory must be allocated for pd. Interface does not allocate memory.
   @pre c2_processors_init() must be called before calling this function.

   @see c2_processor_describe
   @see processor_get_info
 */
static int processor_find_x86info(c2_processor_nr_t id,
				  struct c2_processor_descr *pd)
{
	int rc = -EINVAL;
	struct c2_processor_node *pinfo;

	C2_PRE(pd != NULL);
	C2_PRE(processor_init);

	c2_list_for_each_entry(&x86_cpus, pinfo, struct c2_processor_node,
			       pn_link) {
		if (pinfo->pn_info.pd_id == id) {
			*pd = pinfo->pn_info;
			rc = 0;
			break;
		}/* if - matching CPU id found */

	}/* for - iterate over all the processor nodes */

	return rc;
}

/**
   Create cache for x86 processors. We have support for Intel and AMD.

   This is a blocking call.

   @retval 0 if cache is created.
   @retval -1 if cache is empty.

   @see c2_processors_init
   @see smp_call_function_single (Linux kernel)
 */
static int processor_x86cache_create(void)
{
	bool empty;
	uint32_t cpu;
	struct c2_processor_node *pinfo;

	c2_list_init(&x86_cpus);

	/*
	 * Using online CPU mask get details of each processor.
	 * Unless CPU is online, we cannot execute on it.
	 */
	for_each_online_cpu(cpu) {
		pinfo = (struct c2_processor_node *)
			kmalloc (sizeof(struct c2_processor_node), GFP_KERNEL);
		if (pinfo != NULL) {
			/*
			 * We may not be running on the same processor for
			 * which cache info is needed. Hence run the function
			 * on the requested processor. smp_call... has all the
			 * optimization necessary.
			 */
			smp_call_function_single(cpu,
						 processor_x86_info,
						 (void *)pinfo, true);
			c2_list_add(&x86_cpus, &pinfo->pn_link);
		}
	}/* for - scan all the online processors */

	empty = c2_list_is_empty(&x86_cpus);
	if (empty == true) {
		c2_list_fini(&x86_cpus);
		return -1;
	}

	return 0;
}

/**
   Cache clean-up

   @see c2_processors_fini
   @see c2_list_fini
 */
static void processor_x86cache_destroy(void)
{
	struct c2_list_link *node;
	struct c2_processor_node *pinfo;

	/*
	 * Remove all the processor nodes.
	 */
	node = x86_cpus.l_head;
	while((struct c2_list *)node != &x86_cpus) {
		pinfo = c2_list_entry(node, struct c2_processor_node, pn_link);
		c2_list_del(&pinfo->pn_link);
		kfree(pinfo);
		node = x86_cpus.l_head;
	}
	c2_list_fini(&x86_cpus);
}

#else /* if not X86_64 */

/**
   Fetch default L1 or L2 cache id for a given processor.
   Irrespective of cache level default cache id is same as processor id.
   That will uniquely identify cache.

   @param id -> id of the processor for which information is requested.
   @param cache_level -> cache level (L1 or L2) for which id is requested.
   @return id of L1/L2 cache for the given processor.
 */
static uint32_t processor_get_cacheid(c2_processor_nr_t id,
				      uint32_t cache_level)
{
	uint32_t cache_id = id;

	return cache_id;
}

/**
   Obtain information on the processor with a given id.
   @param id -> id of the processor for which information is requested.
   @param pd -> processor descripto structure. Memory for this should be
                allocated by the calling function. Interface does not allocate
                memory.

   @retval 0

   @pre  Memory must be allocated for pd. Interface does not allocate memory.
   @pre c2_processors_init() must be called before calling this function.

   @see c2_processor_describe
   @see processor_find_x86info
 */
static int processor_get_info(c2_processor_nr_t id,
			      struct c2_processor_descr *pd)
{
	C2_PRE(pd != NULL);
	C2_PRE(processor_init);

	pd->pd_id = id;
	pd->pd_numa_node = processor_get_numanodeid(id);
	pd->pd_pipeline = processor_get_pipelineid(id);

	pd->pd_l1 = processor_get_cacheid(id, C2_PROCESSOR_L1_CACHE);
	pd->pd_l2 = processor_get_cacheid(id, C2_PROCESSOR_L2_CACHE);
	pd->pd_l1_sz = processor_get_cache_sz(id, C2_PROCESSOR_L1_CACHE);
	pd->pd_l2_sz = processor_get_cache_sz(id, C2_PROCESSOR_L2_CACHE);

	return 0;
}

#endif /* CONFIG_X86_64 */

/* ---- Processor Interfaces ---- */

int c2_processors_init()
{
	int rc;

	C2_PRE(!processor_init);
	rc = processor_cache_create();
	if (rc == 0) {
		processor_init = true;
	}

	return rc;
}

void c2_processors_fini()
{
	C2_PRE(processor_init);
	processor_cache_destroy();
	processor_init = false;
	return;
}

c2_processor_nr_t c2_processor_nr_max(void)
{
	return NR_CPUS - 1;
}

void c2_processors_possible(struct c2_bitmap *map)
{
	processors_bitmap_copy(map, cpu_possible_mask, nr_cpu_ids);
}

void c2_processors_available(struct c2_bitmap *map)
{
	processors_bitmap_copy(map, cpu_present_mask, nr_cpu_ids);
}

void c2_processors_online(struct c2_bitmap *map)
{
	processors_bitmap_copy(map, cpu_online_mask, nr_cpu_ids);
}

int c2_processor_describe(c2_processor_nr_t id, struct c2_processor_descr *pd)
{
	if (id >= nr_cpu_ids || pd == NULL)
		return -EINVAL;

	return processor_info(id, pd);
}

c2_processor_nr_t c2_processor_getcpu(void)
{
	int cpu;

	cpu = smp_processor_id();
	return cpu;
}

/** @} end of processor group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
