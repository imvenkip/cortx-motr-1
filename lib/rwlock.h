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
 * Original creation date: 04/01/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_RWLOCK_H__
#define __COLIBRI_LIB_RWLOCK_H__


/**
   @defgroup rwlock Read-write lock
   @{
 */

#ifndef __KERNEL__
#include "lib/user_space/rwlock.h"
#else
#include "lib/linux_kernel/rwlock.h"
#endif

/**
   read-write lock constructor
 */
C2_INTERNAL void c2_rwlock_init(struct c2_rwlock *lock);

/**
   read-write lock destructor
 */
C2_INTERNAL void c2_rwlock_fini(struct c2_rwlock *lock);

/**
   take exclusive lock
 */
C2_INTERNAL void c2_rwlock_write_lock(struct c2_rwlock *lock);
/**
   release exclusive lock
 */
C2_INTERNAL void c2_rwlock_write_unlock(struct c2_rwlock *lock);

/**
   take shared lock
 */
C2_INTERNAL void c2_rwlock_read_lock(struct c2_rwlock *lock);
/**
   release shared lock
 */
C2_INTERNAL void c2_rwlock_read_unlock(struct c2_rwlock *lock);


/* bool c2_rwlock_write_trylock(struct c2_rwlock *lock); */
/* bool c2_rwlock_read_trylock(struct c2_rwlock *lock); */

/** @} end of rwlock group */

/* __COLIBRI_LIB_RWLOCK_H__ */
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
