/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 15-Aug-2016
 */

#pragma once

#ifndef __MERO_DIX_REQ_H__
#define __MERO_DIX_REQ_H__

/**
 * @addtogroup dix
 *
 * @{
 *
 * DIX requests allow to access and modify distributed indices. Requests are
 * executed in the context of distributed index client.
 *
 * Requests logic
 * --------------
 * All operations available over distributed indices share a common logic:
 * - Retrieve index layout descriptor. There are 3 cases:
 *   * Layout descriptor is provided by user: nothing to do.
 *   * Layout id is provided by user: lookup descriptor in layout-descr index.
 *   * Only index fid is specified: lookup descriptor starting from root index.
 * - Create index layout instance (@ref m0_dix_linst) based on layout
 *   descriptor.
 * - Calculate numerical orders of storage devices in a pool (Dn, Dp1 ... Dpk,
 *   Ds1 ... Dsk) that are involved into operation using built layout instance.
 *   Devices Dn, Dp1 ... Dpk are used to store K+1 replicas of index
 *   records. Devices Ds1 .. Dsk only reserve space for index records, that is
 *   used during index repair/rebalance. Numerical order of every pool device is
 *   stored in configuration (m0_conf_sdev::sd_dev_idx).
 * - For every device involved find CAS service serving it. This relationship is
 *   also stored in configuration database.
 * - For every element in input operation vector (record for put(), key for
 *   del(), etc.) execute corresponding CAS operation against:
 *     * Random CAS service if operation doesn't modify index (get, next).
 *     * All CAS services if operation carries modification (put, del).
 *   Operations are executed in parallel.
 * - Wait until all operations are finished and collect results.
 *
 * Execution context
 * -----------------
 * Client operations are executed in the context of state machine group
 * provided at initialisation. Almost all operations have asynchronous
 * interface. It is possible to run several requests in the context of one
 * client simultaneously even if the same state machine group is used.
 */

#include "fid/fid.h"           /* m0_fid */
#include "sm/sm.h"             /* m0_sm */
#include "pool/pool_machine.h" /* m0_poolmach_versions */
#include "cas/client.h"        /* m0_cas_req */
#include "dix/layout.h"        /* m0_dix_layout */
#include "dix/req_internal.h"  /* m0_dix_idxop_ctx */

/* Import */
struct m0_bufvec;
struct m0_dix_ldesc;
struct m0_dtx;
struct m0_dix_meta_req;
struct m0_dix_cli;

enum m0_dix_req_state {
	DIXREQ_INVALID,
	DIXREQ_INIT,
	DIXREQ_LAYOUT_DISCOVERY,
	DIXREQ_LID_DISCOVERY,
	DIXREQ_DISCOVERY_DONE,
	DIXREQ_META_UPDATE,
	DIXREQ_INPROGRESS,
	DIXREQ_GET_RESEND,
	DIXREQ_DEL_PHASE2,
	DIXREQ_FINAL,
	DIXREQ_FAILURE,
	DIXREQ_NR
};

struct m0_dix {
	struct m0_fid        dd_fid;
	struct m0_dix_layout dd_layout;
};

struct m0_dix_next_sort_ctx {
	struct m0_cas_req        *sc_creq;
	struct m0_cas_next_reply *sc_reps;
	uint32_t                  sc_reps_nr;
	bool                      sc_stop;
	bool                      sc_done;
	uint32_t                  sc_pos;
};

struct m0_dix_next_sort_ctx_arr {
	struct m0_dix_next_sort_ctx *sca_ctx;
	uint32_t                     sca_nr;
};

struct m0_dix_next_results {
	struct m0_cas_next_reply  **drs_reps;
	uint32_t                    drs_nr;
	uint32_t                    drs_pos;
};

