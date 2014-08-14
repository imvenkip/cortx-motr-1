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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/17/2010
 */

#pragma once

#ifndef __MERO_LIB_MEMORY_H__
#define __MERO_LIB_MEMORY_H__

#include "lib/types.h"
#include "lib/assert.h" /* M0_CASSERT */
#include "addb/addb.h"

/**
   @defgroup memory Memory allocation handling functions
   @{
*/

/**
 * Allocates zero-filled memory.
 * The memory allocated is guaranteed to be suitably aligned
 * for any kind of variable.
 * @param size - memory size
 *
 * @retval NULL - allocation failed
 * @retval !NULL - allocated memory block
 */
void *m0_alloc(size_t size);

#define M0_ALLOC_ARR(arr, nr)  ((arr) = m0_alloc((nr) * sizeof ((arr)[0])))
#define M0_ALLOC_PTR(ptr)      M0_ALLOC_ARR(ptr, 1)

/**
   Macro to allocate a specified size of memory and assign it to a pointer.
   On failure, a standard ADDB record is posted.
   The pointer value should be checked for NULL by the invoker.
   @param ptr  Pointer to assign.
   @param size Size of memory to be allocated.
   @param mc   Pointer of an ADDB machine to use for posting.  The global ADDB
   machine ::m0_addb_gmc can be used for this purpose.
   @param loc  A context relative numeric location identifier.
   @param ...  One or more ADDB context pointers.  It is recommended that
   the first pointer be that of the module's static context.
   @todo rename me
 */
#define M0_ALLOC_ADDB(ptr, size, mc, loc, ...)				       \
do {									       \
	if ((ptr = m0_alloc(size)) == NULL) M0_ADDB_OOM(mc, loc, __VA_ARGS__); \
} while (0)

/**
   Macro to allocate memory and assign it to a pointer.  The size of memory
   to allocate is determined from the pointer data type.
   On failure, a standard ADDB record is posted.
   The pointer value should be checked for NULL by the invoker.
   @param ptr  Pointer to assign.
   @param mc   Pointer of an ADDB machine to use for posting.  The global ADDB
   machine ::m0_addb_gmc can be used for this purpose.
   @param loc  A context relative numeric location identifier.
   @param ...  One or more ADDB context pointers.  It is recommended that
   the first pointer be that of the module's static context.
   @todo rename me
 */
#define M0_ALLOC_PTR_ADDB(ptr, mc, loc, ...)				  \
do {									  \
	if (M0_ALLOC_PTR(ptr) == NULL) M0_ADDB_OOM(mc, loc, __VA_ARGS__); \
} while (0)

/**
   Macro to allocate memory for an array.
   On failure, a standard ADDB record is posted.
   The pointer value should be checked for NULL by the invoker.
   @param arr  Array pointer.
   @param nr   Number of elements to allocate.
   @param mc   Pointer of an ADDB machine to use for posting.  The global ADDB
   machine ::m0_addb_gmc can be used for this purpose.
   @param loc  A context relative numeric location identifier.
   @param ...  One or more ADDB context pointers.  It is recommended that
   the first pointer be that of the module's static context.
   @todo rename me
 */
#define M0_ALLOC_ARR_ADDB(arr, nr, mc, loc, ...)			      \
do {									      \
	if (M0_ALLOC_ARR(arr, nr) == NULL) M0_ADDB_OOM(mc, loc, __VA_ARGS__); \
} while (0)

/**
   Allocates zero-filled memory, aligned on (2^shift)-byte boundary.
   In kernel mode due to the usage of __GFP_ZERO, it can't be used from hard or
   soft interrupt context.
 */
M0_INTERNAL void *m0_alloc_aligned(size_t size, unsigned shift);

/** It returns true when addr is aligned by value shift. */
static inline bool m0_addr_is_aligned(void *addr, unsigned shift)
{
	M0_CASSERT(sizeof(unsigned long) >= sizeof(void *));
	return ((((unsigned long)addr >> shift) << shift) ==
		  (unsigned long)addr);
}

/**
 * Frees memory block
 *
 * This function must be a no-op when called with NULL argument.
 *
 * @param data pointer to allocated block
 */
void m0_free(void *data);

/** Frees memory and unsets the pointer. */
#define m0_free0(pptr)                        \
	do {                                  \
		typeof(pptr) __pptr = (pptr); \
		m0_free(*__pptr);             \
		*__pptr = NULL;               \
	} while (0)

/**
 * Frees aligned memory block
 * This function must be a no-op when called with NULL argument.
 * @param data pointer to allocated block
 *
 */
M0_INTERNAL void m0_free_aligned(void *data, size_t size, unsigned shift);

/**
 * Return amount of memory currently allocated.
 */
M0_INTERNAL size_t m0_allocated(void);

/**
 * Returns cumulative amount of memory allocated so far since libmero library loading.
 */
M0_INTERNAL size_t m0_allocated_total(void);

/**
 * Returns cumulative amount of memory freed so far since libmero library loading.
 */
M0_INTERNAL size_t m0_freed_total(void);

/**
 * Same as system getpagesize(3).
 * Used in the code shared between user and kernel.
 */
M0_INTERNAL int m0_pagesize_get(void);

/** @} end of memory group */
#endif /* __MERO_LIB_MEMORY_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
