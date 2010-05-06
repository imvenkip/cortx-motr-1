#ifndef _COLIBRI_LIB_MEMORY_H_

#define _COLIBRI_LIB_MEMORY_H_

#include <sys/types.h>

/**
   @defgroup memory memory allocation handling functions
   @{
*/

/**
 * allocate memory
 * @param size - memory size
 *
 * @retval NULL - allocation failed
 * @retval !NULL - allocated memory block
 */
void *c2_alloc(size_t size);

#define C2_ALLOC_PTR(ptr)	(ptr) = c2_alloc(sizeof *(ptr))

/**
 * freed memory block
 *
 * @param data pointer to allocated block
 * @param size block size
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
