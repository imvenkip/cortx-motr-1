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
 * Original creation date: 5-Oct-2014
 */

#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"
#include "clovis/osync.h"

#include "lib/errno.h"
#include "fid/fid.h"               /* m0_fid */
#include "lib/locality.h"          /* m0_locality_here() */
#include "ioservice/fid_convert.h" /* m0_fid_convert_ */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"
#include "lib/finject.h"

#ifdef CLOVIS_FOR_M0T1FS
/**
 * Maximum length for an object's name.
 */
enum {
	OBJ_NAME_MAX_LEN = 64
};
#endif

M0_INTERNAL struct m0_clovis*
m0_clovis__oo_instance(struct m0_clovis_op_obj *oo)
{
	M0_PRE(oo != NULL);

	return m0_clovis__entity_instance(oo->oo_oc.oc_op.op_entity);
}

/**
 * Checks the common part of an operation is not malformed or corrupted.
 *
 * @param oc operation common to be checked.
 * @return true if the operation is not malformed or false if some error
 * was detected.
 */
static bool clovis_op_common_invariant(struct m0_clovis_op_common *oc)
{
	return M0_RC(oc != NULL &&
		     m0_clovis_op_common_bob_check(oc));
}

M0_INTERNAL bool m0_clovis_op_obj_ast_rc_invariant(struct m0_clovis_ast_rc *ar)
{
	return M0_RC(ar != NULL &&
		     m0_clovis_ast_rc_bob_check(ar));
}

M0_INTERNAL bool m0_clovis_op_obj_invariant(struct m0_clovis_op_obj *oo)
{
	return M0_RC(oo != NULL &&
		     m0_clovis_op_obj_bob_check(oo) &&
		     oo->oo_oc.oc_op.op_size >= sizeof *oo &&
		     m0_clovis_op_obj_ast_rc_invariant(&oo->oo_ar) &&
		     clovis_op_common_invariant(&oo->oo_oc));
}

/**
 * Checks an object operation is not malformed or corrupted.
 *
 * @param oo object operation to be checked.
 * @return true if the operation is not malformed or false if some error
 * was detected.
 */
static bool clovis_obj_op_obj_invariant(struct m0_clovis_op_obj *oo)
{
	bool rc;

	/* Don't use a m0_clovis__xxx_instance, as they assert */
	if(oo == NULL)
		rc = false;
	else if(oo->oo_oc.oc_op.op_entity == NULL)
		rc = false;
	else if(oo->oo_oc.oc_op.op_entity->en_realm == NULL)
		rc = false;
	else if(oo->oo_oc.oc_op.op_entity->en_realm->re_instance == NULL)
		rc = false;
	else
		rc = true;

	return M0_RC(rc);
}

/*
 * Pick a locality: Mero and the new locality interface(chore) now uses TLS to
 * store data and these data are set when a "mero" thread is created.
 * An application thread (not the main thread calling m0_init, considering ST
 * multi-threading framework), it doesn't have the same TLS by nature, which
 * causes a problem when it calls mero functions like m0_locality_here/get
 * directly as below.
 *
 * Ensure to use m0_thread_adopt/shun to make a thread (non-)meroism when a
 * thread starts/ends.
 *
 * TODO: more intelligent locality selection policy based on fid and workload.
 */
M0_INTERNAL struct m0_locality *m0_clovis_locality_pick(struct m0_clovis *cinst)
{
	return  m0_locality_here();
}

/**
 * 'launch entry' on the vtable for obj namespace manipulation. This clovis
 * callback gets invoked when launching a create/delete object operation.
 *
 * @param oc operation being launched. Note the operation is of type
 * m0_clovis_op_common although it has to be allocated as a
 * m0_clovis_op_obj.
 */
