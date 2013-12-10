/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/08/2012
 */

#pragma once

#ifndef __MERO_SNS_CM_H__
#define __MERO_SNS_CM_H__

#include "net/buffer_pool.h"
#include "layout/pdclust.h"
#include "pool/pool.h"

#include "cm/cm.h"
#include "sns/cm/iter.h"
#include "rm/rm.h"
#include "file/file.h"
#include "lib/hash.h"


/**
  @page SNSCMDLD-fspec SNS copy machine functional specification

  - @ref SNSCMDLD-fspec-ds
  - @ref SNSCMDLD-fspec-if
  - @ref SNSCMDLD-fspec-usecases-repair
  - @ref SNSCMDLD-fspec-usecases-rebalance

  @section SNSCMDLD-fspec Functional Specification
  SNS copy machine provides infrastructure to perform tasks like repair and
  re-balance using parity de-clustering layout.

  @subsection SNSCMDLD-fspec-ds Data Structures
  - m0_sns_cm_cm
    Represents sns copy machine, this embeds generic struct m0_cm and sns
    specific copy machine objects.

  - m0_sns_cm_iter
    Represents sns copy machine data iterator.

  @subsection SNSCMDLD-fspec-if Interfaces
  - m0_sns_cm_cm_type_register
    Registers sns copy machine type and its corresponding request handler
    service type.
    @see m0_sns_init()

  - m0_sns_cm_cm_type_deregister
    De-registers sns copy machine type and its corresponding request handler
    service type.
    @see m0_sns_fini()

  @subsection SNSCMDLD-fspec-usecases-repair SNS repair recipes
  Test 01: Start sns copy machine service using mero_setup
  Response: SNS copy machine service is started and copy machine is
            initialised.
  Test 02: Configure sns copy machine for repair, create COBs and simulate a
           COB failure.
  Response: The parity groups on the survived COBs should be iterated and
            repair completes successfully.

  @subsection SNSCMDLD-fspec-usecases-rebalance SNS re-balance recipes
  Test 01: Start sns copy machine for repair, once repair is completeand copy
           is stopped, re-start the same copy machine to perform re-balance
           operation.
  Response: Re-balance should complete successfully.
 */

/**
  @defgroup SNSCM SNS copy machine
  @ingroup CM

  SNS copy machine is a replicated state machine, which performs data
  restructuring and handles device, container, node, &c failures.
  @see The @ref SNSCMDLD

  @{
*/

/**
 * Operation that sns copy machine is carrying out.
 */
enum m0_sns_cm_op {
	SNS_REPAIR = 1 << 1,
	SNS_REBALANCE = 1 << 2
};

enum m0_sns_cm_flock_status {
	M0_SCM_FILE_NOT_LOCKED = 0,
	M0_SCM_FILE_LOCKED = 1,
	M0_SCM_FILE_LOCK_WAIT = 2
};

struct m0_sns_cm_buf_pool {
	struct m0_net_buffer_pool sb_bp;
	/**
	 * Channel to wait for buffers in buffer pool to be released.
	 * This channel is signalled when any acquired buffer belonging to
	 * buffer pool is released back to the buffer pool
	 */
	struct m0_chan            sb_wait;
	struct m0_mutex           sb_wait_mutex;
};

/**
 * SNS copy machine helpers for different operations, viz. sns-repair,
 * sns-rebalance, etc.
 */
struct m0_sns_cm_helpers {
	/**
	 * Returns maximum possible number of aggregation group units to be
	 * received by this replica.
	 */
	uint64_t (*sch_ag_max_incoming_units)(const struct m0_sns_cm *scm,
					      const struct m0_cm_ag_id *id,
					      struct m0_pdclust_layout *pl);

	/**
	 * Returns index of starting unit of the given aggregation group to
	 * iterate upon.
	 */
	uint64_t (*sch_ag_unit_start)(const struct m0_pdclust_layout *pl);

