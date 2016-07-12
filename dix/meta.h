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
 * Original creation date: 24-Jun-2016
 */

#pragma once

#ifndef __MERO_DIX_META_H__
#define __MERO_DIX_META_H__

/**
 * @addtogroup dix
 *
 * @{
 *
 * Module "meta" contains functionality to create, destroy, and update
 * meta-data for distributed indexing subsystem.
 *
 * There are three groups of functions to work with 'root', 'layout' and
 * 'layout-descr' indices respectively. Operations for 'root' index are
 * synchronous, since they intended to be executed during provisioning.
 * Operations for 'layout', 'layout-descr' indices are asynchronous.
 *
 * Format of record in 'layout' index:
 * key: index fid
 * val: layout descriptor or layout id
 *
 * Format of record in 'layout-descr' index:
 * key: layout id
 * val: layout descriptor
 *
 * Root index is used to locate 'layout' and 'layout-descr' indices.
 *
 * For more information see dix/client.h.
 *
 * Also, module contains several functions for xcoding m0_dix_ldesc,
 * m0_dix_layout types.
 * There are three types of buffers that can be encoded/decoded via these
 * functions:
 * - [fid + m0_dix_ldescr array]            -> encoded into values
 * - [layout id array, m0_dix_ldescr array] -> encoded into keys and values
 * - [fid array, m0_dix_layout array]       -> encoded into keys and values
 *
 * Usage example.
 * Encode existing arrays of fids and layouts into two buffer vectors (vals and
 * keys):
 * @code
 * rc = m0_dix__layout_vals_enc(fids_array, layout_array, nr, &keys, &vals);
 * @endcode
 * Decode fids and layouts from existing buffer vectors (vals and keys):
 * @code
 * rc = m0_dix__layout_vals_dec(&keys, &vals, fids_array, layout_array, nr);
 * @endcode
 */

#include "lib/types.h"
#include "lib/chan.h"
#include "lib/vec.h"
#include "lib/mutex.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"
#include "dix/layout.h"
#include "dix/req.h"

/* Import */
struct m0_sm_group;
struct m0_dix_cli;
struct m0_dix_layout;
struct m0_dix_ldesc;

M0_EXTERN const struct m0_fid m0_dix_root_fid;
M0_EXTERN const struct m0_fid m0_dix_layout_fid;
M0_EXTERN const struct m0_fid m0_dix_ldescr_fid;

struct m0_dix_meta_req {
	struct m0_dix_req dmr_req;
	struct m0_bufvec  dmr_keys;
	struct m0_bufvec  dmr_vals;
	struct m0_chan    dmr_chan;
	struct m0_mutex   dmr_wait_mutex;
	struct m0_clink   dmr_clink;
};

M0_INTERNAL void m0_dix_meta_req_init(struct m0_dix_meta_req *req,
				      struct m0_dix_cli      *cli,
				      struct m0_sm_group     *grp);

M0_INTERNAL void m0_dix_meta_req_fini(struct m0_dix_meta_req *req);

M0_INTERNAL void m0_dix_meta_lock(struct m0_dix_meta_req *req);

M0_INTERNAL void m0_dix_meta_unlock(struct m0_dix_meta_req *req);

/**
 * The same as m0_dix_meta_req_fini(), but takes request lock internally.
 */
M0_INTERNAL void m0_dix_meta_req_fini_lock(struct m0_dix_meta_req *req);

/**
 * Returns generic return code for the meta operation.
 *
 * If the generic return code is negative, then the whole request has failed.
 * Otherwise, the user should check return codes for the individual items in
 * operation vector via m0_dix_meta_item_rc().
 */
M0_INTERNAL int m0_dix_meta_generic_rc(const struct m0_dix_meta_req *req);

/**
 * Returns execution result for the 'idx'-th item in the input vector.
 *
 * @pre m0_dix_meta_generic_rc(req) == 0
 * @pre idx < m0_dix_meta_req_nr(req)
 */
M0_INTERNAL int m0_dix_meta_item_rc(const struct m0_dix_meta_req *req,
				    uint64_t                      idx);

/**
 * Number of items in a user input vector for the given meta request.
 */
M0_INTERNAL int m0_dix_meta_req_nr(const struct m0_dix_meta_req *req);

/**
 * Creates meta-indices (root, layout, layout-descr).
 *
 * @note The function is synchronous.
 * @pre cli->dx_sm.sm_state == DIXCLI_BOOTSTRAP
 */
M0_INTERNAL int m0_dix_meta_create(struct m0_dix_cli   *cli,
				   struct m0_sm_group  *grp,
				   struct m0_dix_ldesc *dld_layout,
				   struct m0_dix_ldesc *dld_ldescr);

/**
 * Checks whether meta-data is available (previously created).
 *
 * Performs check for existence of 'root', 'layout' and 'layout-descr' in
 * pool version assigned in client (including all meta-indices component
 * catalogues).
 *
 * @note The function is synchronous.
 * @pre cli->dx_sm.sm_state == DIXCLI_READY
 * @see m0_dix_cctgs_lookup()
 */
M0_INTERNAL int m0_dix_meta_check(struct m0_dix_cli  *cli,
				  struct m0_sm_group *grp,
				  bool               *result);

/**
 * Destroys meta-indices (root, layout, layout-descr).
 *
 * @note The function is synchronous.
 * @pre cli->dx_sm.sm_state == DIXCLI_READY
 */
M0_INTERNAL int m0_dix_meta_destroy(struct m0_dix_cli   *cli,
				    struct m0_sm_group *grp);

/**
 * Reads 'layout' and 'layout-descr' layouts from root index.
 *
 * After meta request is finished the layouts are accessible through
 * m0_dix_root_read_rep().
 */
