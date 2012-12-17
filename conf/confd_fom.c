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
#include "conf/confd_hack.h"  /* XXX m0_confd_hack_mode, m0_conf_str */
#include "lib/memory.h"
#include "lib/errno.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"  /* M0_FOPH_NR */

/**
 * @addtogroup confd_dlspec
 *
 * @{
 */

static size_t confd_fom_locality(const struct m0_fom *fom)
{
	return m0_fop_opcode(fom->fo_fop);
}

static void confd_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(container_of(fom, struct m0_confd_fom, dm_fom));
}

static int conf_fetch_tick(struct m0_fom *fom);
static int conf_update_tick(struct m0_fom *fom);

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

static int
conf_fetch_resp_fill(struct m0_conf_fetch_resp *r, const char *local_conf)
{
	/*
	 * Maximal number of confx_objects that a confd is allowed to send.
	 *
	 * It is advisable for this code to be revised when we have a
	 * better idea of confd throughput.  Small limit (16) will be
	 * hit sooner, thus the code will be revised sooner.
	 */
	enum { MAX_NR_XOBJS = 16 };
	struct confx_object *xobjs;
	int n;

	M0_ENTRY();
	M0_PRE(local_conf != NULL && *local_conf != '\0');

	M0_ALLOC_ARR(xobjs, MAX_NR_XOBJS);
	if (xobjs == NULL)
		M0_RETURN(-ENOMEM);

	n = m0_conf_parse(local_conf, xobjs, MAX_NR_XOBJS);
	if (n < 0) {
		m0_free(xobjs);
		M0_RETURN(n);
	}
	M0_ASSERT(n > 0);

	r->fr_rc = 0;
	r->fr_data.ec_nr = n;
	r->fr_data.ec_objs = xobjs; /* will be freed by rpc layer */

	M0_RETURN(0);
}

static int conf_fetch_tick(struct m0_fom *fom)
{
	const char *local_conf;
	int rc;

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);

	M0_ASSERT(fom->fo_rep_fop == NULL);
	fom->fo_rep_fop = m0_fop_alloc(&m0_conf_fetch_resp_fopt, NULL);
	if (fom->fo_rep_fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = m0_conf_str(m0_confd_hack_mode, &local_conf); /*XXX*/
	if (rc == 0) {
		rc = conf_fetch_resp_fill(m0_fop_data(fom->fo_rep_fop),
					  local_conf);
	} else {
		m0_fop_put(fom->fo_rep_fop);
		fom->fo_rep_fop = NULL;
	}
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

/** @} confd_dlspec */
