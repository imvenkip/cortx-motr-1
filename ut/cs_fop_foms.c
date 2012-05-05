/* -*- C -*- */
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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/10/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"

#include "fop/fop.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"

#include "rpc/rpc2.h"
#include "rpc/rpclib.h"
#include "fop/fop_item_type.h"
#include "xcode/bufvec_xcode.h"

#include "fop/fop_format_def.h"

#include "cs_fop_foms.h"
#include "cs_test_fops_u.h"
#include "cs_test_fops.ff"
#include "rpc/rpc_opcodes.h"

static void cs_ut_rpc_item_reply_cb(struct c2_rpc_item *item);

/*
  RPC item operations structures.
 */
const struct c2_rpc_item_ops cs_ds_req_fop_rpc_item_ops = {
        .rio_replied = cs_ut_rpc_item_reply_cb,
	.rio_free    = c2_fop_item_free,
};

/* DS1 service fop type operations.*/
static const struct c2_fop_type_ops cs_ds1_req_fop_type_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

static const struct c2_fop_type_ops cs_ds1_rep_fop_type_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

/* DS2 service fop type operations */
static const struct c2_fop_type_ops cs_ds2_req_fop_type_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

static const struct c2_fop_type_ops cs_ds2_rep_fop_type_ops = {
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_io_coalesce = NULL,
};

C2_FOP_TYPE_DECLARE(cs_ds1_req_fop, "ds1 request", &cs_ds1_req_fop_type_ops,
		    C2_CS_DS1_REQ_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO)
C2_FOP_TYPE_DECLARE(cs_ds1_rep_fop, "ds1 reply", &cs_ds1_rep_fop_type_ops,
		    C2_CS_DS1_REP_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(cs_ds2_req_fop, "ds2 request", &cs_ds2_req_fop_type_ops,
		    C2_CS_DS2_REQ_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO)
C2_FOP_TYPE_DECLARE(cs_ds2_rep_fop, "ds2 reply", &cs_ds2_rep_fop_type_ops,
		    C2_CS_DS2_REP_OPCODE, C2_RPC_ITEM_TYPE_REPLY);

/*
  Defines ds1 service fop types array.
 */
static struct c2_fop_type *cs_ds1_fopts[] = {
        &cs_ds1_req_fop_fopt,
        &cs_ds1_rep_fop_fopt
};

/*
  Defines ds2 service fop types array.
 */
static struct c2_fop_type *cs_ds2_fopts[] = {
        &cs_ds2_req_fop_fopt,
        &cs_ds2_rep_fop_fopt
};

/*
  Fom specific routines for corresponding fops.
 */
static int cs_req_fop_fom_state(struct c2_fom *fom);
static int cs_ds1_req_fop_fom_create(struct c2_fop *fop, struct c2_fop_ctx *ctx,
                                     struct c2_fom **out);
static int cs_ds2_req_fop_fom_create(struct c2_fop *fop, struct c2_fop_ctx *ctx,
                                     struct c2_fom **out);
static void cs_ut_fom_fini(struct c2_fom *fom);
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom);

/*
  Operation structures for ds1 service foms.
 */
static const struct c2_fom_ops cs_ds1_req_fop_fom_ops = {
        .fo_fini = cs_ut_fom_fini,
        .fo_state = cs_req_fop_fom_state,
        .fo_home_locality = cs_ut_find_fom_home_locality,
};

/*
  Operation structures for ds2 service foms.
 */
static const struct c2_fom_ops cs_ds2_req_fop_fom_ops = {
        .fo_fini = cs_ut_fom_fini,
        .fo_state = cs_req_fop_fom_state,
        .fo_home_locality = cs_ut_find_fom_home_locality,
};

/*
  Fom type operations for ds1 service foms.
 */
static const struct c2_fom_type_ops cs_ds1_req_fop_fom_type_ops = {
        .fto_create = cs_ds1_req_fop_fom_create,
};

static struct c2_fom_type cs_ds1_req_fop_fom_mopt = {
	.ft_ops = &cs_ds1_req_fop_fom_type_ops,
};

static const struct c2_fom_type_ops cs_ds1_rep_fop_fom_type_ops = {
        .fto_create = NULL
};

static struct c2_fom_type cs_ds1_rep_fop_fom_mopt = {
	.ft_ops = &cs_ds1_rep_fop_fom_type_ops,
};

/*
  Fom type operations for ds2 service foms.
 */
static const struct c2_fom_type_ops cs_ds2_req_fop_fom_type_ops = {
        .fto_create = cs_ds2_req_fop_fom_create,
};

static struct c2_fom_type cs_ds2_req_fop_fom_mopt = {
	.ft_ops = &cs_ds2_req_fop_fom_type_ops,
};

static const struct c2_fom_type_ops cs_ds2_rep_fop_fom_type_ops = {
        .fto_create = NULL
};

static struct c2_fom_type cs_ds2_rep_fop_fom_mopt = {
	.ft_ops = &cs_ds2_rep_fop_fom_type_ops,
};

static void cs_ut_rpc_item_reply_cb(struct c2_rpc_item *item)
{
	struct c2_fop *req_fop;
	struct c2_fop *rep_fop;

        C2_PRE(item != NULL);
	C2_PRE(c2_chan_has_waiters(&item->ri_chan));

	req_fop = c2_rpc_item_to_fop(item);
	rep_fop = c2_rpc_item_to_fop(item->ri_reply);

	C2_ASSERT(req_fop->f_type->ft_rpc_item_type.rit_opcode ==
		    C2_CS_DS1_REQ_OPCODE ||
		    req_fop->f_type->ft_rpc_item_type.rit_opcode ==
		    C2_CS_DS2_REQ_OPCODE);

	C2_ASSERT(rep_fop->f_type->ft_rpc_item_type.rit_opcode ==
		     C2_CS_DS1_REP_OPCODE ||
		     rep_fop->f_type->ft_rpc_item_type.rit_opcode ==
		     C2_CS_DS2_REP_OPCODE);
}