static void clovis_obj_namei_cb_launch(struct m0_clovis_op_common *oc)
{
	int                      rc;
	struct m0_clovis_op_obj *oo;
	struct m0_clovis_op     *op;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	op = &oc->oc_op;
	M0_PRE(op->op_entity != NULL);
	M0_PRE(m0_uint128_cmp(&M0_CLOVIS_ID_APP,
			      &op->op_entity->en_id) < 0);
	M0_PRE(M0_IN(op->op_code, (M0_CLOVIS_EO_CREATE,
				   M0_CLOVIS_EO_DELETE)));
	M0_PRE(op->op_entity->en_sm.sm_state == M0_CLOVIS_ES_INIT);
	M0_PRE(m0_sm_group_is_locked(&op->op_sm_group));
	M0_ASSERT(clovis_entity_invariant_full(op->op_entity));

	oo = bob_of(oc, struct m0_clovis_op_obj, oo_oc, &oo_bobtype);
	M0_PRE(clovis_obj_op_obj_invariant(oo));

	/* Move to a different state and call the control function. */
	m0_sm_group_lock(&op->op_entity->en_sm_group);
	switch (op->op_code) {
		case M0_CLOVIS_EO_CREATE:
			m0_sm_move(&op->op_entity->en_sm, 0,
				   M0_CLOVIS_ES_CREATING);
			break;
		case M0_CLOVIS_EO_DELETE:
			m0_sm_move(&op->op_entity->en_sm, 0,
				   M0_CLOVIS_ES_DELETING);
			break;
		default:
			M0_IMPOSSIBLE("Management operation not implemented");
	}
	m0_sm_group_unlock(&op->op_entity->en_sm_group);

	rc = m0_clovis_cob_send(oo);
	if (rc != 0)
		goto out;

	m0_sm_move(&op->op_sm, 0, M0_CLOVIS_OS_LAUNCHED);
out:
	M0_LEAVE();
}

/**
 * 'free entry' on the vtable for obj namespace manipulation. This callback
 * gets invoked when freeing an operation.
 *
 * @param oc operation being freed. Note the operation is of type
 * m0_clovis_op_common although it has to be allocated as a
 * m0_clovis_op_obj.
 */
static void clovis_obj_namei_cb_free(struct m0_clovis_op_common *oc)
{
	struct m0_clovis_op_obj *oo;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	M0_PRE((oc->oc_op.op_size >= sizeof *oo));

	/* By now, fini() has been called and bob_of cannot be used */
	oo = container_of(oc, struct m0_clovis_op_obj, oo_oc);
	M0_PRE(clovis_obj_op_obj_invariant(oo));

	m0_free(oo);

	M0_LEAVE();
}

/**
 * 'op fini entry' on the vtable for entities. This clovis callback gets invoked
 * when a create/delete operation on an object gets finalised.
 *
 * @param oc operation being finalised. Note the operation is of type
 * m0_clovis_op_common although it has to be allocated as a
 * m0_clovis_op_obj.
 */
static void clovis_obj_namei_cb_fini(struct m0_clovis_op_common *oc)
{
	struct m0_clovis_op_obj *oo;
	struct m0_clovis        *cinst;
	uint32_t                 icr_nr;
	int                      i;

	M0_ENTRY();

	M0_PRE(oc != NULL);
	M0_PRE(oc->oc_op.op_size >= sizeof *oo);

	oo = bob_of(oc, struct m0_clovis_op_obj, oo_oc, &oo_bobtype);
	M0_PRE(m0_clovis_op_obj_invariant(oo));
	icr_nr = oo->oo_icr_nr;

	cinst = m0_clovis__oo_instance(oo);
	M0_ASSERT(cinst != NULL);

	/* XXX: Can someone else access oo-> fields? do we need to lock? */
	/* Release the mds fop. */
	if (oo->oo_mds_fop != NULL) {
		m0_fop_put_lock(oo->oo_mds_fop);
		oo->oo_mds_fop = NULL;
	}

	/* Release the ios fops. */
	if (oo->oo_ios_fop != NULL) {
		for (i = 0; i < icr_nr; ++i) {
			if (oo->oo_ios_fop[i] != NULL) {
				m0_clovis_cob_ios_fop_fini(oo->oo_ios_fop[i]);
				oo->oo_ios_fop[i] = NULL;
			}
		}
		m0_free(oo->oo_ios_fop);
		oo->oo_ios_fop = NULL;
	}

	if (oo->oo_ios_completed != NULL) {
		m0_free(oo->oo_ios_completed);
		oo->oo_ios_completed = NULL;
	}

	if (oo->oo_layout_instance != NULL) {
		m0_layout_instance_fini(oo->oo_layout_instance);
		oo->oo_layout_instance = NULL;
	}

	m0_clovis_op_common_bob_fini(&oo->oo_oc);
	m0_clovis_ast_rc_bob_fini(&oo->oo_ar);
	m0_clovis_op_obj_bob_fini(oo);

	M0_SET0(&oo->oo_fid);

#ifdef CLOVIS_FOR_M0T1FS
	M0_SET0(&oo->oo_pfid);
	m0_buf_free(&oo->oo_name);
#endif

	M0_LEAVE();
}

