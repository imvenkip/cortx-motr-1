/* -*- C -*- */

#include <linux/cpumask.h>
#include "lib/processor.h"

/**
   @addtogroup Processor

   This file includes additional data structures and functions for processing
   processors data - for kernel-mode programs.

   This file will also implement Linux kernel-mode processors interfaces.

   @see lib/processor.h

   @{
 */

/**
   Convert bitmap from one format to another. Copy cpumask bitmap to
   c2_bitmap.

   @param inpbamp -> Processors bitmap used by Linux kernel.
   @param outpbmap -> Processors bitmap for Colibri programs.
   @param bmpsz -> Size of cpumask bitmap (inbmp)

   @pre Assumes memory is alloacted for outbmp and it's initalized.

   @see lib/processor.h 
   @see lib/bitmap.h 
   @see void c2_bitmap_init()

 */
static void c2_processors_copy_bitmap(const cpumask_t *inpbmp,
                                      struct c2_bitmap *outpbamp,
                                      int bmpsz);

/**
   Fetch NUMA node id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of the NUMA node to which the processor belongs.

   @note This function may become macro or inline function.

 */
static uint32_t c2_processor_get_numanodeid(c2_processor_nr_t id);

/**
   Fetch L1 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L1 cache for the given processor.
 */
static uint32_t c2_processor_get_l1_cacheid(c2_processor_nr_t id);

/**
   Fetch L2 cache id for a given processor.

   @param id -> id of the processor for which information is requested.

   @retval id of L2 cache for the given processor.
 */
static uint32_t c2_processor_get_l2_cacheid(c2_processor_nr_t id);

/**
   Fetch pipeline id for a given processor.
   Curently pipeline id is same as processor id.

   @param id -> id of the processor for which information is requested.

   @retval id of pipeline for the given processor.

   @note This may become an inline function.
 */
static uint32_t c2_processor_get_pipelineid(c2_processor_nr_t id);

/** @} end of processor group */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
