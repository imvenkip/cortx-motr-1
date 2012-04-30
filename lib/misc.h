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

#define c2_forall(var, nr, ...)						\
({									\
	unsigned __nr = (nr);						\
	unsigned var;							\
									\
	for (var = 0; var < __nr && ({ true; __VA_ARGS__ }); ++var)	\
		;							\
	var == __nr;							\
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