M0_INTERNAL int
m0_clovis_pool_version_get(struct m0_clovis *instance,
			   struct m0_pool_version **pv)
{
	int rc;

	if (M0_FI_ENABLED("fake_pool_version")) {
		*pv = instance->m0c_pools_common.pc_cur_pver;
		if (pv == NULL)
			return M0_ERR(-ENOENT);
		return 0;
	}

	/* Get pool version */
	rc = m0_pool_version_get(&instance->m0c_pools_common, pv);
	return M0_RC(rc);
}

M0_INTERNAL uint64_t
m0_clovis_obj_default_layout_id_get(struct m0_clovis *instance)
{
	int                        rc;
	int                        i;
	uint64_t                   lid;
	const uint64_t             FS_LID_INDEX = 1;
	struct m0_reqh            *reqh = &instance->m0c_reqh;
	struct m0_conf_filesystem *fs;

	M0_ENTRY();
	M0_PRE(instance != NULL);

	if (M0_FI_ENABLED("return_default_layout"))
		return M0_DEFAULT_LAYOUT_ID;

	/*
	 * TODO:This layout selection is a temporary solution for s3 team
	 * requirement. In future this has to be replaced by more sophisticated
	 * solution.
	 */
	if (instance->m0c_config->cc_layout_id)
		return instance->m0c_config->cc_layout_id;

	rc = m0_conf_fs_get(&reqh->rh_profile, m0_reqh2confc(reqh), &fs);
	if (rc != 0)
		goto EXIT;

	if (fs->cf_params == NULL)
		M0_LOG(M0_WARN, "fs->cf_params == NULL");

	for (i = 0;
	    fs->cf_params != NULL && fs->cf_params[i] != NULL; ++i) {
		M0_LOG(M0_DEBUG, "param(%d): %s", i, fs->cf_params[i]);
		if (i == FS_LID_INDEX) {
#ifdef __KERNEL__
			lid = simple_strtoul(fs->cf_params[i], NULL, 0);
#else
			lid = strtoul(fs->cf_params[i], NULL, 0);
#endif
			M0_LOG(M0_DEBUG, "fetched layout id: %s, %llu",
			       fs->cf_params[i], (unsigned long long)lid);

			M0_LEAVE();
			m0_confc_close(&fs->cf_obj);
			return lid;
		}
	}

EXIT:
	M0_LEAVE();
	return M0_DEFAULT_LAYOUT_ID;
}

M0_INTERNAL int
m0_clovis_obj_layout_instance_build(struct m0_clovis *cinst,
				    const uint64_t layout_id,
				    const struct m0_fid *fid,
				    struct m0_layout_instance **linst)
{
	int                     rc = 0;
	struct m0_layout       *layout;

	M0_PRE(cinst != NULL);
	M0_PRE(linst != NULL);
	M0_PRE(fid != NULL);

	/*
	 * All the layouts should already be generated on startup and added
	 * to the list unless wrong layout_id is used.
	 */
	layout = m0_layout_find(&cinst->m0c_reqh.rh_ldom, layout_id);
	if (layout == NULL) {
		rc = -EINVAL;
		goto out;
	}

	*linst = NULL;
	rc = m0_layout_instance_build(layout, fid, linst);
	m0_layout_put(layout);

out:
	M0_LEAVE("rc: %d", rc);
	return M0_RC(rc);
}

