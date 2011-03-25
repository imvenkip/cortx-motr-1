/* -*- C -*- */

#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/topology.h>
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/processor.h"

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

/** Global variables */
static bool c2_processor_init = false;

/**
   Convert bitmap from one format to another. Copy cpumask bitmap to
   c2_bitmap.

   @param src -> Processors bitmap used by Linux kernel.
   @param dest -> Processors bitmap for Colibri programs.
   @param bmpsz -> Size of cpumask bitmap (src)

   @pre Assumes memory is alloacted for outbmp and it's initialized.

   @see lib/processor.h 
   @see lib/bitmap.h 

 */
static void c2_processors_copy_bitmap(const cpumask_t *src,
                                      struct c2_bitmap *dest,
                                      uint32_t bmpsz)
{
	uint32_t bit;
	bool     val;
	
	C2_ASSERT(dest->b_nr >= bmpsz);
	
	for (bit=0; bit < bmpsz; bit++) {
		val = cpumask_test_cpu(bit, src);
		c2_bitmap_set(dest, bit, val);
	}
}

/**
   Fetch NUMA node id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of the NUMA node to which the processor belongs.

   @note This function may become macro or inline function.

 */
static uint32_t c2_processor_get_numanodeid(c2_processor_nr_t id)
{
	return cpu_to_node(id);
}

/**
   Fetch L1 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L1 cache for the given processor.
 */
static uint32_t c2_processor_get_l1_cacheid(c2_processor_nr_t id)
{
	uint32_t l1_id = id;

	/*
	 * TODO : Write x86 asm code to figure out L1 info.
	 */
	return l1_id;
}


/**
   Fetch L2 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L2 cache for the given processor.
 */
static uint32_t c2_processor_get_l2_cacheid(c2_processor_nr_t id)
{
	uint32_t l2_id = id;

	/*
	 * TODO : Write x86 asm code to figure out L2 info.
	 */
	return l2_id;
}

/**
   Fetch L1 cache size for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval size of L1 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static size_t c2_processor_get_l1_size(c2_processor_nr_t id)
{
	/*
	 * TODO : Write x86 asm code to figure out L1 info.
	 */
	return DEFAULT_L1_SZ;
}


/**
   Fetch L2 cache size for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval size of L2 cache for the given processor.

   @pre Assumes the directory has been changed to approriate CPU
        info dir.
 */
static size_t c2_processor_get_l2_size(c2_processor_nr_t id)
{
	/*
	 * TODO : Write x86 asm code to figure out L2 info.
	 */
	return DEFAULT_L2_SZ;
}

/**
   Fetch pipeline id for a given processor.
   Curently pipeline id is same as processor id.

   @param id -> id of the processor for which information is requested.

   @retval id of pipeline for the given processor.

   @note This may become an inline function.
 */
static inline uint32_t c2_processor_get_pipelineid(c2_processor_nr_t id)
{
	return id;
}

/* ---- Processor Interfaces ----*/

/**
   Initialize processors interface.
   The kernel interface does not cache the data.

   The calling function should not assume hot-plug CPU facility.
   If the underlying OS supports the hot-plug CPU facility, the calling
   program will have to re-initialize the interface (at least in user-mode)
   after registering for platform specific CPU change notification.

   To re-initialize the interface, c2_processors_fini() must be called first,
   before initializing it again.

   @post Interface initialized.

   Concurrency: The interface should not be initialized twice or simultaneously.
                It's not MT-safe and can be called only once. It can be
                called again after calling c2_processors_fini().
 */
int c2_processors_init()
{
	c2_processor_init = true;
	return 0;
}

/**
   Close the processors interface. This function will destroy any cached data.
   After calling this interface no meaningful data should be assumed.

   Concurrency: Not MT-safe. Assumes no threads are using processor interface.
 */
void c2_processors_fini()
{
	c2_processor_init = false;
	return;
}

/**
   Query if processors interface is initialized.
   @retval true if the interface is initialized
   @retval false if the interface is not initialized.
 */
bool c2_processor_is_initialized()
{
	return c2_processor_init;
}

/**
   Maximum processors this kernel can handle.

 */
c2_processor_nr_t c2_processor_nr_max(void)
{
	return NR_CPUS - 1;
}

/**
   Return the bitmap of possible processors.

   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should allocated memory for 'map' and initialize
        it
   @note This function does not take any locks.
 */
void c2_processors_possible(struct c2_bitmap *map)
{
	c2_processors_copy_bitmap(cpu_possible_mask, map, nr_cpu_ids);
}

/**
   Return the bitmap of available processors.

   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should allocated memory for 'map' and initialize
        it
   @note This function does not take any locks.
 */
void c2_processors_available(struct c2_bitmap *map)
{
	c2_processors_copy_bitmap(cpu_present_mask, map, nr_cpu_ids);
}

/**
   Return the bitmap of online processors.


   @pre map->b_nr >= c2_processor_nr_max()
   @pre c2_processors_init() must be called before calling this function.
   @pre The calling function should allocated memory for 'map' and initialize
        it
   @note This function does not take any locks.
 */
void c2_processors_online(struct c2_bitmap *map)
{
	c2_processors_copy_bitmap(cpu_online_mask, map, nr_cpu_ids);
}

/**
   Obtain information on the processor with a given id.
   @param id -> id of the processor for which information is requested.
   @param pd -> processor descripto structure. Memory for this should be
                allocated by the calling function. Interface does not allocate
                memory.

   @retval 0 if a matching processor is found
   @retval -EINVAL if id does not match with any of the processors or NULL
                   memory pointer for 'pd' is passed.

   @pre  Memory must be allocated for pd. Interface donot allocated memory.
   @pre c2_processors_init() must be called before calling this function.
   @post d->pd_id == id or none

   Concurrency: This is read only data. Interface by itself does not do
                any locking. When used in kernel-mode, the interface may
                call some functions that may use some kind of locks.
 */
int c2_processor_describe(c2_processor_nr_t id,
			  struct c2_processor_descr *pd)
{

	if (id > nr_cpu_ids || pd == NULL) {
		return -EINVAL;
	}


	pd->pd_id = id;
	pd->pd_numa_node = c2_processor_get_numanodeid(id);
	pd->pd_l1 = c2_processor_get_l1_cacheid(id);
	pd->pd_l2 = c2_processor_get_l2_cacheid(id);
	pd->pd_l1_sz = c2_processor_get_l1_size(id);
	pd->pd_l2_sz = c2_processor_get_l2_size(id);
	pd->pd_pipeline = c2_processor_get_pipelineid(id);

	return 0;
}

/**
   Return the id of the processor on which the calling thread is running.

   @retval logical processor id (as supplied by the system) on which the
           calling thread is running.
 */
c2_processor_nr_t c2_processor_getcpu(void)
{
	int cpu;

	cpu = smp_processor_id();
	return cpu;
}

/** Export interfaces */
C2_EXPORTED(c2_processors_init);
C2_EXPORTED(c2_processors_fini);
C2_EXPORTED(c2_processor_is_initialized);
C2_EXPORTED(c2_processor_nr_max);
C2_EXPORTED(c2_processors_possible);
C2_EXPORTED(c2_processors_available);
C2_EXPORTED(c2_processors_online);
C2_EXPORTED(c2_processor_describe);
C2_EXPORTED(c2_processor_getcpu);


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
