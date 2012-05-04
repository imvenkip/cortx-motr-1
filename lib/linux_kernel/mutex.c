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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/05/2010
 */

#include <linux/module.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/mutex.h"
#include "lib/assert.h"

/**
   @addtogroup mutex

   <b>Implementation of c2_mutex on top of Linux struct mutex.</b>

   @{
*/

void c2_mutex_init(struct c2_mutex *mutex)
{
	mutex_init(&mutex->m_mutex);
}
C2_EXPORTED(c2_mutex_init);

void c2_mutex_fini(struct c2_mutex *mutex)
{
	mutex_destroy(&mutex->m_mutex);
}
C2_EXPORTED(c2_mutex_fini);

void c2_mutex_lock(struct c2_mutex *mutex)
{
	mutex_lock(&mutex->m_mutex);
}
C2_EXPORTED(c2_mutex_lock);

int c2_mutex_trylock(struct c2_mutex *mutex)
{
	return mutex_trylock(&mutex->m_mutex);
}

void c2_mutex_unlock(struct c2_mutex *mutex)
{
	mutex_unlock(&mutex->m_mutex);
}
C2_EXPORTED(c2_mutex_unlock);

bool c2_mutex_is_locked(const struct c2_mutex *mutex)
{
	return true;
}
C2_EXPORTED(c2_mutex_is_locked);

bool c2_mutex_is_not_locked(const struct c2_mutex *mutex)
{
	return true;
}
C2_EXPORTED(c2_mutex_is_not_locked);

/** @} end of mutex group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
