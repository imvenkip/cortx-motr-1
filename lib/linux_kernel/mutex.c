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

#include <linux/version.h>   /* LINUX_VERSION_CODE */
#include <linux/module.h>
#include <asm/current.h>

#include "lib/mutex.h"
#include "lib/misc.h"  /* M0_EXPORTED */

/**
   @addtogroup mutex

   <b>Implementation of m0_mutex on top of Linux struct mutex.</b>

   @{
*/

M0_INTERNAL void m0_mutex_init(struct m0_mutex *mutex)
{
	mutex_init(&mutex->m_mutex);
}
M0_EXPORTED(m0_mutex_init);

M0_INTERNAL void m0_mutex_fini(struct m0_mutex *mutex)
{
	mutex_destroy(&mutex->m_mutex);
}
M0_EXPORTED(m0_mutex_fini);

M0_INTERNAL void m0_mutex_lock(struct m0_mutex *mutex)
{
	mutex_lock(&mutex->m_mutex);
}
M0_EXPORTED(m0_mutex_lock);

M0_INTERNAL int m0_mutex_trylock(struct m0_mutex *mutex)
{
	return mutex_trylock(&mutex->m_mutex);
}

M0_INTERNAL void m0_mutex_unlock(struct m0_mutex *mutex)
{
	mutex_unlock(&mutex->m_mutex);
}
M0_EXPORTED(m0_mutex_unlock);

M0_INTERNAL bool m0_mutex_is_locked(const struct m0_mutex *mutex)
{
	/* linux kernel mutex, 1:unlocked, 0:locked, -ve: locked with waiters */
#if defined(CONFIG_DEBUG_MUTEXES) || defined(CONFIG_SMP)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	struct mutex *m = (struct mutex*)(&mutex->m_mutex);
	return mutex_is_locked(m) && m->owner == current;
#else
	struct thread_info *owner = mutex->m_mutex.owner;
	return atomic_read(&mutex->m_mutex.count) < 1 &&
		owner != NULL && owner->task == current;
#endif
#else
	return true;
#endif
}
M0_EXPORTED(m0_mutex_is_locked);

M0_INTERNAL bool m0_mutex_is_not_locked(const struct m0_mutex *mutex)
{
#if defined(CONFIG_DEBUG_MUTEXES) || defined(CONFIG_SMP)
	return !m0_mutex_is_locked(mutex);
#else
	return true;
#endif
}
M0_EXPORTED(m0_mutex_is_not_locked);

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
