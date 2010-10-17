/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "desim/sim.h"

typedef unsigned long long sector_t;

struct storage_dev;

struct storage_conf {
	unsigned   sc_sector_size;
};

enum storage_req_type {
	SRT_READ,
	SRT_WRITE
};

typedef void (*storage_end_io_t)(struct storage_dev *dev);
typedef void (*storage_submit_t)(struct storage_dev *dev,
				 enum storage_req_type type,
				 sector_t sector, unsigned long count);

struct elevator;
struct storage_dev {
	struct sim          *sd_sim;
	struct storage_conf *sd_conf;
	storage_end_io_t     sd_end_io;
	storage_submit_t     sd_submit;
	struct elevator     *sd_el;
};

#endif /* STORAGE_H */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