struct m0_dix_next_resultset {
	/**
	 * Results: i-bufitem contains array with reps_nr[i] items of
	 * m0_cas_next_reply.
	 * start_keys.ov_vec.v_nr is a count of items in bufvec.
	 */
	struct m0_dix_next_results      *nrs_res;
	uint32_t                         nrs_res_nr;
	struct m0_dix_next_sort_ctx_arr  nrs_sctx_arr;
};

enum dix_req_type {
	/** Create new index. */
	DIX_CREATE,
	/** Lookup component catalogues existence for an index. */
	DIX_CCTGS_LOOKUP,
	/** Delete an index. */
	DIX_DELETE,
	/** Given start keys, get records with the next keys from an index. */ 
	DIX_NEXT,
	/** Get records with the given keys from an index. */
	DIX_GET,
	/** Put given records in an index. */
	DIX_PUT,
	/** Delete records with the given keys from an index. */
	DIX_DEL
};

struct m0_dix_req {
	struct m0_sm                  dr_sm;
	struct m0_dix_cli            *dr_cli;
	struct m0_dix_meta_req       *dr_meta_req;
	struct m0_clink               dr_clink;
	struct m0_dix                *dr_orig_indices;
	struct m0_dix                *dr_indices;
	uint32_t                      dr_indices_nr;
	bool                          dr_is_meta;
	struct m0_dix_item           *dr_items;
	const struct m0_bufvec       *dr_keys;
	const struct m0_bufvec       *dr_vals;
	uint64_t                      dr_items_nr;
	struct m0_dtx                *dr_dtx;
	struct m0_dix_idxop_ctx       dr_idxop;
	struct m0_dix_rop_ctx        *dr_rop;
	struct m0_sm_ast              dr_ast;
	enum dix_req_type             dr_type;
	struct m0_dix_next_resultset  dr_rs;
	uint32_t                     *dr_recs_nr;
	/** Request flags bitmask of m0_cas_op_flags values. */
	uint32_t                      dr_flags;
	struct m0_poolmach_versions   dr_pmach_ver;
};

/**
 * Single value retrieved by m0_dix_get() request.
 */
struct m0_dix_get_reply {
	int           dgr_rc;
	/** Retrieved value. Undefined if dgr_rc != 0. */
	struct m0_buf dgr_val;
};

struct m0_dix_next_reply {
	/** Record key. */
	struct m0_buf dnr_key;
	/** Record value. */
	struct m0_buf dnr_val;
};

M0_INTERNAL void m0_dix_req_init(struct m0_dix_req  *req,
				 struct m0_dix_cli  *cli,
				 struct m0_sm_group *grp);

M0_INTERNAL void m0_dix_mreq_init(struct m0_dix_req  *req,
				  struct m0_dix_cli  *cli,
				  struct m0_sm_group *grp);

/**
 * Locks state machine group that is used by dix request.
 */
M0_INTERNAL void m0_dix_req_lock(struct m0_dix_req *req);

/**
 * Unlocks state machine group that is used by dix request.
 */
M0_INTERNAL void m0_dix_req_unlock(struct m0_dix_req *req);

/**
 * Checks whether dix request state machine group is locked.
 */
M0_INTERNAL bool m0_dix_req_is_locked(const struct m0_dix_req *req);

M0_INTERNAL int m0_dix_req_wait(struct m0_dix_req *req, uint64_t states,
				m0_time_t to);

M0_INTERNAL int m0_dix_create(struct m0_dix_req   *req,
			      const struct m0_dix *indices,
			      uint32_t             indices_nr,
			      struct m0_dtx       *dtx,
			      uint32_t             flags);

/**
 * Checks whether all component catalogues exist for the given indices.
 * Returns error if any component catalogue isn't accessible (e.g. disk where it
 * resides has failed) or doesn't exist. It doesn't make sense to call this
 * function for distributed indices with CROW policy, since some component
 * catalogues may be not created yet.
 */
M0_INTERNAL int m0_dix_cctgs_lookup(struct m0_dix_req   *req,
				    const struct m0_dix *indices,
				    uint32_t             indices_nr);

