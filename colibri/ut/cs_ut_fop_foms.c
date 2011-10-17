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
#include <config.h>
#endif

#include "lib/ut.h"    /* C2_UT_ASSERT */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"

#include "fop/fop.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"

#include "rpc/rpccore.h"
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"

#include "fop/fop_format_def.h"

#include "cs_test_fops_u.h"
#include "cs_test_fops.ff"

static int cs_req_fop_fom_init(struct c2_fop *fop, struct c2_fom **m);

static void rpc_item_reply_cb(struct c2_rpc_item *item, int rc)
{
        C2_PRE(item != NULL);
        C2_PRE(c2_chan_has_waiters(&item->ri_chan));

        c2_chan_signal(&item->ri_chan);
}

/**
   RPC item operations structures.
 */
const struct c2_rpc_item_type_ops cs_req_fop_rpc_item_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = rpc_item_reply_cb,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = NULL,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

/**
   Reply rpc item type operations.
 */
static const struct c2_rpc_item_type_ops cs_rep_fop_rpc_item_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = NULL,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = NULL,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

/**
   Fop type operations.
 */
static const struct c2_fop_type_ops cs_req_fop_type_ops = {
        .fto_fom_init = cs_req_fop_fom_init,
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_op_equal = NULL,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
};

static const struct c2_fop_type_ops cs_rep_fop_type_ops = {
        .fto_fom_init = NULL,
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_op_equal = NULL,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
};

enum {
        CS_REQ = 59,
        CS_REP,
};

/**
   Item type declartaions
 */

static struct c2_rpc_item_type cs_req_fop_rpc_item_type = {
        .rit_opcode = CS_REQ,
        .rit_ops = &cs_req_fop_rpc_item_type_ops,
        .rit_flags = C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO
};
/**
   Reply rpc item type
 */
static struct c2_rpc_item_type cs_rep_fop_rpc_item_type = {
        .rit_opcode = CS_REP,
        .rit_ops = &cs_rep_fop_rpc_item_type_ops,
        .rit_flags = C2_RPC_ITEM_TYPE_REPLY
};

C2_FOP_TYPE_DECLARE_NEW(cs_req_fop, "cs request", CS_REQ,
                        &cs_req_fop_type_ops, &cs_req_fop_rpc_item_type);
C2_FOP_TYPE_DECLARE_NEW(cs_rep_fop, "cs reply", CS_REP,
                        &cs_rep_fop_type_ops, &cs_rep_fop_rpc_item_type);

/**
   Defines fop types array.
 */
static struct c2_fop_type *cs_fopts[] = {
        &cs_req_fop_fopt,
        &cs_rep_fop_fopt
};

/**
   Finalises fop types.
   Invoked from service specific startup function.
 */
void cs_fop_fini(void)
{
        c2_fop_type_fini_nr(cs_fopts, ARRAY_SIZE(cs_fopts));
}

/**
   Builds fop types.
   Invoked from service specific stop function.
 */
int cs_fop_init(void)
{
        int result;
        result = c2_fop_type_build_nr(cs_fopts, ARRAY_SIZE(cs_fopts));
        if (result != 0)
                cs_fop_fini();
        return result;
}

/**
   Fom specific routines for corresponding fops.
 */
static int cs_req_fop_fom_state(struct c2_fom *fom);
static int cs_req_fop_fom_create(struct c2_fom_type *ft, struct c2_fom **out);
static void cs_ut_fom_fini(struct c2_fom *fom);
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom);

/**
 * Operation structures for respective foms
 */
static struct c2_fom_ops cs_req_fop_fom_ops = {
        .fo_fini = cs_ut_fom_fini,
        .fo_state = cs_req_fop_fom_state,
        .fo_home_locality = cs_ut_find_fom_home_locality,
};

/**
   * Fom type operations structures for corresponding foms.
    */
static const struct c2_fom_type_ops cs_req_fop_fom_type_ops = {
        .fto_create = cs_req_fop_fom_create,
};

static struct c2_fom_type cs_req_fop_fom_mopt = {
                .ft_ops = &cs_req_fop_fom_type_ops,
};

static struct c2_fom_type *cs_ut_fom_types[] = {
        &cs_req_fop_fom_mopt
};

static struct c2_fom_type *cs_ut_fom_type_map(c2_fop_type_code_t code)
{
        C2_UT_ASSERT(IS_IN_ARRAY((code - CS_REQ), cs_ut_fom_types));

