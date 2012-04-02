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
 * Original creation date: 05/13/2010
 */

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