M0_INTERNAL int m0_dix_delete(struct m0_dix_req   *req,
			      const struct m0_dix *indices,
			      uint64_t             indices_nr,
			      struct m0_dtx       *dtx,
			      uint32_t             flags);

/**
 * Inserts records to a distributed index.
 *
 * @pre flags & ~(COF_OVERWRITE | COF_CROW)) == 0
 */
M0_INTERNAL int m0_dix_put(struct m0_dix_req      *req,
			   const struct m0_dix    *index,
			   const struct m0_bufvec *keys,
			   const struct m0_bufvec *vals,
			   struct m0_dtx          *dtx,
			   uint32_t                flags);

M0_INTERNAL int m0_dix_get(struct m0_dix_req      *req,
			   const struct m0_dix    *index,
			   const struct m0_bufvec *keys);

M0_INTERNAL void m0_dix_get_rep(const struct m0_dix_req *req,
				uint64_t                 idx,
				struct m0_dix_get_reply *rep);

M0_INTERNAL int m0_dix_del(struct m0_dix_req      *req,
			   const struct m0_dix    *index,
			   const struct m0_bufvec *keys,
			   struct m0_dtx          *dtx);

M0_INTERNAL int m0_dix_next(struct m0_dix_req      *req,
			    const struct m0_dix    *index,
			    const struct m0_bufvec *start_keys,
			    const uint32_t         *recs_nr,
			    uint32_t                flags);

/**
 * Gets 'val_idx'-th value retrieved for 'key_idx'-th key as a result of
 * m0_dix_next() request.
 *
 * @pre m0_dix_item_rc(req, key_idx) == 0
 */
M0_INTERNAL void m0_dix_next_rep(const struct m0_dix_req  *req,
				 uint64_t                  key_idx,
				 uint64_t                  val_idx,
				 struct m0_dix_next_reply *rep);

/**
 * Returns number of values retrieved for 'key_idx'-th key.
 *
 * @pre m0_dix_item_rc(req, key_idx) == 0
 */
M0_INTERNAL uint32_t m0_dix_next_rep_nr(const struct m0_dix_req *req,
					uint64_t                 key_idx);

M0_INTERNAL void m0_dix_next_rep_mlock(struct m0_dix_req *req,
				       uint32_t           key_idx,
				       uint32_t           val_idx);

/**
 * Returns generic return code for the operation.
 *
 * If the generic return code is negative, then the whole request has failed.
 * Otherwise, the user should check return codes for the individual items in
 * operation vector via m0_dix_item_rc().
 *
 * @pre M0_IN(req->dr_sm.sm_state, (DIXREQ_FINAL, DIXREQ_FAILURE))
 */
M0_INTERNAL int m0_dix_generic_rc(const struct m0_dix_req *req);

/**
 * Returns execution result for the 'idx'-th item in the input vector.
 *
 * @pre m0_dix_generic_rc(req) == 0
 * @pre idx < m0_dix_req_nr(req)
 */
M0_INTERNAL int m0_dix_item_rc(const struct m0_dix_req *req,
			       uint64_t                 idx);

M0_INTERNAL uint64_t m0_dix_req_nr(const struct m0_dix_req *req);

M0_INTERNAL void m0_dix_get_rep_mlock(struct m0_dix_req *req, uint64_t idx);

M0_INTERNAL void m0_dix_req_fini(struct m0_dix_req *req);

M0_INTERNAL void m0_dix_req_fini_lock(struct m0_dix_req *req);

M0_INTERNAL int m0_dix_desc_set(struct m0_dix             *dix,
				const struct m0_dix_ldesc *desc);

M0_INTERNAL int m0_dix_copy(struct m0_dix *dst, const struct m0_dix *src);

M0_INTERNAL void m0_dix_fini(struct m0_dix *dix);

/** @} end of dix group */

#endif /* __MERO_DIX_REQ_H__ */

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
