/* -*- C -*- */

#ifndef __COLIBRI_LIB_PROCESSOR_H__
#define __COLIBRI_LIB_PROCESSOR_H__

#include "lib/bitmap.h"
#include "lib/types.h"

/**
   @defgroup processor Processor

   Interfaces to learn the number and characteristics of "processors".

   @{
 */

typedef uint32_t c2_processor_nr_t;

/**
   Maximal possible processor identifier (the size of the possible processors
   bitmap).

   @see c2_processors_possible()
 */
c2_processor_nr_t c2_processor_nr_max(void);

/**
   Return the bitmap of possible processors.

   @pre map->b_nr >= c2_processor_nr_max()
 */
void c2_processors_possible(struct c2_bitmap *map);

/**
   Return the bitmap of available processors.

   @pre map->b_nr >= c2_processor_nr_max()
 */
void c2_processors_available(struct c2_bitmap *map);

/**
   Return the bitmap of online processors.

   @pre map->b_nr >= c2_processor_nr_max()
 */
void c2_processors_online(struct c2_bitmap *map);

/**
   Description of a processor in the system.
 */
struct c2_processor_descr {
	/** processor identifier. */
	c2_processor_nr_t pd_id;
	/** all processors in the same numa node share this */
	uint32_t          pd_numa_node;
	/** all processors sharing the same l1 cache have the same value of
	    this. */
	uint32_t          pd_l1;
	/** all processors sharing the same l2 cache have the same value of
	    this. */
	uint32_t          pd_l2;
	/** all processors sharing the same pipeline have the same value of
	    this. */
	uint32_t          pd_pipeline;
};

/**
   Describe a processor.

   @post d->pd_id == id
 */
void c2_processor_describe(c2_processor_nr_t id, struct c2_processor_descr *d);

/** @} end of processor group */

/* __COLIBRI_LIB_PROCESSOR_H__ */
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
