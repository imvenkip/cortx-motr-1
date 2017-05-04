/* -*- C -*- */
/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Ivan Alekhin <ivan.alekhin@seagate.com>
 * Original creation date: 30-May-2017
 */

#pragma once

#ifndef __MERO_CLOVIS_M0CRATE_CRATE_CLOVIS_H__
#define __MERO_CLOVIS_M0CRATE_CRATE_CLOVIS_H__

/**
 * @defgroup crate_clovis
 *
 * @{
 */

#include "fid/fid.h"
#include "clovis/clovis.h"
#include "clovis/m0crate/workload.h"
#include "clovis/m0crate/crate_utils.h"

struct crate_clovis_conf {
        /* Clovis parameters */
        bool is_oostrore;
        bool is_read_verify;
        char *clovis_local_addr;
        char *clovis_ha_addr;
        char *clovis_confd_addr;
        char *clovis_prof;
        char *clovis_process_fid;
        int layout_id;
        unsigned long clovis_block_size;
        char *clovis_index_dir;
        int index_service_id;
        char *cass_cluster_ep;
        char *cass_keyspace;
        int col_family;
};

enum clovis_operation_type {
	INDEX,
	IO
};

enum clovis_operation_status {
	OP_FINISHED = 1,
	OP_FAILED
};


struct clovis_workload_io {
	char                  *src_filename;
	unsigned               layout_id;
	unsigned               pool_id;
	unsigned               iosize;
	int		       mode;
	int		       opcode;
	int                    integrity;
	int                    num_objs;
	struct m0_uint128     *ids;
};

enum cr_opcode {
	CRATE_OP_PUT,
	CRATE_OP_GET,
	CRATE_OP_NEXT,
	CRATE_OP_DEL,
	CRATE_OP_TYPES,
	CRATE_OP_NR = CRATE_OP_TYPES,
	CRATE_OP_START = CRATE_OP_PUT,
	CRATE_OP_INVALID = CRATE_OP_TYPES,
};

struct clovis_workload_index {
	struct m0_uint128      *ids;
	int		       *op_status;
	int			num_index;
	int			num_kvs;
	int			mode;
	int			opcode;
	int			record_size;
	int			opcode_prcnt[CRATE_OP_TYPES];
	int			next_records;

	/** Total count for all operaions.
	 * If op_count == -1, then operations count is unlimited.
	 **/
	int			op_count;

	/** Maximum number of seconds to execute test.
	 * exec_time == -1 means "unlimited".
	 **/
	int			exec_time;

	/** Insert 'warmup_put_cnt' records before test. */
	int			warmup_put_cnt;
	/** Delete every 'warmup_del_ratio' records before test. */
	int			warmup_del_ratio;

	struct m0_fid		key_prefix;
	int			keys_count;

	bool			keys_ordered;

	struct m0_fid		index_fid;

	int			log_level;
	int			max_record_size;
	uint64_t		seed;
};

struct clovis_workload_task {
	int                    task_idx;
	int		      *op_status;
	struct m0_clovis_obj  *objs;
	struct m0_clovis_op  **ops;
	struct timeval        *op_list_time;
	struct m0_thread       mthread;
};

int parse_crate(int argc, char **argv, struct workload *w);
void clovis_run(struct workload *w, struct workload_task *task);
void clovis_op_run(struct workload *w, struct workload_task *task,
		   const struct workload_op *op);
void clovis_run_index(struct workload *w, struct workload_task *tasks);
void clovis_op_run_index(struct workload *w, struct workload_task *task,
			 const struct workload_op *op);


/** @} end of crate_clovis group */
#endif /* __MERO_CLOVIS_M0CRATE_CRATE_CLOVIS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
