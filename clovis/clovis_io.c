/* -*- C -*- */
/*
 * COPYRIGHT 2016 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original authors: Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Revision:         Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 3-Nov-2014
 *
 * Original 'm0t1fs' author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 */

#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_addb.h"
#include "clovis/pg.h"
#include "clovis/io.h"

#include "lib/errno.h"             /* ENOMEM */
#include "fid/fid.h"               /* m0_fid */
#include "ioservice/fid_convert.h" /* m0_fid_convert_ */
#include "rm/rm_service.h"         /* m0_rm_svc_domain_get */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"             /* M0_LOG */

#define DGMODE_IO

/**
 * A note for rwlock.
 *
 * As stated in clovis.h: "All other concurrency control, including ordering
 * of reads and writes to a clovis object, and distributed transaction
 * serializability, is up to the application.", rwlock related code is removed
 * from Clovis (by Sining). And rwlock related commits are ignored as well,
 * such as commit d3c06f4f. If rwlock is thought to be necessary for Clovis
 * later, it will be added.
 */

/** Resource Manager group id, copied from m0t1fs */
const struct m0_uint128 m0_rm_clovis_group = M0_UINT128(0, 1);

/**
 * This is heavily based on m0t1fs/linux_kernel/inode.c::m0t1fs_rm_domain_get
 */
M0_INTERNAL struct m0_rm_domain *
clovis_rm_domain_get(struct m0_clovis *instance)
{
	struct m0_reqh_service *svc;

	M0_ENTRY();

	M0_PRE(instance != NULL);

	svc = m0_reqh_service_find(&m0_rms_type, &instance->m0c_reqh);
	M0_ASSERT(svc != NULL);

	M0_LEAVE();
	return m0_rm_svc_domain_get(svc);
}

M0_INTERNAL struct m0_poolmach*
clovis_ioo_to_poolmach(struct m0_clovis_op_io *ioo)
{
	struct m0_pool_version *pv;
	struct m0_clovis       *instance;

	instance = m0_clovis__op_instance(&ioo->ioo_oo.oo_oc.oc_op);
	pv = m0_pool_version_find(&instance->m0c_pools_common,
				  &ioo->ioo_pver);
	return &pv->pv_mach;
}

/**
 * Sort the elements in the indexvector into index order ... using bubble sort.
 * This is heavily based on m0t1fs/linux_kernel/file.c::indexvec_sort
 *
 * @param ivec[out] The index vector to operate over.
 */
static void indexvec_sort(struct m0_indexvec *ivec)
{
	uint32_t i;
	uint32_t j;

	M0_ENTRY("indexvec = %p", ivec);

	M0_PRE(ivec != NULL);
	M0_PRE(!m0_vec_is_empty(&ivec->iv_vec));

	/*
	 * TODO Should be replaced by an efficient sorting algorithm,
	 * something like heapsort which is fairly inexpensive in kernel
	 * mode with the least worst case scenario.
	 * Existing heap sort from kernel code can not be used due to
	 * apparent disconnect between index vector and its associated
	 * count vector for same index.
	 */
	for (i = 0; i < SEG_NR(ivec); ++i) {
		for (j = i+1; j < SEG_NR(ivec); ++j) {
			if (INDEX(ivec, i) > INDEX(ivec, j)) {
				M0_SWAP(INDEX(ivec, i), INDEX(ivec, j));
				M0_SWAP(COUNT(ivec, i), COUNT(ivec, j));
			}
		}
	}

	M0_LEAVE();
}

