/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 06/14/2013
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_MONITOR_H__
#define __MERO_ADDB_ADDB_MONITOR_H__

#include "addb/addb.h"
#include "lib/vec.h"
#include "lib/bitmap.h"
#include "rpc/session.h"

/**
   @page ADDB-MON-INFRA-DLD-fspec Functional Specification
   - @ref ADDB-MON-INFRA-DLD-fspec-ds
   - @ref ADDB-MON-INFRA-DLD-fspec-sub
   - @ref ADDB-MON-INFRA-DLD-fspec-cli
   - @ref ADDB-MON-INFRA-DLD-fspec-usecases
     - @ref ADDB-MON-INFRA-DLD-fspec-uc-IAAM
   - Detailed functional specifications:
     - @ref addb "Analysis and Diagnostics Data-Base API"
     - @ref addb_pvt "ADDB Internal Interfaces"

   <hr>
   @section ADDB-DLD-fspec-ds Data Structures

   The following data structures are involved in an ADDB monitoring
   infrastructure:
   - m0_addb_monitoring_ctx
   - m0_addb_monitor
   - m0_addb_sum_rec

   <hr>
   @section ADDB-DLD-fspec-sub Subroutines and Macros

   Interfaces needed for ADDB monitoring infrastructure
   - m0_addb_monitors_init()
   - m0_addb_monitor_add()
   - m0_addb_monitor_del()
   - m0_addb_monitors_fini()
   - m0_addb_monitor_summaries_post()

   <hr>
   @section ADDB-DLD-fspec-usecases Recipes
   - @ref ADDB-DLD-fspec-uc-IAAM

   @subsection ADDB-DLD-fspec-uc-IAAM Init & Add monitor

   This shows the series of steps that needs to be taken
   for creating, initializing & adding the monitor into mero system.

   @code
	   1. Add monitor to the structure that is to be monitored
	      or dynamically allocate them as mentioned in step 7.
	      Define key field to locate m0_addb_sum_rec for this monitor.
	struct m0_xyz_module_struct {
		.
		.
		.
		struct m0_addb_monitor xms_mon;
		uint32_t               xms_key;
	};

	Lets consider we have a variable available to access an instance
	of struct m0_xyz_module_struct, viz. xyz_module_var.

	   2. Define the ADDB summary record type.
	M0_ADDB_RT_DP(m0_addb_xyz, M0_ADDB_XYZ, "xyz_parm");

	   3. Define a new monitor specific stats data
	   NOTE: The fields should be only uint64_t
	struct xyz {
		uint64_t x_parm;
	};
	Its upto the monitor implementer where to define a global variable for
	this xyz or to embed it any top level structure. If dynamic
	allocation is needed, it should be done as mentioned in step 7.

	For this eq. lets consider it part of struct m0_xyz_module_struct
	mentioned in step 1.
	struct m0_xyz_module_struct {
		.
		.
		.
		struct xyz xms_xyz;
	};
	NOTE: fields of this struct should match in total number as well as
	the meaning to the ADDB summary record type defined in 2 (serial order
	should be maintained while defining the structure).

	   4. Define monitor operation amo_sum_rec
	struct m0_addb_sum_rec *xyz_amo_sum_rec(const struct m0_addb_monitor *m,
					       struct m0_reqh             *rqh)
	{
		struct m0_addb_sum_rec *sum_rec;

		m0_rwlock_read_lock(&reqh->rh_rwlock);
		sum_rec = m0_reqh_lockers_get(reqh, xyz_key);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);

		M0_POST(sum_rec != NULL);

		return sum_rec;
	}

	   5. Define monitor operations amo_watch()
	void xyz_amo_watch(struct m0_addb_monitor *monitor,
			   struct m0_addb_rec *rec, struct m0_reqh *reqh)
	{
		// for addb rec of type X
		struct m0_addb_sum_rec *xyz;

		m0_rwlock_read_lock(&reqh->rh_rwlock);
		xyz = reqh_lockers_get(reqh, xyz_key);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);

		// NOTE: Locking is optional, use only when monitor implementor
		// specifically wants to synchronize access to summary data.
		m0_mutex_lock(&xyz->asr_mutex);
		// update data
		xyz->asr_dirty = true;
		m0_mutex_unlock(&xyz->asr_mutex);
	}

	   6. Define monitor ops struct and set these operations as initializers
	const struct m0_addb_monitor_ops mon_ops = {
		.amo_watch = xyz_amo_watch,
		.amo_sum_rec = xyz_amo_sum_rec
	};

	   7. Define monitor entry's init function
	int xyz_ame_init(void)
	{
		1. Define monitor.(as part of some module structure or
		   global variable)
		2. Define monitor specific data, in example defined as
		   'struct xyz' & key for it. (as part of some module
		   structure or global variable)
		3. Dynamically allocate instance of struct m0_addb_sum_rec.
		4. Initialize monitor.
		5. Initialize summary record (m0_addb_sum_rec) with monitor
		   specific data.
		6. Initialize monitor specific data's key.
		7. Store (lockers_set) this monitor specific data in reqh's
		   locker data structure.
		8. Add monitor to reqh's list of monitors for this node.

		struct m0_addb_sum_rec *rec;
		M0_ALLOC_PTR(rec);

		m0_addb_monitor_init(&xyz_module_var.xms_mon, mon_ops);
		m0_addb_monitor_sum_rec_init(rec, &m0_addb_xyz,
					     &xyz_module_var.xms_xyz,
					     sizeof(xyz_module_var.xms_xyz));
		xyz_module.xms_key = m0_reqh_lockers_allot();
		m0_reqh_lockers_set(reqh, &xyz_module.xms_key, rec);

		m0_addb_monitor_add(&xyz_module_var.xms_mon);
	}

	   8. Define monitor entry's fini function
	void xyz_ame_fini(void)
	{
		1. Delete monitor from reqh's list of monitors for this node.
		2. Remove (lockers_clear)  monitor specific data from reqh's
		   locker data structure (obviously do lockers_get before this
		   to get the pointer to m0_addb_sum_rec structure for
		   following steps, NOTE: shown in below code snippet).
		3. Finalize summary record (m0_addb_sum_rec).
		4. Free summary record (m0_addb_sum_rec).
		5. Finalize monitor.
		6. Free monitor specific data, key or monitor iff any of them
		   was dynamically allocated.

		struct m0_addb_sum_rec *rec =
			&xyz_module_var.xms_mon->am_ops->amo_sum_rec(reqh,
							xyz_module_var.xms_mon);
		M0_PRE(rec != NULL);

		m0_addb_monitor_del(&xyz_module_var.xms_mon);
		m0_reqh_lockers_clear(reqh, &xyz_module_var.xms_key);
		m0_addb_monitor_sum_rec_fini(rec);
		m0_free(rec);
		m0_addb_monitor_fini(&xyz_module_var.xms_mon);
	}

   @endcode
*/

