/*
 * Copyright 2010 ClusterStor.
 *
 * Nikita Danilov.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib/assert.h"
#include "desim/elevator.h"

/*
 * Simple FIFO elevator.
 */

static void elevator_submit(struct elevator *el,
			    enum storage_req_type type,
			    sector_t sector, unsigned long count)
{
	el->e_idle = 0;
	el->e_dev->sd_submit(el->e_dev, type, sector, count);
}

/*
 * submit asynchronous requests.
 */
static void elevator_go(struct elevator *el)
{
	/* impossible for now */
	C2_IMPOSSIBLE("Elevator is not yet implemented");
}

void el_end_io(struct storage_dev *dev)
{
	struct elevator *el;

	el = dev->sd_el;
	el->e_idle = 1;
	if (!c2_list_is_empty(&el->e_queue))
		elevator_go(el);
	sim_chan_broadcast(&el->e_wait);
}

void elevator_init(struct elevator *el, struct storage_dev *dev)
{
	el->e_dev  = dev;
	el->e_idle = 1;
	dev->sd_end_io = el_end_io;
	dev->sd_el     = el;
	c2_list_init(&el->e_queue);
	sim_chan_init(&el->e_wait, "xfer-queue@%p", dev);
}

void elevator_fini(struct elevator *el)
{
	c2_list_fini(&el->e_queue);
	sim_chan_fini(&el->e_wait);
}

void elevator_io(struct elevator *el, enum storage_req_type type,
		 sector_t sector, unsigned long count)
{
	while (!el->e_idle)
		sim_chan_wait(&el->e_wait, sim_thread_current());
	elevator_submit(el, type, sector, count);
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