M0_INTERNAL bool m0_clovis_op_io_invariant(const struct m0_clovis_op_io *ioo)
{
	bool rc;

	M0_ENTRY();

	if (ioo == NULL)
		rc = false;
	else if (!m0_clovis_op_io_bob_check(ioo))
		rc = false;

	/* ioo is big enough */
	else if(ioo->ioo_oo.oo_oc.oc_op.op_size < sizeof *ioo)
		rc = false;

	/* is a supported type */
	else if(!M0_IN(ioo->ioo_oo.oo_oc.oc_op.op_code,
		      (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)))
		rc = false;

	/* read/write extent is for an area with a size */
	else if(m0_vec_count(&ioo->ioo_ext.iv_vec) <= 0)
		rc = false;

	/* read/write extent is a multiple of block size */
	else if(m0_vec_count(&ioo->ioo_ext.iv_vec) %
			(1ULL << ioo->ioo_obj->ob_attr.oa_bshift) != 0)
		rc = false;

	/* memory area for read/write is the same size as the extents */
	else if(ergo(M0_IN(ioo->ioo_oo.oo_oc.oc_op.op_code,
			   (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)),
		      m0_vec_count(&ioo->ioo_ext.iv_vec) !=
				m0_vec_count(&ioo->ioo_data.ov_vec)))
		rc = false;

#ifdef BLOCK_ATTR_SUPPORTED /* Block attr not yet enabled. */
	/* memory area for attribute read/write is big enough */
	else if(ergo(M0_IN(ioo->ioo_oo.oo_oc.oc_op.op_code,
			   (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)),
		     m0_vec_count(&ioo->ioo_attr.ov_vec) !=
		     8 * m0_no_of_bits_set(ioo->ioo_attr_mask) *
		     (m0_vec_count(&ioo->ioo_ext.iv_vec) >>
		      ioo->ioo_obj->ob_attr.oa_bshift)))
		rc = false;

#endif
	/* alloc/free don't have a memory area  */
	else if(!ergo(M0_IN(ioo->ioo_oo.oo_oc.oc_op.op_code,
			   (M0_CLOVIS_OC_ALLOC, M0_CLOVIS_OC_FREE)),
		     M0_IS0(&ioo->ioo_data) && M0_IS0(&ioo->ioo_attr) &&
		     ioo->ioo_attr_mask == 0))
		rc = false;

	else if(!m0_fid_is_valid(&ioo->ioo_oo.oo_fid))
		rc = false;

	/* a network transfer machine is registered for read/write */
	else if(!ergo(M0_IN(ioreq_sm_state(ioo), (IRS_READING, IRS_WRITING)),
			   !tioreqht_htable_is_empty(
			   &ioo->ioo_nwxfer.nxr_tioreqs_hash)))
		rc = false;

	/* if finished, there are no fops left waiting */
	else if(!ergo(M0_IN(ioreq_sm_state(ioo),
			   (IRS_WRITE_COMPLETE, IRS_READ_COMPLETE)),
			m0_atomic64_get(&ioo->ioo_nwxfer.nxr_iofop_nr) == 0 &&
			m0_atomic64_get(&ioo->ioo_nwxfer.nxr_rdbulk_nr) == 0))
		rc = false;
	else if(!pargrp_iomap_invariant_nr(ioo))
		rc = false;
	else if(!nw_xfer_request_invariant(&ioo->ioo_nwxfer))
		rc = false;
	else
		rc = true;

	return M0_RC(rc);
}

/**
 * Callback for an IO operation being launched.
 * Prepares io maps and distributes the operations in the network transfer.
 * Schedules an AST to acquire the resource manager file lock.
 *
 * @param oc The common callback struct for the operation being launched.
 */