	/**
	 * Returns index of final unit of the given aggregation group to
	 * iterate upon.
	 */
	uint64_t (*sch_ag_unit_end)(const struct m0_pdclust_layout *pl);

	/**
	 * Returns true iff the given aggregation group has any incoming copy
	 * packets from other replicas, else false.
	 */
	bool     (*sch_ag_is_relevant)(struct m0_sns_cm *scm,
				       const struct m0_fid *gfid,
				       uint64_t group,
				       struct m0_pdclust_layout *pl,
				       struct m0_pdclust_instance *pi);

	int      (*sch_ag_setup)(struct m0_sns_cm_ag *sag,
				 struct m0_pdclust_layout *pl);

};

/** Resource manager context for a sns copy machine. */
struct m0_sns_cm_rm_ctx {
	/*
	 * Resource manager domain for this sns copy machine.
	 * Ideally this should be a part of reqh object, but
	 * that would require modifications in the rm code.
	 */
	struct m0_rm_domain             rc_dom;
	struct m0_rm_resource_type	rc_rt;

	/** RPC objects  representing the the remote rm owner. */
	struct m0_rpc_conn              rc_conn;
	struct m0_rpc_session           rc_session;
};

/**
 * Holds the rm file lock context of a file that is being repaired/rebalanced.
 * @todo Use the same object to hold layout context.
 */
struct m0_sns_cm_file_ctx {
        /** Holds M0_SNS_CM_FILE_CTX_MAGIC. */
        uint64_t                   sf_magic;

	/** Resource manager file lock for this file. */
	struct m0_file		   sf_file;

	/**
	 * The global file identifier for this file. This would be used
	 * as a "key" to the m0_sns_cm::sc_file_ctx and hence cannot
	 * be a pointer.
	 */
	struct m0_fid		   sf_fid;

	/** Linkage into m0_sns_cm::sc_file_ctx. */
	struct m0_hlink		   sf_sc_link;

	/** An owner for maintaining this file's lock. */
	struct m0_rm_owner	   sf_owner;

	/** Remote portal for requesting resource from creditor. */
	struct m0_rm_remote	   sf_creditor;

	/** Request to borrow resource (file lock) from creditor. */
	struct m0_rm_incoming	   sf_rin;

	/** Back pointer to the sns copy machine. */
	struct m0_sns_cm	  *sf_scm;

	/**
	 * Count of aggregation groups that would be processed for this fid.
	 * When all the aggregagtion groups have been processed, the
	 * file lock can be released.
	 */
	uint64_t		   sf_ag_nr;

	/**
	 * Holds the reference count for this object. The reference is
	 * incremented explicitly every time the file lock is acquired.
	 * The reference is decremented by calling m0_sns_cm_file_unlock().
	 * The m0_sns_cm_file_ctx object is freed (and file is unlocked) whenever
	 * the reference count equals zero.
	 */
	struct m0_ref		   sf_ref;

	 /** Holds the current status of the file lock. */
	enum m0_sns_cm_flock_status sf_flock_status;
};

struct m0_sns_cm {
	struct m0_cm		        sc_base;

	/** Operation that sns copy machine is going to execute. */
	enum m0_sns_cm_op               sc_op;

	/**
	 * Helper functions implemented with respect to specific sns copy
	 * machine operation, viz. repair or re-balance.
	 */
	const struct m0_sns_cm_helpers *sc_helpers;

	/** SNS copy machine data iterator. */
	struct m0_sns_cm_iter           sc_it;

	/**
	 * Buffer pool for incoming copy packets, this is used by sliding
	 * window.
	 */
	struct m0_sns_cm_buf_pool       sc_ibp;

	/**
	 * Maintains the reserve count for the buffers from the
	 * m0_sns_cm::sc_ibp buffer pool for the incoming copy packets
	 * corresponding to the aggregation groups in the
	 * struct m0_cm::cm_aggr_grps_in list. This makes sure that the buffers
	 * are available for all the incoming copy packets within the sliding
	 * window.
	 */
	uint64_t                        sc_ibp_reserved_nr;

