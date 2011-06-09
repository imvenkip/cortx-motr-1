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
 * Original creation date: 02/18/2011
 */

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
