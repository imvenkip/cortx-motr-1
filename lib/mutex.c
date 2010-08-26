/* -*- C -*- */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"
#include "lib/assert.h"

/**
   @addtogroup mutex

   Implementation of c2_mutex on top of pthread_mutex_t.

   @{
*/

static void mutex_owner_reset(struct c2_mutex *mutex)
{
	C2_SET0(&mutex->m_owner);
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

int c2_mutex_trylock(struct c2_mutex *mutex)
{
	pthread_t self;
	int ret;

	self = pthread_self();
	C2_PRE(!pthread_equal(mutex->m_owner, self));
	ret = pthread_mutex_trylock(&mutex->m_impl);
	if (ret == 0)
		memcpy(&mutex->m_owner, &self, sizeof self);

	return ret;
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