static void clovis_obj_io_cb_launch(struct m0_clovis_op_common *oc)
{
	int                      rc;
	struct m0_clovis        *instance;
	struct m0_clovis_op     *op;
	struct m0_clovis_op_obj *oo;
	struct m0_clovis_op_io  *ioo;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	M0_PRE(oc->oc_op.op_entity != NULL);
	M0_PRE(m0_uint128_cmp(&M0_CLOVIS_ID_APP,
				     &oc->oc_op.op_entity->en_id) < 0);
	M0_PRE(M0_IN(oc->oc_op.op_code,
			    (M0_CLOVIS_OC_WRITE, M0_CLOVIS_OC_READ)));
	M0_PRE(oc->oc_op.op_size >= sizeof *ioo);

	op = &oc->oc_op;
	oo = bob_of(oc, struct m0_clovis_op_obj, oo_oc, &oo_bobtype);
	ioo = bob_of(oo, struct m0_clovis_op_io, ioo_oo, &ioo_bobtype);
	M0_PRE_EX(m0_clovis_op_io_invariant(ioo));
	instance = m0_clovis__op_instance(op);
	M0_PRE(instance != NULL);

	rc = ioo->ioo_ops->iro_iomaps_prepare(ioo);
	if (rc != 0) {
		goto end;
	}

	rc = ioo->ioo_nwxfer.nxr_ops->nxo_distribute(&ioo->ioo_nwxfer);
	if (rc != 0) {
		ioo->ioo_ops->iro_iomaps_destroy(ioo);
		ioo->ioo_nwxfer.nxr_state = NXS_COMPLETE;
		goto end;
	}

	/*
	 * This walks through the iomap looking for a READOLD/READREST slot.
	 * Updating ioo_map_idx to indicate where ioreq_iosm_handle_launch
	 * should start. This behaviour is replicated from
	 * m0t1fs:ioreq_iosm_handle.
	 */
	for (ioo->ioo_map_idx = 0; ioo->ioo_map_idx < ioo->ioo_iomap_nr;
	     ++ioo->ioo_map_idx) {
		if (M0_IN(ioo->ioo_iomaps[ioo->ioo_map_idx]->pi_rtype,
			(PIR_READOLD, PIR_READREST)))
			break;
	}

	ioo->ioo_ast.sa_cb = ioo->ioo_ops->iro_iosm_handle_launch;
	m0_sm_ast_post(ioo->ioo_oo.oo_sm_grp, &ioo->ioo_ast);

end:
	M0_LEAVE();
}

/**
 * AST callback for op_fini on an IO operation.
 * This does the work for freeing iofops et al, as it must be done from the
 * AST that performed the IO work.
 *
 * @param grp The (locked) state machine group for this ast.
 * @param ast The ast descriptor, embedded in an m0_clovis_op_io.
 */
static void clovis_obj_io_ast_fini(struct m0_sm_group *grp,
				     struct m0_sm_ast *ast)
{
	struct m0_clovis_op_io *ioo;
	struct target_ioreq    *ti;

	M0_ENTRY();

	M0_PRE(ast != NULL);
	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	ioo = bob_of(ast, struct m0_clovis_op_io, ioo_ast, &ioo_bobtype);
	M0_PRE_EX(m0_clovis_op_io_invariant(ioo));
	/* XXX Shouldn't this be a controlled rc? */
	M0_PRE(M0_IN(ioo->ioo_sm.sm_state,
		     (IRS_REQ_COMPLETE, IRS_FAILED, IRS_INITIALIZED)));

	/* Cleanup the state machine */
	m0_sm_fini(&ioo->ioo_sm);

	/* Free all the iorequests */
	m0_htable_for(tioreqht, ti, &ioo->ioo_nwxfer.nxr_tioreqs_hash) {
		tioreqht_htable_del(&ioo->ioo_nwxfer.nxr_tioreqs_hash, ti);
		/*
		 * All ioreq_fop structures in list target_ioreq::ti_iofops
		 * are already finalized in nw_xfer_req_complete().
		 */
		target_ioreq_fini(ti);
	} m0_htable_endfor;

	/* Free memory used for io maps and buffers */
	if (ioo->ioo_iomaps != NULL) {
		ioo->ioo_ops->iro_iomaps_destroy(ioo);
	}

	nw_xfer_request_fini(&ioo->ioo_nwxfer);
	m0_layout_instance_fini(ioo->ioo_oo.oo_layout_instance);
	m0_chan_signal_lock(&ioo->ioo_completion);
	m0_free(ioo->ioo_failed_session);