	/** Buffer pool for outgoing copy packets. */
	struct m0_sns_cm_buf_pool       sc_obp;

	/** Tracks the number for which repair operation has been executed. */
	uint32_t		        sc_repair_done;

	/**
	 * Start time for sns copy machine. This is recorded when the ready fop
	 * arrives to the sns copy machine replica.
	 */
	m0_time_t                       sc_start_time;

	/**
	 * Stop time for sns copy machine. This is recorded when repair is
	 * completed.
	 */
	m0_time_t                       sc_stop_time;

	/**
	 * The hash table of file contexts of the fids being
	 * repaired/rebalanced. Currently holds the file lock related
	 * information only. Could be used to hold sns layout ctx
	 * in the future.
	 */
	struct m0_htable                sc_file_ctx;

	/** Mutex to serialise the access to sc_file_ctx hash table. */
	struct m0_mutex		        sc_file_ctx_mutex;

	/** Resource manager context for this sns copy machine. */
	struct m0_sns_cm_rm_ctx		sc_rm_ctx;

	/** Magic denoted by M0_SNS_CM_MAGIC. */
	uint64_t                        sc_magic;

};

M0_INTERNAL int m0_sns_cm_type_register(void);
M0_INTERNAL void m0_sns_cm_type_deregister(void);

M0_INTERNAL size_t m0_sns_cm_buffer_pool_provision(struct m0_net_buffer_pool *bp,
                                                   size_t bufs_nr);
M0_INTERNAL void m0_sns_cm_buffer_pools_prune(struct m0_cm *cm);
M0_INTERNAL struct m0_net_buffer *m0_sns_cm_buffer_get(struct m0_net_buffer_pool
						       *bp, size_t colour);
M0_INTERNAL void m0_sns_cm_buffer_put(struct m0_net_buffer_pool *bp,
					  struct m0_net_buffer *buf,
					  uint64_t colour);

M0_INTERNAL int m0_sns_cm_buf_attach(struct m0_net_buffer_pool *bp,
				     struct m0_cm_cp *cp);

M0_INTERNAL struct m0_sns_cm *cm2sns(struct m0_cm *cm);

M0_INTERNAL struct m0_cm_cp *m0_sns_cm_cp_alloc(struct m0_cm *cm);

M0_INTERNAL int m0_sns_cm_prepare(struct m0_cm *cm);

M0_INTERNAL int m0_sns_cm_stop(struct m0_cm *cm);

M0_INTERNAL int m0_sns_cm_setup(struct m0_cm *cm);

M0_INTERNAL int m0_sns_cm_start(struct m0_cm *cm);

M0_INTERNAL int m0_sns_cm_ag_next(struct m0_cm *cm,
				  const struct m0_cm_ag_id id_curr,
				  struct m0_cm_ag_id *id_next);

M0_INTERNAL void m0_sns_cm_complete(struct m0_cm *cm);

M0_INTERNAL void m0_sns_cm_fini(struct m0_cm *cm);

M0_INTERNAL uint64_t
m0_sns_cm_incoming_reserve_bufs(struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl);

M0_INTERNAL uint64_t m0_sns_cm_data_seg_nr(struct m0_sns_cm *scm,
					   struct m0_pdclust_layout *pl);

M0_INTERNAL void m0_sns_cm_buf_available(struct m0_net_buffer_pool *pool);

M0_INTERNAL uint64_t m0_sns_cm_cp_buf_nr(struct m0_net_buffer_pool *bp,
                                         uint64_t data_seg_nr);

M0_INTERNAL void m0_sns_cm_normalize_reservation(struct m0_sns_cm *scm,
						 struct m0_cm_aggr_group *ag,
						 struct m0_pdclust_layout *pl,
						 uint64_t nr_res_bufs);

M0_INTERNAL bool m0_sns_cm_has_space_for(struct m0_sns_cm *scm,
					 struct m0_pdclust_layout *pl,
					 uint64_t nr_bufs);