        return cs_ut_fom_types[code - CS_REQ];
}

/**
   Allocates and initialises a fom.
 */
static int cs_req_fop_fom_create(struct c2_fom_type *ft, struct c2_fom **out)
{
        struct c2_fom *fom;

	C2_PRE(ft != NULL);
        C2_PRE(out != NULL);

        C2_ALLOC_PTR(fom);
        if (fom == NULL)
                return -ENOMEM;
        fom->fo_type = ft;

        fom->fo_ops = &cs_req_fop_fom_ops;
        *out = fom;

        return 0;
}

/**
   Initialises a fom.
 */
static int cs_req_fop_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom_type      *fom_type;
        int                      result;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        fom_type = cs_ut_fom_type_map(fop->f_type->ft_code);
        if (fom_type == NULL)
                return -EINVAL;
        fop->f_type->ft_fom_type = *fom_type;
        result = fop->f_type->ft_fom_type.ft_ops->fto_create(&(fop->f_type->ft_fom_type), m);
        if (result == 0) {
                (*m)->fo_fop = fop;
                c2_fom_init(*m);
        }

        return result;

}

/**
   Finalises a fom.
 */
static void cs_ut_fom_fini(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

        c2_fom_fini(fom);
        c2_free(fom);
}

/**
   Returns an index value base on fom parameters to locate fom's
   home locality to execute a fom.
 */
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom)
{
	return fom->fo_fop->f_type->ft_code;
}

/**
   Transitions fom through its generic phases and also
   performs corresponding fop specific execution.
 */
static int cs_req_fop_fom_state(struct c2_fom *fom)
{
	int                  rc;
	struct cs_req_fop   *reqfop;
	struct cs_rep_fop   *repfop;
	struct c2_fop       *rfop;
	struct c2_rpc_item  *item;

	C2_PRE(fom->fo_fop->f_type->ft_code == CS_REQ);

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		rfop = c2_fop_alloc(&cs_rep_fop_fopt, NULL);
		if (rfop == NULL) {
			fom->fo_phase = FOPH_FINISH;
			return FSO_WAIT;
		}
		reqfop = c2_fop_data(fom->fo_fop);
		repfop = c2_fop_data(rfop);
		item = c2_fop_to_rpc_item(rfop);
		c2_rpc_item_init(item);
		item->ri_type = &cs_rep_fop_rpc_item_type;
		item->ri_group = NULL;
		rfop->f_type->ft_ri_type = &cs_rep_fop_rpc_item_type;

		repfop->csr_rc = reqfop->csr_value;
		fom->fo_rep_fop = rfop;
		fom->fo_rc = 0;
		fom->fo_phase = FOPH_SUCCESS;
		rc = FSO_AGAIN;
	}

	return rc;
}

/**
   Sends fops to server.
 */
void send_fops(struct c2_rpc_session *cl_rpc_session)
{
        struct c2_clink          clink[10];
        struct c2_rpc_item      *item;
        struct c2_fop           *fop[10];
        struct cs_req_fop       *cs_fop;
        struct c2_fop_type      *ftype;
        c2_time_t                timeout;
        uint32_t                 i;

	/* Send fops */
        for (i = 0; i < 10; ++i) {
                fop[i] = c2_fop_alloc(&cs_req_fop_fopt, NULL);
                cs_fop = c2_fop_data(fop[i]);
                cs_fop->csr_value = i;

                item = &fop[i]->f_item;
                c2_rpc_item_init(item);
                item->ri_deadline = 0;
                item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
                item->ri_group = NULL;
                item->ri_type = &cs_req_fop_rpc_item_type;
                ftype = fop[i]->f_type;
                ftype->ft_ri_type = &cs_req_fop_rpc_item_type;
                item->ri_session = cl_rpc_session;
                c2_time_set(&timeout, 60, 0);
                c2_clink_init(&clink[i], NULL);
                c2_clink_add(&item->ri_chan, &clink[i]);
                timeout = c2_time_add(c2_time_now(), timeout);
                c2_rpc_post(item);
	}

	/* Wait for replys */
        for (i = 0; i < 10; ++i) {
                c2_rpc_reply_timedwait(&clink[i], timeout);
                c2_clink_del(&clink[i]);
                c2_clink_fini(&clink[i]);
        }
}

/** @} endgroup colibri_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

