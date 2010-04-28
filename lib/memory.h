#ifndef _COLIBRI_LIB_MEMORY_H_

#define _COLIBRI_LIB_MEMORY_H_

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
void c2_free(void *data, size_t size);

#define C2_FREE_PTR(ptr)	c2_free((ptr), sizeof *ptr)

/** @} end of memory group */

#endif
