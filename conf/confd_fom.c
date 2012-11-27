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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/confd_fom.h"
#include "conf_fop.h"         /* c2_conf_fetch_resp_fopt */
#include "conf/onwire.h"      /* c2_conf_fetch_resp */
#include "conf/preload.h"     /* c2_conf_parse */
#include "conf/confd.h"       /* c2_confd, c2_confd_bob */
#include "lib/memory.h"
#include "lib/errno.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"  /* C2_FOPH_NR */

/**
 * @addtogroup confd_dlspec
 *
 * @{
 */

static size_t confd_fom_locality(const struct c2_fom *fom)
{
	return c2_fop_opcode(fom->fo_fop);
}

static void confd_fom_fini(struct c2_fom *fom)
{
	c2_fom_fini(fom);
	c2_free(container_of(fom, struct c2_confd_fom, dm_fom));
}

static int conf_fetch_tick(struct c2_fom *fom);
static int conf_update_tick(struct c2_fom *fom);

static const struct c2_fom_ops conf_fetch_fom_ops = {
	.fo_home_locality = confd_fom_locality,
	.fo_tick = conf_fetch_tick,
	.fo_fini = confd_fom_fini
};

static const struct c2_fom_ops conf_update_fom_ops = {
	.fo_home_locality = confd_fom_locality,
	.fo_tick = conf_update_tick,
	.fo_fini = confd_fom_fini
};

C2_INTERNAL int c2_confd_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	struct c2_confd_fom     *m;
	const struct c2_fom_ops *ops;

	C2_ENTRY();

	C2_ALLOC_PTR(m);
	if (m == NULL)
		C2_RETURN(-ENOMEM);

	switch (c2_fop_opcode(fop)) {
	case C2_CONF_FETCH_OPCODE:
		ops = &conf_fetch_fom_ops;
		break;
	case C2_CONF_UPDATE_OPCODE:
		ops = &conf_update_fom_ops;
		break;
	default:
		c2_free(m);
		C2_RETURN(-EOPNOTSUPP);
	}

	c2_fom_init(&m->dm_fom, &fop->f_type->ft_fom_type, ops, fop, NULL);
	*out = &m->dm_fom;
	C2_RETURN(0);
}

static int
conf_fetch_resp_fill(struct c2_conf_fetch_resp *r, const char *local_conf)
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

	C2_ENTRY();
	C2_PRE(local_conf != NULL && *local_conf != '\0');

	C2_ALLOC_ARR(xobjs, MAX_NR_XOBJS);
	if (xobjs == NULL)
		C2_RETURN(-ENOMEM);

	n = c2_conf_parse(local_conf, xobjs, MAX_NR_XOBJS);
	if (n < 0) {
		c2_free(xobjs);
		C2_RETURN(n);
	}
	C2_ASSERT(n > 0);

	r->fr_rc = 0;
	r->fr_data.ec_nr = n;
	r->fr_data.ec_objs = xobjs; /* will be freed by rpc layer */

	C2_RETURN(0);
}

/** Accesses c2_confd::d_local_conf. */
static const char *local_conf(const struct c2_fom *fom)
{
	C2_PRE(fom->fo_service != NULL);

	return bob_of(fom->fo_service, struct c2_confd, d_reqh,
		      &c2_confd_bob)->d_local_conf;
}

static int conf_fetch_tick(struct c2_fom *fom)
{
	struct c2_fop *p;
	int rc;

	if (c2_fom_phase(fom) < C2_FOPH_NR)
		return c2_fom_tick_generic(fom);

	C2_ASSERT(fom->fo_rep_fop == NULL);
	fom->fo_rep_fop = p = c2_fop_alloc(&c2_conf_fetch_resp_fopt, NULL);
	rc = p == NULL ? -ENOMEM : conf_fetch_resp_fill(c2_fop_data(p),
							local_conf(fom));

        c2_fom_phase_moveif(fom, rc, C2_FOPH_SUCCESS, C2_FOPH_FAILURE);
	return C2_FSO_AGAIN;
}

static int conf_update_tick(struct c2_fom *fom)
{
	C2_IMPOSSIBLE("XXX Not implemented");

        c2_fom_phase_move(fom, -999, C2_FOPH_FAILURE);
	return C2_FSO_AGAIN;
}

/** @} confd_dlspec */
