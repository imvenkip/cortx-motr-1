/*
 * Copyright 2010 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifndef ELEVATOR_H
#define ELEVATOR_H

#include "desim/sim.h"
#include "desim/storage.h"
#include "lib/list.h"

/**
   @addtogroup desim desim
   @{
 */

struct elevator {
	struct storage_dev *e_dev;
	int                 e_idle;
	struct c2_list      e_queue;
	struct sim_chan     e_wait;
};

void elevator_init(struct elevator *el, struct storage_dev *dev);
void elevator_fini(struct elevator *el);

void elevator_io(struct elevator *el, enum storage_req_type type,
		 sector_t sector, unsigned long count);

#endif /* ELEVATOR_H */

/** @} end of desim group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
