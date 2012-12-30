/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/confd_fom.h"
#include "conf_fop.h"         /* m0_conf_fetch_resp_fopt */
#include "conf/onwire.h"      /* m0_conf_fetch_resp */
#include "conf/preload.h"     /* m0_conf_parse */
#include "conf/confd.h"       /* m0_confd, m0_confd_bob */
#include "conf/buf_ext.h"     /* m0_buf_strdup */
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"         /* M0_SET0 */
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"  /* M0_FOPH_NR */

/**
 * @addtogroup confd_dlspec
 *
 * @{
 */

static int conf_fetch_tick(struct m0_fom *fom);
static int conf_update_tick(struct m0_fom *fom);
static int enconf_fill(struct enconf *dest, const struct objid *origin,
		       const struct arr_buf *path, struct m0_conf_reg *reg);

static size_t confd_fom_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static void confd_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(container_of(fom, struct m0_confd_fom, dm_fom));
}

static const struct m0_fom_ops conf_fetch_fom_ops = {
	.fo_home_locality = confd_fom_locality,
	.fo_tick = conf_fetch_tick,
	.fo_fini = confd_fom_fini
};

static const struct m0_fom_ops conf_update_fom_ops = {
	.fo_home_locality = confd_fom_locality,
	.fo_tick = conf_update_tick,
	.fo_fini = confd_fom_fini
};

M0_INTERNAL int m0_confd_fom_create(struct m0_fop *fop, struct m0_fom **out)
{
	struct m0_confd_fom     *m;
	const struct m0_fom_ops *ops;

	M0_ENTRY();

	M0_ALLOC_PTR(m);
	if (m == NULL)
		M0_RETURN(-ENOMEM);

	switch (m0_fop_opcode(fop)) {
	case M0_CONF_FETCH_OPCODE:
		ops = &conf_fetch_fom_ops;
		break;
	case M0_CONF_UPDATE_OPCODE:
		ops = &conf_update_fom_ops;
		break;
	default:
		m0_free(m);
		M0_RETURN(-EOPNOTSUPP);
	}

	m0_fom_init(&m->dm_fom, &fop->f_type->ft_fom_type, ops, fop, NULL);
	*out = &m->dm_fom;
	M0_RETURN(0);
}

static int conf_fetch_tick(struct m0_fom *fom)
{
	const struct m0_conf_fetch *q;
	struct m0_conf_fetch_resp  *r;
	int                         rc;

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	M0_ASSERT(fom->fo_rep_fop == NULL);
	fom->fo_rep_fop = m0_fop_alloc(&m0_conf_fetch_resp_fopt, NULL);
	if (fom->fo_rep_fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	q = m0_fop_data(fom->fo_fop);
	r = m0_fop_data(fom->fo_rep_fop);

	rc = enconf_fill(&r->fr_data, &q->f_origin, &q->f_path,
			 &bob_of(fom->fo_service, struct m0_confd, d_reqh,
				 &m0_confd_bob)->d_reg);
	if (rc != 0)
		M0_ASSERT(r->fr_data.ec_nr == 0 && r->fr_data.ec_objs == NULL);
	r->fr_rc = rc;
out:
	m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	return M0_FSO_AGAIN;
}

static int conf_update_tick(struct m0_fom *fom)
{
	M0_IMPOSSIBLE("XXX Not implemented");

	m0_fom_phase_move(fom, -999, M0_FOPH_FAILURE);
	return M0_FSO_AGAIN;
}

static int _count(size_t *n, struct enconf *enc __attribute__((unused)),
		  const struct m0_conf_obj *obj __attribute__((unused)))
{
	++*n;
	return 0;
}

static int _encode(size_t *n, struct enconf *enc, const struct m0_conf_obj *obj)
{
	int rc;

	M0_PRE(m0_conf_obj_invariant(obj) && obj->co_status == M0_CS_READY);

	M0_CNT_DEC(*n);
	rc = obj->co_ops->coo_xfill(&enc->ec_objs[enc->ec_nr], obj);
	if (rc == 0)
		++enc->ec_nr;
	return rc;
}

static int readiness_check(const struct m0_conf_obj *obj)
{
	if (m0_conf_obj_is_stub(obj)) {
		/* All configuration is expected to be loaded already. */
		M0_ASSERT(obj->co_status != M0_CS_LOADING);
		return -ENOENT;
	}
	return 0;
}

/**
 * Traverses the DAG of configuration objects.
 * Starts at `cur' and follows the `path', applying given function.
 *
 * @param cur    Path origin.
 * @param path   Sequence of path components as requested by confc.
 * @param apply  The action to perform.
 * @param cnt    Address of the counter to be used by apply().
 * @param enc    Encoded configuration data to be used by apply().
 */
static int confd_path_walk(struct m0_conf_obj *cur, const struct arr_buf *path,
			   int (*apply)(size_t *n, struct enconf *enc,
					const struct m0_conf_obj *obj),
			   size_t *n, struct enconf *enc)
{
	struct m0_conf_obj *entry;
	int                 i;
	int                 rc;

	M0_ENTRY();
	M0_PRE(m0_conf_obj_invariant(cur) && cur->co_status == M0_CS_READY);

	for (i = 0; i < path->ab_count; ++i) {
		/* Handle intermediate object. */
		if (cur->co_type != M0_CO_DIR) {
			rc = apply(n, enc, cur);
			if (rc != 0)
				M0_RETURN(rc);
		}

		rc = cur->co_ops->coo_lookup(cur, &path->ab_elems[i], &cur) ?:
			readiness_check(cur);
		if (rc != 0)
			M0_RETURN(rc);
	}

	/* Handle final object. */
	if (cur->co_parent != NULL && cur->co_parent->co_type == M0_CO_DIR)
		/* Include siblings into the resulting set. */
		cur = cur->co_parent;
	if (cur->co_type == M0_CO_DIR) {
		m0_conf_obj_get(cur); /* as expected by ->coo_readdir() */
		for (entry = NULL;
		     (rc = cur->co_ops->coo_readdir(cur, &entry)) > 0; ) {
			/* All configuration is expected to be available. */
			M0_ASSERT(rc != M0_CONF_DIRMISS);

			rc = apply(n, enc, entry);
			if (rc != 0)
				M0_RETURN(rc);
		}
		m0_conf_obj_put(cur);
	} else {
		rc = apply(n, enc, cur);
	}

	M0_RETURN(rc);
}

static int enconf_fill(struct enconf *dest, const struct objid *origin,
		       const struct arr_buf *path, struct m0_conf_reg *reg)
{
	struct m0_conf_obj *org;
	int                 rc;
	size_t              nr = 0;

	M0_ENTRY();
	M0_SET0(dest);

	rc = m0_conf_obj_find(reg, origin->oi_type, &origin->oi_id, &org) ?:
		readiness_check(org);
	if (rc != 0)
		M0_RETURN(rc);

	rc = confd_path_walk(org, path, _count, &nr, NULL);
	if (rc != 0 || nr == 0)
		M0_RETURN(rc);

	M0_ALLOC_ARR(dest->ec_objs, nr);
	if (dest->ec_objs == NULL)
		M0_RETURN(-ENOMEM);

	M0_LOG(M0_DEBUG, "Will encode %zu configuration object%s", nr,
	       (char *)(nr > 1 ? "s" : ""));
	rc = confd_path_walk(org, path, _encode, &nr, dest);
	if (rc == 0) {
		M0_ASSERT(nr == 0);
	} else {
		m0_confx_fini(dest->ec_objs, dest->ec_nr);
		m0_free(dest->ec_objs);
	}

	M0_RETURN(rc);
}

/** @} confd_dlspec */
