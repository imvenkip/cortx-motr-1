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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/conf_fom.h"
#include "conf/onwire.h"
#include "fop/fom_generic.h"
#include "fop/fop.h"
#include "rpc/rpc.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "conf/preload.h"

extern struct c2_fop_type c2_conf_fetch_resp_fopt;

static void conf_fom_fini(struct c2_fom *fom);
static int conf_fetch_tick(struct c2_fom *fom);
static int conf_update_tick(struct c2_fom *fom);
static size_t conf_home_locality(const struct c2_fom *fom);
static int fetch_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int update_fom_create(struct c2_fop *fop, struct c2_fom **m);

static struct c2_fom_ops c2_fom_conf_fetch_ops = {
	.fo_fini          = conf_fom_fini,
	.fo_tick          = conf_fetch_tick,
	.fo_home_locality = conf_home_locality
};

static struct c2_fom_ops c2_fom_conf_update_ops = {
	.fo_fini          = conf_fom_fini,
	.fo_tick          = conf_update_tick,
	.fo_home_locality = conf_home_locality
};

const struct c2_fom_type_ops c2_fom_conf_fetch_type_ops = {
	.fto_create = fetch_fom_create
};

const struct c2_fom_type_ops c2_fom_conf_update_type_ops = {
	.fto_create = update_fom_create
};

static size_t conf_home_locality(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	return c2_fop_opcode(fom->fo_fop);
}

static int
conf_fom_create(struct c2_fop *fop, struct c2_fom **res, struct c2_fom_ops *ops)
{
	struct c2_conf_fom *m;

	C2_ENTRY();
	C2_PRE(fop != NULL && res != NULL);

	C2_ALLOC_PTR(m);
	if (m == NULL)
		C2_RETURN(-ENOMEM);

	c2_fom_init(&m->cf_gen, &fop->f_type->ft_fom_type, ops, fop, NULL);
	m->cf_fop = fop;
	*res = &m->cf_gen;

	C2_RETURN(0);
}

static int fetch_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	return conf_fom_create(fop, m, &c2_fom_conf_fetch_ops);
}

static int update_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	return conf_fom_create(fop, m, &c2_fom_conf_update_ops);
}

static void conf_fom_fini(struct c2_fom *fom)
{
	struct c2_conf_fom *fom_obj;

	fom_obj = container_of(fom, struct c2_conf_fom, cf_gen);
	c2_fom_fini(fom);
	c2_free(fom_obj);
}

static int conf_fetch_tick(struct c2_fom *fom)
{
	static bool been_here = false; /* XXX */

	if (c2_fom_phase(fom) < C2_FOPH_NR)
		return c2_fom_tick_generic(fom);

	if (!been_here) {
		struct c2_fop             *resp;
		struct confx_object        xobjs[6];
		int                        xnr;
		int                        rc;
		const char buf[] = "[1: (\"test-2\", {1| (\"c2t1fs\")})]";

		been_here = true;

		xnr = c2_conf_parse(buf, xobjs, ARRAY_SIZE(xobjs));
		C2_ASSERT(xnr == 1);

		resp = c2_fop_alloc(&c2_conf_fetch_resp_fopt, NULL);
		C2_ASSERT(resp != NULL);
		{
			struct c2_conf_fetch_resp *r = c2_fop_data(resp);

			r->fr_rc = 0;
			r->fr_data.ec_nr = xnr;
			r->fr_data.ec_objs = xobjs;
		}

		/* Just to confirm our understanding of what is going on. */
		C2_ASSERT(resp->f_item.ri_type->rit_ops ==
			  &c2_rpc_fop_default_item_type_ops);

		rc = c2_rpc_reply_post(
			c2_fop_to_rpc_item(container_of(fom, struct c2_conf_fom,
							cf_gen)->cf_fop),
			c2_fop_to_rpc_item(resp));
		C2_ASSERT(rc == 0);

		c2_confx_fini(xobjs, xnr);

		c2_dtx_init(&fom->fo_tx);
		c2_dtx_done(&fom->fo_tx);
	}

	c2_fom_phase_set(fom, C2_FOPH_FINISH);
	return C2_FSO_WAIT;
}

static int conf_update_tick(struct c2_fom *fom)
{
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
