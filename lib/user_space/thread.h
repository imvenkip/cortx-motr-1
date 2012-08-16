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
 * Original creation date: 02/18/2011
 */

#pragma once

#ifndef __COLIBRI_LIB_USER_SPACE_THREAD_H__
#define __COLIBRI_LIB_USER_SPACE_THREAD_H__

#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

/**
   @addtogroup thread Thread

   <b>User space c2_thread implementation</b>

   User space implementation is straight-forwardly based on POSIX thread
   interface.

   @see c2_thread

   @{
 */

struct c2_thread_handle {
	pthread_t h_id;
};

enum {
	C2_THREAD_NAME_LEN = 16
};

int  c2_threads_init(void);
void c2_threads_fini(void);

/**
   Helper macro creating an anonymous function with a given body.

   For example:

   @code
   int x;

   result = C2_THREAD_INIT(&tcb, int, NULL,
                           LAMBDA(void, (int y) { printf("%i", x + y); } ), 1,
                           "add_%d", 1);
   @endcode

   LAMBDA is useful to create an "ad-hoc" function that can be passed as a
   call-back function pointer.

   @note resulting anonymous function can be called only while the block where
   LAMBDA macro was called is active. For example, in the code fragment above,
   the tcb thread must finish before the block where C2_THREAD_INIT() was called
   is left.

   @note Be careful if using LAMBDA in kernel code, as the code could be
   generated on the stack and would fault in the kernel as it is execution
   protected in the kernel.  If someone figures out the secret allocation
   sauce, update this note with the recipe; one observed problem was when a
   reference was made to an automatic variable from a lambda function, and that
   problem went away when the variable was made global.  Other lambda functions
   that simply returned values caused no problems in the kernel.

   @see http://en.wikipedia.org/wiki/Lambda_calculus
 */
#define LAMBDA(T, ...) ({ T __lambda __VA_ARGS__; &__lambda; })

/** @} end of thread group */

/* __COLIBRI_LIB_USER_SPACE_THREAD_H__ */
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