	M0_LEAVE();
}

/**
 * Callback for an IO operation being finalised.
 * This causes iofops et al to be freed.
 *
 * @param oc The common callback struct for the operation being finialised.
 */
static void clovis_obj_io_cb_fini(struct m0_clovis_op_common *oc)
{
	struct m0_clovis_op_obj *oo;
	struct m0_clovis_op_io  *ioo;
	struct m0_clink          w;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	M0_PRE(M0_IN(oc->oc_op.op_code,
		     (M0_CLOVIS_OC_WRITE, M0_CLOVIS_OC_READ)));
	M0_PRE(M0_IN(oc->oc_op.op_sm.sm_state,
		     (M0_CLOVIS_OS_STABLE, M0_CLOVIS_OS_FAILED,
		      M0_CLOVIS_OS_INITIALISED)));
	M0_PRE(oc->oc_op.op_size >= sizeof *ioo);

	oo = bob_of(oc, struct m0_clovis_op_obj, oo_oc, &oo_bobtype);
	ioo = bob_of(oo, struct m0_clovis_op_io, ioo_oo, &ioo_bobtype);
	M0_PRE_EX(m0_clovis_op_io_invariant(ioo));

	/* Finalise the io state machine */
	/* We do this by posting the fini callback AST, and waiting for it
	 * to complete */
	m0_clink_init(&w, NULL);
	m0_clink_add_lock(&ioo->ioo_completion, &w);

	ioo->ioo_ast.sa_cb = clovis_obj_io_ast_fini;
	m0_sm_ast_post(ioo->ioo_oo.oo_sm_grp, &ioo->ioo_ast);

	m0_chan_wait(&w);
	m0_clink_del_lock(&w);
	m0_clink_fini(&w);
	m0_chan_fini_lock(&ioo->ioo_completion);

	/* Finalise the bob type */
	m0_clovis_op_io_bob_fini(ioo);

	M0_LEAVE();
}

/**
 * 'free entry' on the vtable for obj io operations. This callback gets
 * invoked when freeing an operation.
 *
 * @param oc operation being freed. Note the operation is of type
 * m0_clovis_op_common although it should have been allocated as a
 * m0_clovis_op_io.
 */
static void clovis_obj_io_cb_free(struct m0_clovis_op_common *oc)
{
	struct m0_clovis_op_obj *oo;
	struct m0_clovis_op_io  *ioo;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	M0_PRE((oc->oc_op.op_size >= sizeof *ioo));

	/* Can't use bob_of here */
	oo = M0_AMB(oo, oc, oo_oc);
	ioo = M0_AMB(ioo, oo, ioo_oo);

	m0_free(ioo);

	M0_LEAVE();
}

void m0_clovis_obj_op(struct m0_clovis_obj       *obj,
		      enum m0_clovis_obj_opcode   opcode,
		      struct m0_indexvec         *ext,
		      struct m0_bufvec           *data,
		      struct m0_bufvec           *attr,
		      uint64_t                    mask,
		      struct m0_clovis_op       **op)
{
	int                         rc;
	int                         i;
	uint64_t                    max_failures;
	uint64_t                    layout_id;
	struct m0_clovis_op_io     *ioo;
	struct m0_clovis_op_obj    *oo;
	struct m0_clovis_op_common *oc;
	struct m0_clovis_entity    *entity;
	struct m0_locality         *locality;
	struct m0_clovis           *instance;
	struct m0_pool_version     *pv;

	M0_ENTRY();

