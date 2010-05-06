#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>

#include "repair.h"

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
