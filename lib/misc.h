/* -*- C -*- */
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/18/2010
 */

#ifndef __COLIBRI_LIB_MISC_H__
#define __COLIBRI_LIB_MISC_H__

#ifndef __KERNEL__
#include <string.h>               /* memset, ffs */
#else
#include <linux/string.h>         /* memset */
#include <linux/bitops.h>         /* ffs */
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

#define C2_IN(x, set) C2_IN0(x, C2_UNPACK set)
#define C2_UNPACK(...) __VA_ARGS__

#define C2_IN0(...) \
	C2_CAT(C2_IN_, C2_COUNT_PARAMS(__VA_ARGS__))(__VA_ARGS__)

#define C2_IN_1(x, v) ((x) == (v))
#define C2_IN_2(x, v, ...) ((x) == (v) || C2_IN_1(x, __VA_ARGS__))
#define C2_IN_3(x, v, ...) ((x) == (v) || C2_IN_2(x, __VA_ARGS__))
#define C2_IN_4(x, v, ...) ((x) == (v) || C2_IN_3(x, __VA_ARGS__))
#define C2_IN_5(x, v, ...) ((x) == (v) || C2_IN_4(x, __VA_ARGS__))
#define C2_IN_6(x, v, ...) ((x) == (v) || C2_IN_5(x, __VA_ARGS__))
#define C2_IN_7(x, v, ...) ((x) == (v) || C2_IN_6(x, __VA_ARGS__))
#define C2_IN_8(x, v, ...) ((x) == (v) || C2_IN_7(x, __VA_ARGS__))
#define C2_IN_9(x, v, ...) ((x) == (v) || C2_IN_8(x, __VA_ARGS__))

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