	M0_PRE(obj != NULL);
	M0_PRE(M0_IN(opcode, (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)));
	M0_PRE(ext != NULL);
	M0_PRE(obj->ob_attr.oa_bshift >=  CLOVIS_MIN_BUF_SHIFT);
	M0_PRE(m0_vec_count(&ext->iv_vec) %
		      (1ULL << obj->ob_attr.oa_bshift) == 0);
	M0_PRE(op != NULL);
	M0_PRE(ergo(M0_IN(opcode, (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)),
		    data != NULL && attr != NULL &&
		    m0_vec_count(&ext->iv_vec) == m0_vec_count(&data->ov_vec)));
#ifdef BLOCK_ATTR_SUPPORTED /* Block metadata is not yet supported */
	M0_PRE(m0_vec_count(&attr->ov_vec) ==
	       (8 * m0_no_of_bits_set(mask) *
		(m0_vec_count(&ext->iv_vec) >> obj->ob_attr.oa_bshift)));
#endif
	M0_PRE(ergo(M0_IN(opcode, (M0_CLOVIS_OC_ALLOC, M0_CLOVIS_OC_FREE)),
		    data == NULL && attr == NULL && mask == 0));

	/* Block metadata is not yet supported */
	M0_PRE(mask == 0);

	entity = &obj->ob_entity;
	instance = m0_clovis__entity_instance(entity);

	/* Allocate the operation */
	if (*op == NULL) {
		rc = m0_clovis_op_alloc(op, sizeof *ioo);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Unable to initialise the operation: %d.",
			       rc);

