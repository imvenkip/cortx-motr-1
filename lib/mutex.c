/* -*- C -*- */

#include <string.h> /* memset */

#include "mutex.h"
#include "assert.h"

/**
   @addtogroup mutex
   @{
*/

static void mutex_owner_reset(struct c2_mutex *mutex)
{
	memset(&mutex->m_owner, 0, sizeof mutex->m_owner);
}

void c2_mutex_init(struct c2_mutex *mutex)
{
	pthread_mutex_init(&mutex->m_impl, NULL);
	mutex_owner_reset(mutex);
}

void c2_mutex_fini(struct c2_mutex *mutex)
{
	pthread_mutex_destroy(&mutex->m_impl);
}

void c2_mutex_lock(struct c2_mutex *mutex)
{
	pthread_t self;

	self = pthread_self();
	C2_PRE(!pthread_equal(mutex->m_owner, self));
	pthread_mutex_lock(&mutex->m_impl);
	memcpy(&mutex->m_owner, &self, sizeof self);
}

void c2_mutex_unlock(struct c2_mutex *mutex)
{
	pthread_t self;

	self = pthread_self();
	C2_PRE(pthread_equal(mutex->m_owner, self));
	mutex_owner_reset(mutex);
	pthread_mutex_unlock(&mutex->m_impl);
}

bool c2_mutex_is_locked(const struct c2_mutex *mutex)
{
	return pthread_equal(mutex->m_owner, pthread_self());
}

bool c2_mutex_is_not_locked(const struct c2_mutex *mutex)
{
	return !pthread_equal(mutex->m_owner, pthread_self());
}

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
