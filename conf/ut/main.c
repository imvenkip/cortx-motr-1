/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Aug-2012
 */

#include "lib/ut.h"
#include "conf/conf_xcode.h"

extern void test_obj_fill(void);
extern void test_obj_find(void);
extern void test_obj_xtors(void);
extern void test_reg(void);
extern void test_confx_xcode(void);

const struct c2_test_suite conf_ut = {
	.ts_name  = "conf-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "reg",        test_reg         },
		{ "obj-xtors",  test_obj_xtors   },
		{ "obj-find",   test_obj_find    },
		{ "obj-fill",   test_obj_fill    },
		{ "xobj-xcode", test_confx_xcode },
		{ NULL        , NULL             }
	}
};