#include "stats/stats_fops.h"
#include "rpc/conn.h"

struct m0_reqh;
struct m0_addb_monitor;
struct addb_post_fom;

enum {
	ADDB_STATS_MAX_RPCS_IN_FLIGHT = 1,
};

M0_TL_DESCR_DECLARE(addb_mon, M0_EXTERN);

#define M0_ADDB_MONITOR_STATS_TYPE_REGISTER(stats_type, name)		\
do {									\
	(stats_type)->art_name = name;					\
	m0_addb_rec_type_register(stats_type);				\
} while(0)

struct m0_addb_sum_rec {
	/**
	 * This flag is true iff the record contains data still
	 * not sent to the stats service.
	 */
	bool                 asr_dirty;
	/** This lock seriailizes access to this structure */
	struct m0_mutex      asr_mutex;
	/**
	 * ADDB in-memory summary record
	 * m0_stats_sum:ss_data::au64s_data is monitor
	 * specific data
	 */
	struct m0_stats_sum  asr_rec;
};

struct m0_addb_monitor_ops {
	/**
	 * This method is called on each addb record.
	 */
	void                    (*amo_watch) (struct m0_addb_monitor   *mon,
					      const struct m0_addb_rec *rec,
					      struct m0_reqh           *r);
	/** Returns m0_addb_sum_rec, if any for this monitor. */
	struct m0_addb_sum_rec *(*amo_sum_rec) (struct m0_addb_monitor *m,
					        struct m0_reqh         *r);
};

struct m0_addb_monitor {
	/**
	 * Linkage to monitors list per reqh
	 * reqh::m0_addb_monitoring_ctx::amc_list
	 */
	struct m0_tlink                   am_linkage;
	/** ADDB monitor operations vector */
	const struct m0_addb_monitor_ops *am_ops;
	/* Magic needed for monitor's tlist */
	uint64_t                          am_magic;
};

/**
 * Monitor invariant
 */
M0_INTERNAL bool m0_addb_monitor_invariant(struct m0_addb_monitor *mon);

/**
 * Monitoring sub-system collective information
 * object.
 */
struct m0_addb_monitoring_ctx {
	uint64_t               amc_magic;
	/* List of active monitors */
	struct m0_tl           amc_list;
	/* Mutex to protect amc_list */
	struct m0_mutex        amc_mutex;
	/* Stats service endpoint */
	const char            *amc_stats_ep;
	struct m0_rpc_conn    *amc_stats_conn;
};

/**
 * Monitor context invariant.
 */
M0_INTERNAL bool m0_addb_mon_ctx_invariant(struct m0_addb_monitoring_ctx *ctx);