#ifdef CLOVIS_FOR_M0T1FS
/**
 * Generates a name for an object from its fid.
 *
 * @param name buffer where the name is stored.
 * @param name_len length of the name buffer.
 * @param fid fid of the object.
 * @return 0 if the name was correctly generated or -EINVAL otherwise.
 */
static int clovis_obj_fid_make_name(char *name, size_t name_len,
				    struct m0_fid *fid)
{
	int rc;

	M0_PRE(name != NULL);
	M0_PRE(name_len > 0);
	M0_PRE(fid != NULL);

	rc = snprintf(name, name_len, "%"PRIx64":%"PRIx64, FID_P(fid));
	if (rc < 0 || rc >= name_len)
		return M0_ERR(-EINVAL);

	return M0_RC(0);
}
#endif

/**
 * Initialises a m0_clovis_obj namespace operation. The operation is intended
 * to manage in the backend the object the provided entity is associated to.
 *
 * @param entity in-memory representation of the object's entity.
 * @param op operation being set. The operation must have been allocated as a
 * m0_clovis_op_obj.
 * @return 0 if the function completes successfully or an error code otherwise.
 */
static int clovis_obj_namei_op_init(struct m0_clovis_entity *entity,
				    struct m0_clovis_op *op)
{
	int                         rc;
	char                       *obj_name;
	uint64_t                    obj_key;
	uint64_t                    obj_container;
	uint64_t                    layout_id;
	struct m0_clovis_op_obj    *oo;
	struct m0_clovis_op_common *oc;
	struct m0_layout_instance  *linst;
	struct m0_clovis           *cinst;

	M0_ENTRY();

	M0_PRE(entity != NULL);
	M0_PRE(op != NULL);

	if (op->op_size < sizeof *oo) {
		rc = M0_ERR(-EMSGSIZE);
		goto error;
	}

	oc = bob_of(op, struct m0_clovis_op_common, oc_op, &oc_bobtype);
	M0_PRE(clovis_op_common_invariant(oc));
	oo = bob_of(oc, struct m0_clovis_op_obj, oo_oc, &oo_bobtype);
	M0_PRE(m0_clovis_op_obj_invariant(oo));

	cinst = m0_clovis__oo_instance(oo);
	M0_ASSERT(cinst != NULL);

	/* Set the op_common's callbacks. */
	oc->oc_cb_launch = clovis_obj_namei_cb_launch;
	oc->oc_cb_fini = clovis_obj_namei_cb_fini;
	oc->oc_cb_free = clovis_obj_namei_cb_free;

	/*
	 * Set the object's fid.
	 *
	 * Something about the fid at current mero implementation (fid_convert.h)
	 * (1) fid is 128 bits long,  global fid and cob fid both use the highest
	 * 8 bits to represent object type and the lowest 96 bits to store object
	 * id. The interpretion of these 96 bits depends on the users. For
	 * example, as the name of fid::f_container suggests, the 32 bits (or
	 * any number of bits) in f_container can be viewed as 'application
	 * container' id, so supporting multiple application containers is
	 * possible in current Mero implementation.
	 *
	 * (2) The difference of global fid and cob fid is in the 24 bits in
	 * fid::f_container. cob fid uses these 24 bits to store device id in a
	 * layout (md is 0, and devices in ioservices ranges from 1 to P).
	 *
	 * (3) Does Clovis need to check if an object's container id matches
	 * the container id inside its fid?
	 */
	obj_container = entity->en_id.u_hi;
	obj_key = entity->en_id.u_lo;
	m0_fid_gob_make(&oo->oo_fid, obj_container, obj_key);

	/* Get a layout instance for the object. */
	layout_id = m0_pool_version2layout_id(&oo->oo_pver,
			m0_clovis_obj_default_layout_id_get(cinst));
	rc = m0_clovis_obj_layout_instance_build(
			cinst, layout_id, &oo->oo_fid, &linst);
	if (rc != 0)
		goto error;
	oo->oo_layout_instance = linst;

#ifdef CLOVIS_FOR_M0T1FS
	/* Set the object's parent's fid. */
	if (!m0_fid_is_set(&cinst->m0c_root_fid) ||
	    !m0_fid_is_valid(&cinst->m0c_root_fid)) {
		rc = -EINVAL;
		goto error;
	}
	oo->oo_pfid = cinst->m0c_root_fid;