void c2_cs_ut_ds1_fop_fini(void)
{
	int i;

        c2_fop_type_fini_nr(cs_ds1_fopts, ARRAY_SIZE(cs_ds1_fopts));

	for (i = 0; i < ARRAY_SIZE(cs_ds1_fopts); ++i)
		cs_ds1_fopts[i]->ft_top = NULL;
}

int c2_cs_ut_ds1_fop_init(void)
{
        int result;

        /*
           As we are finalising and initialising fop types multiple times
           per service for various colibri_setup commands, So reinitialise
           fop_type_format for each corresponding service fop types.
         */
	cs_ds1_fopts[0]->ft_fmt = &cs_ds1_req_fop_tfmt;
	cs_ds1_fopts[1]->ft_fmt = &cs_ds1_rep_fop_tfmt;
	cs_ds1_fopts[0]->ft_fom_type = cs_ds1_req_fop_fom_mopt;
	cs_ds1_fopts[1]->ft_fom_type = cs_ds1_rep_fop_fom_mopt;

        result = c2_fop_type_build_nr(cs_ds1_fopts, ARRAY_SIZE(cs_ds1_fopts));
        if (result != 0)
                c2_cs_ut_ds1_fop_fini();
        return result;
}

void c2_cs_ut_ds2_fop_fini(void)
{
	int i;

        c2_fop_type_fini_nr(cs_ds2_fopts, ARRAY_SIZE(cs_ds2_fopts));
	for (i = 0; i < ARRAY_SIZE(cs_ds2_fopts); ++i)
		cs_ds2_fopts[i]->ft_top = NULL;
}

int c2_cs_ut_ds2_fop_init(void)
{
        int result;

	/*
	   As we are finalising and initialising fop types multiple times
	   per service for various colibri_setup commands, So reinitialise
	   fop_type_format for each corresponding service fop types.
	 */
	cs_ds2_fopts[0]->ft_fmt = &cs_ds2_req_fop_tfmt;
	cs_ds2_fopts[1]->ft_fmt = &cs_ds2_rep_fop_tfmt;
	cs_ds2_fopts[0]->ft_fom_type = cs_ds2_req_fop_fom_mopt;
	cs_ds2_fopts[1]->ft_fom_type = cs_ds2_rep_fop_fom_mopt;

        result = c2_fop_type_build_nr(cs_ds2_fopts, ARRAY_SIZE(cs_ds2_fopts));
        if (result != 0)
                c2_cs_ut_ds2_fop_fini();
        return result;
}

/*
  Allocates and initialises a fom.
 */
static int cs_ds_req_fop_fom_create(struct c2_fop *fop,
		const struct c2_fom_ops *ops, struct c2_fom **out)
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

static int cs_ds1_req_fop_fom_create(struct c2_fop *fop, struct c2_fop_ctx *ctx,
                                     struct c2_fom **out)
{
	return cs_ds_req_fop_fom_create(fop, &cs_ds1_req_fop_fom_ops, out);
}

static int cs_ds2_req_fop_fom_create(struct c2_fop *fop, struct c2_fop_ctx *ctx,
                                     struct c2_fom **out)
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

	return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

/*
  Transitions fom through its generic phases and also
  performs corresponding fop specific execution.
 */
static int cs_req_fop_fom_state(struct c2_fom *fom)
{
	int                    rc;
	struct c2_fop         *rfop;
	struct cs_ds1_req_fop *ds1_reqfop;
	struct cs_ds1_rep_fop *ds1_repfop;
	struct cs_ds2_req_fop *ds2_reqfop;
	struct cs_ds2_rep_fop *ds2_repfop;
	uint64_t               opcode;

	C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
	       C2_CS_DS1_REQ_OPCODE ||
	       fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
	       C2_CS_DS2_REQ_OPCODE);

	if (fom->fo_phase < C2_FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		opcode = fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
		switch (opcode) {
		case C2_CS_DS1_REQ_OPCODE:
			rfop = c2_fop_alloc(&cs_ds1_rep_fop_fopt, NULL);
			if (rfop == NULL) {
				fom->fo_phase = C2_FOPH_FINISH;
				return C2_FSO_WAIT;
			}
			ds1_reqfop = c2_fop_data(fom->fo_fop);
			ds1_repfop = c2_fop_data(rfop);
			ds1_repfop->csr_rc = ds1_reqfop->csr_value;
			fom->fo_rep_fop = rfop;
			fom->fo_rc = 0;
			fom->fo_phase = C2_FOPH_SUCCESS;
			rc = C2_FSO_AGAIN;
			break;
		case C2_CS_DS2_REQ_OPCODE:
			rfop = c2_fop_alloc(&cs_ds2_rep_fop_fopt, NULL);
			if (rfop == NULL) {
				fom->fo_phase = C2_FOPH_FINISH;
				return C2_FSO_WAIT;
			}
			ds2_reqfop = c2_fop_data(fom->fo_fop);
			ds2_repfop = c2_fop_data(rfop);
			ds2_repfop->csr_rc = ds2_reqfop->csr_value;
			fom->fo_rep_fop = rfop;
			fom->fo_rc = 0;
			fom->fo_phase = C2_FOPH_SUCCESS;
			rc = C2_FSO_AGAIN;
			break;
		default:
			 C2_ASSERT("Invalid fop" == 0);
		}
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
