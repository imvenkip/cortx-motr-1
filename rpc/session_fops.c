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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>,
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/15/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"        /* m0_net_end_point_get */
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "dtm/verno_xc.h" /* m0_xc_verno_init */
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   This file contains definitions of fop types and rpc item types, of fops
   belonging to rpc-session module
 */

static void conn_establish_fop_release(struct m0_ref *ref)
{
	struct m0_rpc_fop_conn_establish_ctx *ctx;
	struct m0_fop                        *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	ctx = container_of(fop, struct m0_rpc_fop_conn_establish_ctx, cec_fop);
	m0_fop_fini(fop);
	if (ctx->cec_sender_ep != NULL)
		/* For all conn-establish items a reference to sender_ep
		   is taken during m0_rpc_fop_conn_establish_ctx_init()
		 */
		m0_net_end_point_put(ctx->cec_sender_ep);
	m0_free(ctx);
}

static int conn_establish_item_decode(const struct m0_rpc_item_type *item_type,
				      struct m0_rpc_item           **item,
				      struct m0_bufvec_cursor       *cur)
{
	struct m0_rpc_fop_conn_establish_ctx *ctx;
	struct m0_fop                        *fop;
	int                                   rc;

	M0_ENTRY("item_opcode: %u", item_type->rit_opcode);
	M0_PRE(item_type != NULL && item != NULL && cur != NULL);
	M0_PRE(item_type->rit_opcode == M0_RPC_CONN_ESTABLISH_OPCODE);

	*item = NULL;

	RPC_ALLOC_PTR(ctx, SESSION_FOP_CONN_ESTABLISH_ITEM_DECODE, &m0_rpc_addb_ctx);
	if (ctx == NULL)
		M0_RETURN(-ENOMEM);

	ctx->cec_sender_ep = NULL;
	fop = &ctx->cec_fop;

	/**
	   No need to allocate fop->f_data.fd_data since xcode allocates
	   top level object also.
	 */
	m0_fop_init(fop, &m0_rpc_fop_conn_establish_fopt, NULL,
		    conn_establish_fop_release);

	rc = m0_fop_item_encdec(&fop->f_item, cur, M0_XCODE_DECODE);
	if (rc == 0)
		*item = &fop->f_item;
	else
		m0_fop_put(fop);

	M0_RETURN(rc);
}

const struct m0_fop_type_ops m0_rpc_fop_noop_ops = {
};

static struct m0_rpc_item_type_ops conn_establish_item_type_ops = {
	M0_FOP_DEFAULT_ITEM_TYPE_OPS,
	.rito_decode       = conn_establish_item_decode,
};

struct m0_fop_type m0_rpc_fop_conn_establish_fopt;
struct m0_fop_type m0_rpc_fop_conn_establish_rep_fopt;
struct m0_fop_type m0_rpc_fop_conn_terminate_fopt;
struct m0_fop_type m0_rpc_fop_conn_terminate_rep_fopt;
struct m0_fop_type m0_rpc_fop_session_establish_fopt;
struct m0_fop_type m0_rpc_fop_session_establish_rep_fopt;
struct m0_fop_type m0_rpc_fop_session_terminate_fopt;
struct m0_fop_type m0_rpc_fop_session_terminate_rep_fopt;
struct m0_fop_type m0_rpc_fop_noop_fopt;

static struct m0_fop_type *fop_types[] = {
	&m0_rpc_fop_conn_establish_fopt,
	&m0_rpc_fop_conn_terminate_fopt,
	&m0_rpc_fop_session_establish_fopt,
	&m0_rpc_fop_session_terminate_fopt,
	&m0_rpc_fop_conn_establish_rep_fopt,
	&m0_rpc_fop_conn_terminate_rep_fopt,
	&m0_rpc_fop_session_establish_rep_fopt,
	&m0_rpc_fop_session_terminate_rep_fopt,
	&m0_rpc_fop_noop_fopt,
};

M0_INTERNAL void m0_rpc_session_fop_fini(void)
{
	m0_fop_type_fini(&m0_rpc_fop_noop_fopt);
	m0_fop_type_fini(&m0_rpc_fop_session_terminate_rep_fopt);
	m0_fop_type_fini(&m0_rpc_fop_session_establish_rep_fopt);
	m0_fop_type_fini(&m0_rpc_fop_conn_terminate_rep_fopt);
	m0_fop_type_fini(&m0_rpc_fop_conn_establish_rep_fopt);
	m0_fop_type_fini(&m0_rpc_fop_session_terminate_fopt);
	m0_fop_type_fini(&m0_rpc_fop_session_establish_fopt);
	m0_fop_type_fini(&m0_rpc_fop_conn_terminate_fopt);
	m0_fop_type_fini(&m0_rpc_fop_conn_establish_fopt);
	m0_xc_session_fops_fini();
	m0_xc_rpc_onwire_fini();
	m0_xc_verno_fini();
}

