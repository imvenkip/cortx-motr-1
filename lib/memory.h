/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov
 * Original creation date: 05/17/2010
 */

#ifndef _COLIBRI_LIB_MEMORY_H_
#define _COLIBRI_LIB_MEMORY_H_

#include "lib/types.h"
#include "lib/assert.h" /* C2_CASSERT */
#include "addb/addb.h"

/**
   @defgroup memory memory allocation handling functions
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
void *c2_alloc(size_t size);

#define C2_ALLOC_ARR(arr, nr)  ((arr) = c2_alloc((nr) * sizeof ((arr)[0])))
#define C2_ALLOC_PTR(ptr)      C2_ALLOC_ARR(ptr, 1)
#define C2_ALLOC_ADDB(ptr, size, ctx, loc) \
    if ((ptr = c2_alloc(size)) == NULL) C2_ADDB_ADD(ctx, loc, c2_addb_oom)
#define C2_ALLOC_PTR_ADDB(ptr, ctx, loc) \
    if (C2_ALLOC_PTR(ptr) == NULL) C2_ADDB_ADD(ctx, loc, c2_addb_oom)
#define C2_ALLOC_ARR_ADDB(arr, nr, ctx, loc)				\
    if (C2_ALLOC_ARR(arr, nr) == NULL) C2_ADDB_ADD(ctx, loc, c2_addb_oom)

/**
   Allocates zero-filled memory, aligned on (2^shift)-byte boundary.
 */
void *c2_alloc_aligned(size_t size, unsigned shift);

/**
 * Frees memory block
 *
 * This function must be a no-op when called with NULL argument.
 *
 * @param data pointer to allocated block
 *
 * @return none
 */
void c2_free(void *data);

/**
 * Return amount of memory currently allocated.
 */
size_t c2_allocated(void);

/** @} end of memory group */

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
