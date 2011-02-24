/* -*- C -*- */

#ifndef __COLIBRI_LIB_PROCESSOR_U_INTERNAL_H__
#define __COLIBRI_LIB_PROCESSOR_U_INTERNAL_H__

#include "lib/processor.h"

/**
   @addtogroup Processor

   This file includes additional data structures and function for caching
   processors data - for user-mode programs.

   This file will also implement Linux user-mode processors interfaces.

   @section TestCases

   @subsection Configuration
   Get sysfs file system data for following configurations:
      - Single CPU
      - SMP
      - SMP with multicore CPUs
      - NUMA
      - Configurations with Intel, AMD processors
      - Developer VMs
   @subsection Procedure
   - Point the directory for sysfs file system data for the above
     configurations using environment variable 'C2_PROCESSORS_INFO_DIR'.
   - Create expected reults file.
   -  Run unit test and compare the processor info structure with expected
      results.
   @{
 */

/**
   Parse "sysfs" (/sys/devices/system) directory to fetch the summary of 
   processors on this system.

   To facilitate testing, this function will fetch the directory string from
   environment variable C2_PROCESSORS_INFO_DIR. This environment variable
   should be used only for unit testing.
   Under normal operation, a default value of "sysfs" directory is used.

   @pre  C2_PROCESSORS_INFO_DIR/default directory must exist.
   @post A global variable of type c2_processor_sys_summary will be filled in

   @see lib/processor.h 
   @see void c2_processors_init()

 */
static void c2_processors_getsummary();

/**
   Collect all the information needed to describe a single processor.
   This function will scan,parse directories and files under "sysfs".
   This data is cached.

   This function will be called from c2_processors_getsummary().

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.
   @param pn -> A linked list node containing processor information.

   @pre Memory to 'pn' must be allocated by the calling function
   @post pn structure will be filled with processor information

   @see c2_processors_getsummary()
 */
static void c2_processor_getinfo(char *sysfs_dir, c2_processor_nr_t id,
                          struct c2_processor_node *pn);

/**
   Fetch NUMA node id for a given processor.

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.

   @retval id of the NUMA node to which the processor belongs.
 */
static uint32_t c2_processor_get_numanodeid(char *sysfs_dir,
                                            c2_processor_nr_t id);

/**
   Fetch L1 cache id for a given processor.

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.

   @retval id of L1 cache for the given processor.
 */
static uint32_t c2_processor_get_l1_cacheid(char *sysfs_dir,
                                            c2_processor_nr_t id);

/**
   Fetch L2 cache id for a given processor.

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.

   @retval id of L2 cache for the given processor.
 */
static uint32_t c2_processor_get_l2_cacheid(char *sysfs_dir,
                                            c2_processor_nr_t id);

/**
   Fetch pipeline id for a given processor.
   Curently pipeline id is same as processor id.

   @param sysfs_dir -> Directory underwhich the information should be searched
   @param id -> id of the processor for which information is requested.

   @retval id of pipeline for the given processor.

   @note This may become macro or inline function.
 */
static uint32_t c2_processor_get_pipelineid(char *sysfs_dir,
                                            c2_processor_nr_t id);

/**
   System wide summary of all the processors. This is the head node.
   It contains various processor statistics and a linked list of
   processor info (struct c2_processor_descr).

   @see lib/processor.h
 */
struct c2_processor_sys_summary {
	/** Head of the list for processor info */
	struct c2_list	pss_head;

	/** Number of possible processors that can be attached to this OS.
          This means the number of processors statically configured into
          this OS. It could be less than or equal to absolute maximum
          processors that OS can handle.  */
	c2_processor_nr_t pss_max;

	/** Pointer to a bitmap of possible processors on this node */
	struct c2_bitmap *pss_poss_map;

	/** Pointer to a bitmap of processors that are present on this node */
	struct c2_bitmap *pss_avail_map;

	/** Pointer to a bitmap of online processors on this node */
	struct c2_bitmap *pss_online_map;

};

/**
   A node in the linked list describing processor properties. It
   encapsulates 'struct c2_processor_descr'.
   @see lib/processor.h
 */
struct c2_processor_node {
	/** Linking structure for node */
	struct c2_list	pn_link;

	/** Processor descritor strcture */
	struct c2_processor_descr pn_info;
};

/** @} end of processor group */

/* __COLIBRI_LIB_PROCESSOR_U_INTERNAL_H__ */
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