extern struct m0_fom_type_ops m0_rpc_fom_conn_establish_type_ops;
extern struct m0_fom_type_ops m0_rpc_fom_session_establish_type_ops;
extern struct m0_fom_type_ops m0_rpc_fom_conn_terminate_type_ops;
extern struct m0_fom_type_ops m0_rpc_fom_session_terminate_type_ops;
extern struct m0_reqh_service_type m0_rpc_service_type;
M0_INTERNAL int m0_rpc_session_fop_init(void)
{
	m0_xc_session_fops_init();
	M0_FOP_TYPE_INIT(&m0_rpc_fop_conn_establish_fopt,
			 .name      = "Rpc conn establish",
			 .opcode    = M0_RPC_CONN_ESTABLISH_OPCODE,
			 .xt        = m0_rpc_fop_conn_establish_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .rpc_ops   = &conn_establish_item_type_ops,
			 .fom_ops   = &m0_rpc_fom_conn_establish_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_conn_terminate_fopt,
			 .name      = "Rpc conn terminate",
			 .opcode    = M0_RPC_CONN_TERMINATE_OPCODE,
			 .xt        = m0_rpc_fop_conn_terminate_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &m0_rpc_fom_conn_terminate_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_session_establish_fopt,
			 .name      = "Rpc session establish",
			 .opcode    = M0_RPC_SESSION_ESTABLISH_OPCODE,
			 .xt        = m0_rpc_fop_session_establish_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &m0_rpc_fom_session_establish_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_session_terminate_fopt,
			 .name      = "Rpc session terminate",
			 .opcode    = M0_RPC_SESSION_TERMINATE_OPCODE,
			 .xt        = m0_rpc_fop_session_terminate_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &m0_rpc_fom_session_terminate_type_ops,
			 .sm        = &m0_generic_conf,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_conn_establish_rep_fopt,
			 .name      = "Rpc conn establish reply",
			 .opcode    = M0_RPC_CONN_ESTABLISH_REP_OPCODE,
			 .xt        = m0_rpc_fop_conn_establish_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_conn_terminate_rep_fopt,
			 .name      = "Rpc conn terminate reply",
			 .opcode    = M0_RPC_CONN_TERMINATE_REP_OPCODE,
			 .xt        = m0_rpc_fop_conn_terminate_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_session_establish_rep_fopt,
			 .name      = "Rpc session establish reply",
			 .opcode    = M0_RPC_SESSION_ESTABLISH_REP_OPCODE,
			 .xt        = m0_rpc_fop_session_establish_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_session_terminate_rep_fopt,
			 .name      = "Rpc session terminate reply",
			 .opcode    = M0_RPC_SESSION_TERMINATE_REP_OPCODE,
			 .xt        = m0_rpc_fop_session_terminate_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .svc_type  = &m0_rpc_service_type);
	M0_FOP_TYPE_INIT(&m0_rpc_fop_noop_fopt,
			 .name      = "No-op",
			 .opcode    = M0_RPC_NOOP_OPCODE,
			 .xt        = m0_rpc_fop_noop_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .fop_ops   = &m0_rpc_fop_noop_ops,
			 .svc_type  = &m0_rpc_service_type);
	return 0;
}

M0_INTERNAL void m0_rpc_fop_conn_establish_ctx_init(struct m0_rpc_item *item,
						    struct m0_net_end_point *ep)
{
	struct m0_rpc_fop_conn_establish_ctx *ctx;

	M0_ENTRY("item: %p, ep_addr: %s", item, (char *)ep->nep_addr);
	M0_PRE(item != NULL && ep != NULL);

	ctx = container_of(item, struct m0_rpc_fop_conn_establish_ctx,
				cec_fop.f_item);
	/* This reference will be dropped when the item is getting freed i.e.
	   conn_establish_fop_release()
	 */
	m0_net_end_point_get(ep);
	ctx->cec_sender_ep = ep;
	M0_LEAVE();
}

M0_INTERNAL bool m0_rpc_item_is_control_msg(const struct m0_rpc_item *item)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fop_types); i++) {
		if (item->ri_type->rit_opcode ==
		    fop_types[i]->ft_rpc_item_type.rit_opcode)
			return true;
	}
	return false;
}

/** @} End of rpc_session group */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