			/*
			 * in this case the user is expected to interpret
			 * op==NULL as ENOMEM
			 */
			return;
		}
	} else {
		size_t cached_size = (*op)->op_size;
		if ((*op)->op_size < sizeof *ioo) {
			M0_LOG(M0_ERROR, "Provided buffer too small.");
			rc = -EMSGSIZE;
			/* XXX juan: we cannot assume (*op)->op_sm is init'ed */
			/* XXX morse: then we can't trust the value, and can't
			 *            ever return EMSGSIZE ... so operations
			 *            can never be re-used.... a compromise is
			 *            required */

			goto fail;
		}

		/* 0 the pre-allocated operation */
		memset(*op, 0, cached_size);
		(*op)->op_size = cached_size;
	}
	m0_mutex_init(&(*op)->op_pending_tx_lock);
	spti_tlist_init(&(*op)->op_pending_tx);

	/*
	 * Sanity test before proceeding.
	 * Note: Can't use bob_of at this point as oc/oo/ioo haven't been
	 * initilised yet.
	 */
	M0_ASSERT((*op)->op_size >= sizeof *ioo);
	oc = M0_AMB(oc, *op, oc_op);
	oo = M0_AMB(oo, oc, oo_oc);
	ioo = M0_AMB(ioo, oo, ioo_oo);

	/* Initialise the operation */
	rc = m0_clovis_op_init(*op, &clovis_op_conf, entity);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Unable to initialise the operation.");
		/* XXX juan: if this fails we cannot try to move the state */
		goto fail;
	} else
		(*op)->op_code = opcode;

	ioo->ioo_obj = obj;
	ioo->ioo_ops = &ioo_ops;

	/** TODO: hash the fid to chose a locality */
	locality = m0_clovis_locality_pick(instance);
	M0_ASSERT(locality != NULL);

	/* Initalise the vtable */
	switch (opcode) {
	case M0_CLOVIS_OC_READ:
	case M0_CLOVIS_OC_WRITE:
		/* General entries */
		oc->oc_cb_launch = clovis_obj_io_cb_launch;
		oc->oc_cb_fini = clovis_obj_io_cb_fini;
		oc->oc_cb_free = clovis_obj_io_cb_free;

		break;
	default:
		M0_IMPOSSIBLE("Not implememented yet");
		break;
	}

	/* Initialise this object as a 'bob' */
	m0_clovis_op_common_bob_init(oc);
	m0_clovis_op_obj_bob_init(oo);
	m0_clovis_op_io_bob_init(ioo);

	/* Convert the clovis:object-id to a mero:fid */
	m0_fid_gob_make(&ioo->ioo_oo.oo_fid,
			obj->ob_entity.en_id.u_hi, obj->ob_entity.en_id.u_lo);

	/* Get current pool version for the object. */
	rc = m0_clovis__pool_version_get(instance, &pv);
	if (rc != 0)
		goto fail;
	ioo->ioo_pver = pv->pv_id;

	/*
	 * TODO: Build layout instance: current implementation of Clovis
	 * doesn't retrieve latest latest layout id of an object (and it
	 * doesn't even have an API to change layout id).
	 */
	layout_id = m0_pool_version2layout_id(&ioo->ioo_pver,
			m0_clovis__obj_layout_id_get(oo));
	rc = m0_clovis__obj_layout_instance_build(instance, layout_id,
						  &oo->oo_fid,
						  &oo->oo_layout_instance);
	if (rc != 0)
		goto fail;

	/* Initialise this operation as a network transfer */
	nw_xfer_request_init(&ioo->ioo_nwxfer);
	if (ioo->ioo_nwxfer.nxr_rc != 0) {
		rc = ioo->ioo_nwxfer.nxr_rc;;
		M0_LOG(M0_ERROR, "nw_xfer_req_init() failed: %d", rc);
		goto fail;
	}

	/* Allocate and initialise failed sessions. */
	max_failures = tolerance_of_level(ioo, M0_CONF_PVER_LVL_CTRLS);
	M0_ALLOC_ARR(ioo->ioo_failed_session, max_failures + 1);
	if (ioo->ioo_failed_session == NULL) {
		M0_LOG(M0_ERROR, "Allocation of an array of failed sessions.");
		goto fail;
	}
	for (i = 0; i < max_failures; ++i) {
		ioo->ioo_failed_session[i] = ~(uint64_t)0;
	}

	/* Initialise the state machine */
	ioo->ioo_oo.oo_sm_grp = locality->lo_grp;
	m0_sm_init(&ioo->ioo_sm, &io_sm_conf, IRS_INITIALIZED,
		   locality->lo_grp);

	/* This is used to wait for the ioo to be finalised */
	m0_chan_init(&ioo->ioo_completion, &instance->m0c_sm_group.s_lock);

	/* Sorts the index vector in increasing order of file offset. */
	indexvec_sort(ext);

	/* Store the remaining parameters */
	ioo->ioo_iomap_nr = 0;
	ioo->ioo_sns_state = SRS_UNINITIALIZED;
	ioo->ioo_ext = *ext;
	if (M0_IN(opcode, (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE))) {
		ioo->ioo_data = *data;
		ioo->ioo_attr = *attr;
		ioo->ioo_attr_mask = mask;
	}

	M0_POST_EX(m0_clovis_op_io_invariant(ioo));
	M0_POST(*op != NULL &&
		(*op)->op_code == opcode &&
		(*op)->op_sm.sm_state == M0_CLOVIS_OS_INITIALISED);

	M0_LEAVE();
	return;

fail:
	m0_sm_group_lock(&(*op)->op_sm_group);
	m0_sm_fail(&(*op)->op_sm, M0_CLOVIS_OS_FAILED, rc);
	m0_clovis_op_failed(*op);
	m0_sm_group_unlock(&(*op)->op_sm_group);

	M0_LEAVE();
	return;
}
M0_EXPORTED(m0_clovis_obj_op);

/**
 * Initialisation for object io operations.
 * This initialises certain list types.
 * This is heavily based on m0t1fs/linux_kernel/file.c::io_bob_tlists_init
 */
M0_INTERNAL void m0_clovis_init_io_op(void)
{
	M0_ASSERT(tioreq_bobtype.bt_magix == M0_CLOVIS_TIOREQ_MAGIC);
	m0_bob_type_tlist_init(&iofop_bobtype, &iofops_tl);
	M0_ASSERT(iofop_bobtype.bt_magix == M0_CLOVIS_IOFOP_MAGIC);
}

#undef M0_TRACE_SUBSYSTEM

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
