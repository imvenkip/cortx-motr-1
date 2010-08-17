#ifndef _COLIBRI_LIB_MEMORY_H_

#define _COLIBRI_LIB_MEMORY_H_

#include "lib/types.h"
#include "lib/assert.h" /* C2_CASSERT */

/**
   @defgroup memory memory allocation handling functions
   @{
*/

/**
 * Allocates zero-filled memory
 * @param size - memory size
 *
 * @retval NULL - allocation failed
 * @retval !NULL - allocated memory block
 */
void *c2_alloc(size_t size);

#define C2_ALLOC_ARR(arr, nr)  ((arr) = c2_alloc((nr) * sizeof ((arr)[0])))
#define C2_ALLOC_PTR(ptr)      C2_ALLOC_ARR(ptr, 1)

/**
 * freed memory block
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