M0_INTERNAL int m0_sns_cm_pm_event_post(struct m0_sns_cm *scm,
					enum m0_pool_event_owner_type et,
					enum m0_pool_nd_state state);

/**
 * Returns state of SNS repair process with respect to @gfid.
 * @param gfid Input global fid for which SNS repair state has to
 *             be retrieved.
 * @param reqh Parent request handler object.
 * @pre   m0_fid_is_valid(gfid) && reqh != NULL.
 * @ret   1 if SNS repair has not started at all.
 *        2 if SNS repair has started but not completed for @gfid.
 *        3 if SNS repair has started and completed for @gfid.
 */
M0_INTERNAL enum sns_repair_state
m0_sns_cm_fid_repair_done(struct m0_fid *gfid, struct m0_reqh *reqh);

M0_INTERNAL int m0_sns_cm_rm_init(struct m0_sns_cm *scm);
M0_INTERNAL void  m0_sns_cm_rm_fini(struct m0_sns_cm *scm);

/** Allocates and initialises new m0_sns_cm_file_ctx object. */
M0_INTERNAL int m0_sns_cm_fctx_init(struct m0_sns_cm *scm, struct m0_fid *fid,
				    struct m0_sns_cm_file_ctx **sc_fctx);
M0_INTERNAL void m0_sns_cm_fctx_fini(struct m0_sns_cm_file_ctx *fctx);

/*
 * Invokes async file lock api and adds the given fctx object to
 * m0_sns_cm::sc_file_ctx hash table. Its the responsibility of
 * the caller to wait for the lock on rm's incoming channel where
 * the state changes are announced.
 *
 * @see m0_sns_cm_file_lock_wait().
 * @ret M0_FSO_WAIT.
 */
M0_INTERNAL int m0_sns_cm_file_lock(struct m0_sns_cm_file_ctx *fctx);

/**
 * Returns M0_FS0_WAIT until the rm file lock is acquired. The given
 * fom waits on the the rm incoming channel till the file lock is acquired.
 * @ret 0 When file lock is acquired successfully.
 * @ret -EFAULT     When file lock acquisition fails.
 * @ret M0_FS0_WAIT When waiting for the file lock to be acquired.
 */
M0_INTERNAL int m0_sns_cm_file_lock_wait(struct m0_sns_cm_file_ctx *fctx,
					 struct m0_fom *fom);

/**
 * Decrements the reference on the m0_sns_cm_file_ctx object.
 * When the count reaches null, m0_file_unlock() is invoked and
 * the m0_sns_cm_file_ctx_object is freed.
 * @see m0_sns_cm_fid_check_unlock()
 */
M0_INTERNAL void m0_sns_cm_file_unlock(struct m0_sns_cm_file_ctx *fctx);

/**
 * Invokes m0_sns_cm_file_unlock() if all the aggregation groups belonging to a
 * file have been processed.
 */
M0_INTERNAL void m0_sns_cm_fid_check_unlock(struct m0_sns_cm *scm,
					    struct m0_fid *fid);

/**
 * Looks up the m0_sns_cm::sc_file_ctx hash table and returns the
 * m0_sns_cm_file_ctx object for the passed global fid.
 * Returns NULL if the object is not present in the hash table.
 */
M0_INTERNAL struct m0_sns_cm_file_ctx
		  *m0_sns_cm_fctx_locate(struct m0_sns_cm *scm,
		                         struct m0_fid *fid);

M0_INTERNAL void m0_sns_cm_fctx_ag_incr(struct m0_sns_cm *scm,
					const struct m0_cm_ag_id *id);

M0_INTERNAL void m0_sns_cm_fctx_ag_dec(struct m0_sns_cm *scm,
				       const struct m0_cm_ag_id *id);

M0_HT_DESCR_DECLARE(m0_scmfctx, M0_EXTERN);
M0_HT_DECLARE(m0_scmfctx, M0_EXTERN, struct m0_sns_cm_file_ctx,
	      struct m0_fid);

/** @} SNSCM */
#endif /* __MERO_SNS_CM_CM_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
