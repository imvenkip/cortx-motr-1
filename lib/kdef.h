/* -*- C -*- */

#ifndef __COLIBRI_FOP_KDEF_H__
#define __COLIBRI_FOP_KDEF_H__

#include "lib/cdefs.h"
#include <linux/slab.h>
#include <linux/module.h>

static inline void *c2_alloc(size_t size)
{
	void *result;

	result = kmalloc(size, GFP_KERNEL);
	if (result != NULL)
		memset(result, 0, size);
	return result;
}

#define C2_ALLOC_ARR(arr, nr)  ((arr) = c2_alloc((nr) * sizeof ((arr)[0])))
#define C2_ALLOC_PTR(ptr)      C2_ALLOC_ARR(ptr, 1)

static inline void c2_free(void *data)
{
	kfree(data);
}

#define C2_ASSERT(cond) BUG_ON(!(cond))
#define C2_PRE(cond) C2_ASSERT(cond)
#define C2_POST(cond) C2_ASSERT(cond)
#define C2_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)
#define C2_BASSERT(cond) extern char __build_assertion_failure[!!(cond) - 1]
#define C2_IMPOSSIBLE(msg) C2_ASSERT(msg == NULL)

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_H__ */
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
