/*
 * Copyright 2010 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifndef CHS_H
#define CHS_H

/**
   @addtogroup desim desim
   @{
 */

/*
 * CHS: Cylinder-head-sector rotational storage.
 */
#include "desim/sim.h"
#include "desim/storage.h"

struct chs_dev;

struct chs_conf {
	struct storage_conf cc_storage;

	unsigned   cc_heads;
	unsigned   cc_cylinders;
	unsigned   cc_track_skew;
	unsigned   cc_cylinder_skew;
	unsigned   cc_sectors_min;
	unsigned   cc_sectors_max;
	unsigned   cc_cyl_in_zone;

	sim_time_t cc_seek_avg;
	sim_time_t cc_seek_track_to_track;
	sim_time_t cc_seek_full_stroke;
	sim_time_t cc_write_settle;
	sim_time_t cc_head_switch;
	sim_time_t cc_command_latency;

	unsigned   cc_rps; /* revolutions per second */

	long long  cc_alpha;
	long long  cc_beta;

	struct {
		sector_t track_sectors;
		sector_t cyl_sectors;
		sector_t cyl_first;
	} *cc_zone;
};

enum chs_dev_state {
	CDS_XFER,
	CDS_IDLE
};

struct chs_dev {
	struct storage_dev  cd_storage;
	struct chs_conf    *cd_conf;
	enum chs_dev_state  cd_state;
	unsigned            cd_head;
	unsigned            cd_cylinder;
	struct sim_chan     cd_wait;
	struct sim_callout  cd_todo;

	struct cnt          cd_seek_time;
	struct cnt          cd_rotation_time;
	struct cnt          cd_xfer_time;
	struct cnt          cd_read_size;
	struct cnt          cd_write_size;
};

void chs_conf_init(struct chs_conf *conf);
void chs_conf_fini(struct chs_conf *conf);

void chs_dev_init(struct chs_dev *dev, struct sim *sim, struct chs_conf *conf);
void chs_dev_fini(struct chs_dev *dev);

#endif /* CHS_H */

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
