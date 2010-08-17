/* -*- C -*- */

#ifndef __COLIBRI_LIB_MISC_H__
#define __COLIBRI_LIB_MISC_H__

#ifndef __KERNEL__
#include <string.h>               /* memset */
#else
#include <linux/string.h>         /* memset */
#endif

#include "lib/assert.h"           /* C2_CASSERT */
#include "lib/cdefs.h"            /* c2_is_array */

#define C2_SET0(obj)				\
({						\
	C2_CASSERT(!c2_is_array(obj));		\
	memset((obj), 0, sizeof *(obj));	\
})

#define C2_SET_ARR0(arr)			\
({						\
	C2_CASSERT(c2_is_array(arr));		\
	memset((arr), 0, sizeof (arr));		\
})

/* __COLIBRI_LIB_MISC_H__ */
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