	/* Generate a valid oo_name. */
	obj_name = m0_alloc(OBJ_NAME_MAX_LEN);
	rc = clovis_obj_fid_make_name(obj_name, OBJ_NAME_MAX_LEN, &oo->oo_fid);
	if (rc != 0)
		goto error;
	m0_buf_init(&oo->oo_name, obj_name, strlen(obj_name));
#endif

	M0_ASSERT(rc == 0);
	return M0_RC(rc);
error:
	M0_ASSERT(rc != 0);
	return M0_ERR(rc);
}

/**
 * Initialises a m0_clovis_op_obj (i.e. an operation on an object).
 *
 * @param oo object operation to be initialised.
 * @return 0 if success or an error code otherwise.
 */
static int clovis_obj_op_obj_init(struct m0_clovis_op_obj *oo)
{
	uint32_t                pool_width;
	struct m0_clovis       *cinst;
	struct m0_locality     *locality;
	struct m0_pool_version *pv;

	M0_ENTRY();
	M0_PRE(oo != NULL);

	cinst = m0_clovis__oo_instance(oo);
	M0_ASSERT(cinst != NULL);

	/* Get pool version */
	if (m0_clovis_pool_version_get(cinst, &pv) != 0)
		return M0_ERR(-ENOENT);

	oo->oo_pver = pv->pv_id;
	pool_width = pv->pv_attr.pa_P;
	M0_ASSERT(pool_width > 0);

	/* Init and set FOP related members. */
	oo->oo_mds_fop = NULL;

	M0_ALLOC_ARR(oo->oo_ios_fop, pool_width);
	if (oo->oo_ios_fop == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_ARR(oo->oo_ios_completed, pool_width);
	if (oo->oo_ios_completed == NULL) {
		m0_free(oo->oo_ios_fop);
		return M0_ERR(-ENOMEM);
	}

	/** TODO: hash the fid to chose a locality */
	locality = m0_clovis_locality_pick(cinst);
	M0_ASSERT(locality != NULL);
	oo->oo_sm_grp = locality->lo_grp;
	M0_SET0(&oo->oo_ar);

	oo->oo_layout_instance = NULL;
	M0_SET0(&oo->oo_fid);

#ifdef CLOVIS_FOR_M0T1FS
	M0_SET0(&oo->oo_pfid);
	M0_SET0(&oo->oo_name);
#endif

	m0_clovis_op_obj_bob_init(oo);
	m0_clovis_ast_rc_bob_init(&oo->oo_ar);

	M0_POST(m0_clovis_op_obj_invariant(oo));
	return M0_RC(0);
}

/**
 * Prepares a clovis operation to be executed on an object. Does all the
 * generic stuff common to every operation on objects. Also allocates the
 * operation if it has not been pre-allocated.
 *
 * @param entity Entity of the obj the operation is targeted to.
 * @param[out] op Operation to be prepared. The operation might have been
 * pre-allocated. Otherwise the function allocates extra memory.
 * @param opcode Specific operation code.
 * @return 0 if the operation completes successfully or an error code
 * otherwise.
 */
static int clovis_obj_op_prepare(struct m0_clovis_entity *entity,
				 struct m0_clovis_op **op,
				 enum m0_clovis_entity_opcode opcode)
{
	int                         rc;
	bool                        alloced = false;
	struct m0_clovis_op_common *oc;
	struct m0_clovis_op_obj    *oo;

	M0_ENTRY();

	M0_PRE(entity != NULL);
	M0_ASSERT(clovis_entity_invariant_full(entity));
	M0_PRE(entity->en_sm.sm_state == M0_CLOVIS_ES_INIT);
	M0_PRE(op != NULL);
	/* We may want to gain this lock, check we don't already hold it. */
	M0_PRE(*op == NULL ||
	       !m0_sm_group_is_locked(&(*op)->op_sm_group));

	/* Allocate the op if necessary. */
	if (*op == NULL) {
		rc = m0_clovis_op_alloc(op, sizeof *oo);
		if (rc != 0)
			return M0_ERR(rc);
		alloced = true;
	} else {
		size_t cached_size = (*op)->op_size;

		if ((*op)->op_size < sizeof *oo)
			return M0_ERR(-EMSGSIZE);

		/* 0 the pre-allocated operation. */
		memset(*op, 0, cached_size);
		(*op)->op_size = cached_size;
	}

	/* Initialise the operation's generic part. */
	rc = m0_clovis_op_init(*op, &clovis_op_conf, entity);
	if (rc != 0)
		goto op_free;
	(*op)->op_code = opcode;

	/* No bob_init()'s have been called yet: we use container_of */
	oc = container_of(*op, struct m0_clovis_op_common, oc_op);
	m0_clovis_op_common_bob_init(oc);

	/* Init the m0_clovis_op_obj part. */
	oo = container_of(oc, struct m0_clovis_op_obj, oo_oc);
	rc = clovis_obj_op_obj_init(oo);
	if (rc != 0)
		goto op_fini;

	return M0_RC(0);

op_fini:
	m0_clovis_op_fini(*op);
op_free:
	if (alloced)
		m0_clovis_op_free(*op);

	return M0_RC(rc);
}

/**
 * Sets a entity operation to modify the object namespace.
 * This type of operation on entities allow creating and deleting entities.
 *
 * @param entity entity to be modified.
 * @param op pointer to the operation being set. The caller might pre-allocate
 * this operation.Otherwise, the function will allocate the required memory.
 * @param opcode M0_CLOVIS_EO_CREATE or M0_CLOVIS_EO_DELETE.
 * @return 0 if the function succeeds or an error code otherwise.
 */
static int clovis_entity_namei_op(struct m0_clovis_entity *entity,
				struct m0_clovis_op **op,
				enum m0_clovis_entity_opcode opcode)
{
	int rc;

	M0_ENTRY();

	M0_PRE(entity != NULL);
	M0_ASSERT(clovis_entity_invariant_full(entity));
	M0_PRE(entity->en_sm.sm_state == M0_CLOVIS_ES_INIT);
	M0_PRE(op != NULL);
	M0_PRE(M0_IN(opcode, (M0_CLOVIS_EO_CREATE, M0_CLOVIS_EO_DELETE)));

	switch (entity->en_type) {
	case M0_CLOVIS_ET_OBJ:
		/* Allocate an op on an object and initialise common stuff. */
		rc = clovis_obj_op_prepare(entity, op, opcode);
		if (rc != 0)
			goto error;

		/* Initialise the stuff specific to a obj namespace operation. */
		rc = clovis_obj_namei_op_init(entity, *op);
		if (rc != 0)
			goto error;
		break;
	case M0_CLOVIS_ET_IDX:
		rc = m0_clovis_idx_op_namei(entity, op, opcode);
		break;
	default:
		M0_IMPOSSIBLE("Entity type not yet implemented.");
	}

	M0_POST(rc == 0);
	M0_POST(*op != NULL);
	m0_sm_group_lock(&(*op)->op_sm_group);
	M0_POST((*op)->op_sm.sm_rc == 0);
	m0_sm_group_unlock(&(*op)->op_sm_group);

	return M0_RC(0);
error:
	M0_ASSERT(rc != 0);
	return M0_ERR(rc);
}

int m0_clovis_entity_create(struct m0_clovis_entity *entity,
			    struct m0_clovis_op **op)
{
	M0_ENTRY();

	M0_PRE(entity != NULL);
	M0_PRE(op != NULL);

	return M0_RC(clovis_entity_namei_op(entity, op, M0_CLOVIS_EO_CREATE));
}
M0_EXPORTED(m0_clovis_entity_create);

int m0_clovis_entity_delete(struct m0_clovis_entity *entity,
			    struct m0_clovis_op **op)
{
	M0_ENTRY();

	M0_PRE(entity != NULL);
	M0_PRE(op != NULL);

	return M0_RC(clovis_entity_namei_op(entity, op, M0_CLOVIS_EO_DELETE));
}
M0_EXPORTED(m0_clovis_entity_delete);

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
