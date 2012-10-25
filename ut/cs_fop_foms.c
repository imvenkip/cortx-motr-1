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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/10/2011
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"
#include "lib/finject.h"
#include "lib/time.h"

#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"

#include "rpc/rpc2.h"
#include "rpc/rpclib.h"
#include "fop/fop_item_type.h"

#include "cs_fop_foms.h"
#include "cs_test_fops_ff.h"
#include "rpc/rpc_opcodes.h"

static void cs_ut_rpc_item_reply_cb(struct c2_rpc_item *item);

/*
  RPC item operations structures.
 */
const struct c2_rpc_item_ops cs_ds_req_fop_rpc_item_ops = {
        .rio_replied = cs_ut_rpc_item_reply_cb,
	.rio_free    = c2_fop_item_free,
};

struct c2_fop_type cs_ds1_req_fop_fopt;
struct c2_fop_type cs_ds1_rep_fop_fopt;
struct c2_fop_type cs_ds2_req_fop_fopt;
struct c2_fop_type cs_ds2_rep_fop_fopt;

/*
  Fom specific routines for corresponding fops.
 */
static int cs_req_fop_fom_tick(struct c2_fom *fom);
static int cs_ds1_req_fop_fom_create(struct c2_fop *fop, struct c2_fom **out);
static int cs_ds2_req_fop_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void cs_ut_fom_fini(struct c2_fom *fom);
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom);

/*
  Operation structures for ds1 service foms.
 */
static const struct c2_fom_ops cs_ds1_req_fop_fom_ops = {
        .fo_fini = cs_ut_fom_fini,
        .fo_tick = cs_req_fop_fom_tick,
        .fo_home_locality = cs_ut_find_fom_home_locality,
};

/*
  Operation structures for ds2 service foms.
 */
static const struct c2_fom_ops cs_ds2_req_fop_fom_ops = {
        .fo_fini = cs_ut_fom_fini,
        .fo_tick = cs_req_fop_fom_tick,
        .fo_home_locality = cs_ut_find_fom_home_locality,
};

extern struct c2_reqh_service_type ds1_service_type;
extern struct c2_reqh_service_type ds2_service_type;

enum ds_phases {
	C2_FOPH_DS1_REQ = C2_FOPH_NR + 1,
	C2_FOPH_DS2_REQ = C2_FOPH_NR + 1,
};

/*
  Fom type operations for ds1 service foms.
 */
static const struct c2_fom_type_ops cs_ds1_req_fop_fom_type_ops = {
        .fto_create = cs_ds1_req_fop_fom_create,
};

/*
  Fom type operations for ds2 service foms.
 */
static const struct c2_fom_type_ops cs_ds2_req_fop_fom_type_ops = {
        .fto_create = cs_ds2_req_fop_fom_create,
};

static void cs_ut_rpc_item_reply_cb(struct c2_rpc_item *item)
{
	struct c2_fop *req_fop;
	struct c2_fop *rep_fop;

        C2_PRE(item != NULL);

	req_fop = c2_rpc_item_to_fop(item);

	C2_ASSERT(C2_IN(c2_fop_opcode(req_fop), (C2_CS_DS1_REQ_OPCODE,
						 C2_CS_DS2_REQ_OPCODE)));

	if (item->ri_error == 0) {
		rep_fop = c2_rpc_item_to_fop(item->ri_reply);
		C2_ASSERT(C2_IN(c2_fop_opcode(rep_fop),
				(C2_CS_DS1_REP_OPCODE,
				 C2_CS_DS2_REP_OPCODE)));
	}
}

void c2_cs_ut_ds1_fop_fini(void)
{
	c2_fop_type_fini(&cs_ds1_req_fop_fopt);
	c2_fop_type_fini(&cs_ds1_rep_fop_fopt);
	c2_xc_cs_test_fops_fini();
}

int c2_cs_ut_ds1_fop_init(void)
{
        /*
           As we are finalising and initialising fop types multiple times
           per service for various colibri_setup commands, So reinitialise
           fop_type_format for each corresponding service fop types.
         */
	c2_xc_cs_test_fops_init();
        return  C2_FOP_TYPE_INIT(&cs_ds1_req_fop_fopt,
				 .name      = "ds1 request",
				 .opcode    = C2_CS_DS1_REQ_OPCODE,
				 .xt        = cs_ds1_req_fop_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fom_ops   = &cs_ds1_req_fop_fom_type_ops,
				 .sm        = &c2_generic_conf,
				 .svc_type  = &ds1_service_type) ?:
		C2_FOP_TYPE_INIT(&cs_ds1_rep_fop_fopt,
				 .name      = "ds1 reply",
				 .opcode    = C2_CS_DS1_REP_OPCODE,
				 .xt        = cs_ds1_rep_fop_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .fom_ops   = &cs_ds1_req_fop_fom_type_ops);
}

