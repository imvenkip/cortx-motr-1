/* -*- C -*- */

#include <linux/module.h>

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
EXPORT_SYMBOL(c2_mutex_init);

void c2_mutex_fini(struct c2_mutex *mutex)
{
	mutex_destroy(&mutex->m_mutex);
}
EXPORT_SYMBOL(c2_mutex_fini);

void c2_mutex_lock(struct c2_mutex *mutex)
{
	mutex_lock(&mutex->m_mutex);
}
EXPORT_SYMBOL(c2_mutex_lock);

void c2_mutex_unlock(struct c2_mutex *mutex)
{
	mutex_unlock(&mutex->m_mutex);
}
EXPORT_SYMBOL(c2_mutex_unlock);

bool c2_mutex_is_locked(const struct c2_mutex *mutex)
{
	return true;
}
EXPORT_SYMBOL(c2_mutex_is_locked);

bool c2_mutex_is_not_locked(const struct c2_mutex *mutex)
{
	return true;
}
EXPORT_SYMBOL(c2_mutex_is_not_locked);

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