/**
 * Initialize ADDB monitoring sub-system
 * @param mach RPC machine
 * @param endpoint endpoint where stats service runs
 * @pre reqh != NULL
 * @post reqh::rh_addb_monitoring_ctx:amg_list initialized
 */
M0_INTERNAL int m0_addb_monitors_init(struct m0_reqh *reqh);

/**
 * Initialize ADDB monitor
 * @param monitor ADDB monitor object
 * @pre monitor != NULL
 * @pre (*am_watch) () != NULL
 * @pre rtype != NULL && m0_addb_rec_type_lookup(rtype->art_id) != NULL
 * @post *monitor != NULL
 */
M0_INTERNAL void m0_addb_monitor_init(struct m0_addb_monitor           *monitor,
				      const struct m0_addb_monitor_ops *mon_ops);

/**
 * Init m0_addb_sum_rec (Generic summary record structure)
 * @param rec ADDB summary record to init
 * @param rt  ADDB record type that this monitor would produce
 * @param md  uint64_t array of monitor specific data
 * @param nr  No of uint64_t words.
 */
M0_INTERNAL void m0_addb_monitor_sum_rec_init(struct m0_addb_sum_rec        *rec,
					      const struct m0_addb_rec_type *rt,
					      uint64_t                      *md,
					      size_t                         nr);

/**
 * Fini m0_addb_sum_rec (Generic summary record structure)
 * @param sum_rec ADDB summary record to fini
 */
M0_INTERNAL void m0_addb_monitor_sum_rec_fini(struct m0_addb_sum_rec *sum_rec);

/**
 * Check if monitor is initialised ot not ?
 * @param monitor monitor object
 * @retval true if initialised
 *         false otherwise
 */
M0_INTERNAL
bool m0_addb_monitor_is_initialised(struct m0_addb_monitor *monitor);

/**
 * Add a particular monitor with the ADDB monitoring sub-system
 * @param reqh Request handler
 * @param monitor ADDB monitor to register
 */
M0_INTERNAL void m0_addb_monitor_add(struct m0_reqh *reqh,
				     struct m0_addb_monitor *monitor);

/**
 * Delete a particular monitor from the ADDB monitoring sub-system
 * @param reqh Request handler
 * @param monitor ADDB monitor to unregister
 */
M0_INTERNAL void m0_addb_monitor_del(struct m0_reqh *reqh,
		                     struct m0_addb_monitor *monitor);

/**
 * Finalize monitor
 * @param  monitor ADDB monitor to finalize
 */
M0_INTERNAL void m0_addb_monitor_fini(struct m0_addb_monitor *monitor);

/**
 * Cleanup the ADDB monitoring sub-system
 */
M0_INTERNAL void m0_addb_monitors_fini(struct m0_reqh *reqh);

/**
 * This sends all the dirtied addb summary records for the added monitors
 * to the stats service as a fop.
 */
M0_INTERNAL int m0_addb_monitor_summaries_post(struct m0_reqh       *reqh,
					       struct addb_post_fom *fom);

/**
 * Setup addb monitoring ctx within request handler with stats service
 * connection details
 * @param reqh request handler
 * @param conn rpc connection object of stats service
 * @param ep stats service endpoint
 */
M0_INTERNAL void m0_addb_monitor_setup(struct m0_reqh     *reqh,
				       struct m0_rpc_conn *conn,
				       const char         *ep);

/**
 * Macro to post ADDB monitor stats summary records as ADDB records
 * on given addb machine.
 * @param mc    Specify the addb machine.
 * @param rt    Specify the pointer to the ADDB record type.
 * @param cv    Specify the context vector.
 * @param stats Specify the m0_stats_sum structure, this represents
 *              the stats summary record that needs to be posted.
 */
#define M0_ADDB_MONITOR_STATS_POST(mc, rt, cv, stats)                   \
do {                                                                    \
	struct m0_addb_post_data pd;                                    \
	M0_ASSERT(mc != NULL && rt != NULL && cv != NULL &&             \
		  stats != NULL);                                       \
	pd.apd_rt = rt;                                                 \
	pd.apd_cv = cv;                                                 \
	M0_ASSERT((rt)->art_id == stats->ss_id);                        \
	M0__ADDB_POST(pd.apd_rt, stats->ss_data.au64s_nr,               \
		      stats->ss_data.au64s_data);                       \
	m0__addb_post(mc, &pd);                                         \
} while (0)

/**
 * Establish rpc connection & session with stats service endpoint.
 * @parm reqh Initialized request handler.
 */
M0_INTERNAL int m0_addb_monitor_stats_svc_conn_init(struct m0_reqh *reqh);

/**
 * Close rpc connection & session with stats service endpoint.
 * @param reqh Initialized request handler.
 */
M0_INTERNAL void m0_addb_monitor_stats_svc_conn_fini(struct m0_reqh *reqh);

#endif /* __MERO_ADDB_ADDB_MONITOR_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
