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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/04/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_USER_SPACE_ASSERT_H__
#define __COLIBRI_LIB_USER_SPACE_ASSERT_H__

/**
   @addtogroup assert

   <b>User space assertions based on c2_panic() function.</b>

   @{
*/

C2_INTERNAL void
c2_panic(const char *expr, const char *func, const char *file, int lineno)
	__attribute__((noreturn));

/**
   A macro to assert that a condition is true. If condition is true, C2_ASSERT()
   does nothing. Otherwise it emits a diagnostics message and terminates the
   system. The message and the termination method are platform dependent.
 */
#define C2_ASSERT(cond) \
        ((cond) ? (void)0 : c2_panic(#cond, __func__, __FILE__, __LINE__))


/** @} end of assert group */

/* __COLIBRI_LIB_USER_SPACE_ASSERT_H__ */
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