M0_INTERNAL int m0_dix_root_read(struct m0_dix_meta_req *req);

/**
 * Returns result of m0_dix_root_read() request.
 */
M0_INTERNAL int m0_dix_root_read_rep(struct m0_dix_meta_req *req,
				     struct m0_dix_ldesc    *layout,
				     struct m0_dix_ldesc    *ldescr);

/**
 * Adds mapping between layout id and layout descriptor into 'layout-descr'
 * meta-index.
 */
M0_INTERNAL int m0_dix_ldescr_put(struct m0_dix_meta_req    *req,
				  const uint64_t            *lid,
				  const struct m0_dix_ldesc *ldesc,
				  uint32_t                   nr);

/**
 * Queries layout descriptor for the given layout id from 'layout-descr'
 * meta-index. After request is finished retrieved layout descriptor can be
 * accessed through m0_dix_ldescr_rep_get().
 */
M0_INTERNAL int m0_dix_ldescr_get(struct m0_dix_meta_req *req,
				  const uint64_t         *lid,
				  uint32_t                nr);

/**
 * Returns result of m0_dix_ldescr_get() request.
 * 
 * @pre m0_dix_meta_generic_rc(req) == 0
 */
M0_INTERNAL int m0_dix_ldescr_rep_get(struct m0_dix_meta_req *req,
				      uint64_t                idx,
				      struct m0_dix_ldesc    *ldesc);

/**
 * Deletes mapping between layout id and layout descriptor from 'layout-descr'
 * meta-index.
 */
M0_INTERNAL int m0_dix_ldescr_del(struct m0_dix_meta_req *req,
				  const uint64_t         *lid,
				  uint32_t                nr);

/**
 * Stores layouts for distributed indices with the given fids.
 *
 * It puts "index fid":"index layout" pairs to "layout" meta-index.
 */
M0_INTERNAL int m0_dix_layout_put(struct m0_dix_meta_req     *req,
				  const struct m0_fid        *fid,
				  const struct m0_dix_layout *dlay,
				  uint32_t                    nr);

/**
 * Retrieves layouts for distributed indices with the given fids.
 *
 * It can also be used to check for existence of the distributed indices.
 * Once operation is finished, retrieved layouts are accessible via
 * m0_dix_layout_rep_get().
 */
M0_INTERNAL int m0_dix_layout_get(struct m0_dix_meta_req *req,
				  const struct m0_fid    *fid,
				  uint32_t                nr);

/**
 * Deletes layouts for distributed indices with the given fids.
 *
 * It deletes records from "layout" meta-index.
 */
M0_INTERNAL int m0_dix_layout_del(struct m0_dix_meta_req *req,
				  const struct m0_fid    *fid,
				  uint32_t                nr);

/**
 * Returns the 'idx'-th layout retrieved by m0_dix_layout_get().
 *
 * 'dlay' may be NULL if user is interested only in return code without copying
 * the layout, e.g. to check that the index exists.
 */
M0_INTERNAL int m0_dix_layout_rep_get(struct m0_dix_meta_req *req,
				      uint64_t                idx,
				      struct m0_dix_layout   *dlay);

M0_INTERNAL int m0_dix_index_list(struct m0_dix_meta_req *req,
				  const struct m0_fid    *start_fid,
				  uint32_t                indices_nr);

M0_INTERNAL int m0_dix_index_list_rep_nr(struct m0_dix_meta_req *req);

M0_INTERNAL int m0_dix_index_list_rep(struct m0_dix_meta_req *req,
				      uint32_t                idx,
				      struct m0_fid          *fid);

M0_INTERNAL int m0_dix__meta_val_enc(const struct m0_fid       *fid,
				     const struct m0_dix_ldesc *dld,
				     uint32_t                   nr,
				     struct m0_bufvec          *vals);

M0_INTERNAL int m0_dix__meta_val_dec(const struct m0_bufvec *vals,
				     struct m0_fid          *out_fid,
				     struct m0_dix_ldesc    *out_dld,
				     uint32_t                nr);

/**
 * Encoded data saves to key and val.
 * User must call m0_bufvec_free for bufvecs when those are not necessary.
 */
M0_INTERNAL int m0_dix__ldesc_vals_enc(const uint64_t            *lid,
				       const struct m0_dix_ldesc *ldesc,
				       uint32_t                   nr,
				       struct m0_bufvec          *keys,
				       struct m0_bufvec          *vals);

/**
 * Decodes key and val bufvecs and returns lid and dix_ldesc objects.
 * User must call m0_dix_ldesc_fini() for returned object out_ldesc
 * when it is not necessary.
 */
M0_INTERNAL int m0_dix__ldesc_vals_dec(const struct m0_bufvec *keys,
				       const struct m0_bufvec *vals,
				       uint64_t               *out_lid,
				       struct m0_dix_ldesc    *out_ldesc,
				       uint32_t                nr);

/**
 *  Encoded data saves to key and val.
 *  User must call m0_bufvec_free() for bufvecs when those are not necessary.
 */

M0_INTERNAL int m0_dix__layout_vals_enc(const struct m0_fid        *fid,
					const struct m0_dix_layout *dlay,
					uint32_t                    nr,
					struct m0_bufvec           *keys,
					struct m0_bufvec           *vals);

/**
 * Decodes key and val bufvecs and returns fid and dix_layout object.
 * Returned value out_dlay after decoding will contains dix_ldesc object.
 * User must destroy the object when it is not necessary.
 */
M0_INTERNAL int m0_dix__layout_vals_dec(const struct m0_bufvec *keys,
				        const struct m0_bufvec *vals,
				        struct m0_fid          *out_fid,
				        struct m0_dix_layout   *out_dlay,
				        uint32_t                nr);

/** @} end of dix group */

#endif /* __MERO_DIX_META_H__ */

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
