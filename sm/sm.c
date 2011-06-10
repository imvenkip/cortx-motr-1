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
 * Original creation date: 04/01/2010
 */

#include "sm.h"

/**
   @addtogroup sm
   @{
*/

int c2_sm_init(struct c2_sm *mach)
{
	return 0;
}

void c2_sm_fini(struct c2_sm *mach)
{
}

c2_sm_state_t c2_sm_state_get  (struct c2_sm *mach)
{
	return 0;
}

int c2_sm_state_set  (struct c2_sm *mach, c2_sm_state_t state)
{
	return 0;
}

int c2_sm_state_wait (struct c2_sm *mach)
{
	return 0;
}

int c2_sm_state_until(struct c2_sm *mach, c2_sm_state_t state)
{
	return 0;
}

int c2_sm_event_init(struct c2_sm_event *ev, struct c2_sm *mach)
{
	return 0;
}

void c2_sm_event_fini(struct c2_sm_event *ev)
{
}

enum c2_sm_res c2_sm_event_try(struct c2_sm_event *sm)
{
	return SR_DONE;
}

int c2_sm_event_wait (struct c2_sm_event *sm)
{
	return 0;
}

int c2_sm_event_apply(struct c2_sm_event *sm)
{
	return 0;
}


int c2_sm_event_queue(struct c2_sm_event *sm)
{
	return 0;
}

int c2_persistent_sm_register(struct c2_persistent_sm *pmach,
			      struct c2_dtm *dtm, 
			      const struct c2_persistent_sm_ops *ops)
{
	return 0;
}

void c2_persistent_sm_unregister(struct c2_persistent_sm *pmach)
{
}

/** @} end of sm group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
