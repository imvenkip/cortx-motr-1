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
 * Original creation date: 04/28/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "sns/repair.h"

static const struct c2_persistent_sm_ops;

/**
   Register pool state machine as a persistent state machine with the given
   transaction manager instance. This allows poolmachine to update its persisten
   state transactionally and to be call-back on node restart.
 */
int c2_poolmach_init(struct c2_poolmach *pm, struct c2_dtm *dtm)
{
	c2_persistent_sm_register(&pm->pm_mach, dtm,
				  &poolmach_persistent_sm_ops);
}

void c2_poolmach_fini(struct c2_poolmach *pm)
{
	c2_persistent_sm_unregister(&pm->pm_mach);
}

int  c2_poolmach_device_join (struct c2_poolmach *pm, struct c2_pooldev *dev)
{
}

int  c2_poolmach_device_leave(struct c2_poolmach *pm, struct c2_pooldev *dev)
{
}

int  c2_poolmach_node_join (struct c2_poolmach *pm, struct c2_poolnode *node)
{
}

int  c2_poolmach_node_leave(struct c2_poolmach *pm, struct c2_poolnode *node)
{
}

/**
   Pool machine recovery call-back.

   This function is installed as c2_persistent_sm_ops::pso_recover method of a
   persistent state machine. It is called when a node hosting a pool machine
   replica reboots and starts local recovery.
 */
static int poolmach_recover(struct c2_persistent_sm *pmach)
{
	struct c2_poolmach *pm;

	pm = container_of(pmach, struct c2_poolmach, pm_mach);
}

static const struct c2_persistent_sm_ops poolmach_persistent_sm_ops = {
	.pso_recover = poolmach_recover
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