void c2_cs_ut_ds2_fop_fini(void)
{
	c2_fop_type_fini(&cs_ds2_rep_fop_fopt);
	c2_fop_type_fini(&cs_ds2_req_fop_fopt);
	c2_xc_cs_test_fops_fini();
}

int c2_cs_ut_ds2_fop_init(void)
{
	/*
	   As we are finalising and initialising fop types multiple times
	   per service for various colibri_setup commands, So reinitialise
	   fop_type_format for each corresponding service fop types.
	 */
	c2_xc_cs_test_fops_init();
	return  C2_FOP_TYPE_INIT(&cs_ds2_req_fop_fopt,
				 .name      = "ds2 request",
				 .opcode    = C2_CS_DS2_REQ_OPCODE,
				 .xt        = cs_ds2_req_fop_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fom_ops   = &cs_ds2_req_fop_fom_type_ops,
				 .sm        = &c2_generic_conf,
				 .svc_type  = &ds2_service_type) ?:
		C2_FOP_TYPE_INIT(&cs_ds2_rep_fop_fopt,
				 .name      = "ds2 reply",
				 .opcode    = C2_CS_DS2_REP_OPCODE,
				 .xt        = cs_ds2_rep_fop_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .fom_ops   = &cs_ds2_req_fop_fom_type_ops);
}

/*
  Allocates and initialises a fom.
 */
static int cs_ds_req_fop_fom_create(struct c2_fop *fop,
				    const struct c2_fom_ops *ops,
				    struct c2_fom **out)
{
        struct c2_fom *fom;

	C2_PRE(fop != NULL);
	C2_PRE(ops != NULL);
        C2_PRE(out != NULL);

        C2_ALLOC_PTR(fom);
        if (fom == NULL)
                return -ENOMEM;

	c2_fom_init(fom, &fop->f_type->ft_fom_type, ops, fop, NULL);

        *out = fom;
        return 0;
}

static int cs_ds1_req_fop_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return cs_ds_req_fop_fom_create(fop, &cs_ds1_req_fop_fom_ops, out);
}

static int cs_ds2_req_fop_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return cs_ds_req_fop_fom_create(fop, &cs_ds2_req_fop_fom_ops, out);
}

/*
  Finalises a fom.
 */
static void cs_ut_fom_fini(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

        c2_fom_fini(fom);
        c2_free(fom);
}

/*
  Returns an index value base on fom parameters to locate fom's
  home locality to execute a fom.
 */
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	return c2_fop_opcode(fom->fo_fop);
}

/*
  Transitions fom through its generic phases and also
  performs corresponding fop specific execution.
 */
static int cs_req_fop_fom_tick(struct c2_fom *fom)
{
	int                    rc;
	struct c2_fop         *rfop;
	struct cs_ds1_req_fop *ds1_reqfop;
	struct cs_ds1_rep_fop *ds1_repfop;
	struct cs_ds2_req_fop *ds2_reqfop;
	struct cs_ds2_rep_fop *ds2_repfop;
	uint64_t               opcode;

	C2_PRE(C2_IN(c2_fop_opcode(fom->fo_fop), (C2_CS_DS1_REQ_OPCODE,
						  C2_CS_DS2_REQ_OPCODE)));
	if (c2_fom_phase(fom) < C2_FOPH_NR) {
		rc = c2_fom_tick_generic(fom);
	} else {
		opcode = c2_fop_opcode(fom->fo_fop);
		switch (opcode) {
		case C2_CS_DS1_REQ_OPCODE:
			rfop = c2_fop_alloc(&cs_ds1_rep_fop_fopt, NULL);
			if (rfop == NULL) {
				c2_fom_phase_set(fom, C2_FOPH_FINISH);
				return C2_FSO_WAIT;
			}
			ds1_reqfop = c2_fop_data(fom->fo_fop);
			ds1_repfop = c2_fop_data(rfop);
			ds1_repfop->csr_rc = ds1_reqfop->csr_value;
			fom->fo_rep_fop = rfop;
			c2_fom_phase_set(fom, C2_FOPH_SUCCESS);
			rc = C2_FSO_AGAIN;
			break;
		case C2_CS_DS2_REQ_OPCODE:
			rfop = c2_fop_alloc(&cs_ds2_rep_fop_fopt, NULL);
			if (rfop == NULL) {
				c2_fom_phase_set(fom, C2_FOPH_FINISH);
				return C2_FSO_WAIT;
			}
			ds2_reqfop = c2_fop_data(fom->fo_fop);
			ds2_repfop = c2_fop_data(rfop);
			ds2_repfop->csr_rc = ds2_reqfop->csr_value;
			fom->fo_rep_fop = rfop;
			c2_fom_phase_set(fom, C2_FOPH_SUCCESS);
			rc = C2_FSO_AGAIN;
			break;
		default:
			 C2_ASSERT("Invalid fop" == 0);
		}
	}
	if (C2_FI_ENABLED("inject_delay")) {
		c2_nanosleep(c2_time(2, 0), NULL);
	}
	return rc;
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
