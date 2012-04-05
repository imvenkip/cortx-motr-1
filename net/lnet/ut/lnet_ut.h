/* -*- C -*- */
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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 04/3/2012
 */

#ifndef __COLIBRI_LNET_UT_H__
#define __COLIBRI_LNET_UT_H__

enum {
	UT_TEST_NONE = 0,	  /**< no test requested, user program idles */
	UT_TEST_DEV  = 1,	  /**< device registered */
	UT_TEST_OPEN = 2,	  /**< open/close */
	UT_TEST_TM   = 3,	  /**< TM start/stop */
	UT_TEST_MAX  = 2,	  /**< final implemented test ID */

	UT_TEST_DONE = 127,	  /**< done testing, no user response */

	UT_USER_READY = 'r',	  /**< user program is ready */
	UT_USER_SUCCESS = 'y',	  /**< current test succeeded in user space */
	UT_USER_FAIL = 'n',	  /**< current test failed in user space */
};

#endif /* __COLIBRI_LNET_UT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
