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
#include "clovis/sync.h"

#include "lib/errno.h"
#include "lib/finject.h"
#include "fid/fid.h"             /* m0_fid */
#include "fop/fom_generic.h"     /* m0_rpc_item_is_generic_reply_fop */
#include "ioservice/fid_convert.h" /* m0_fid_convert_ */
#include "ioservice/io_device.h" /* M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH */
#include "mdservice/md_fops.h"
#include "rpc/rpclib.h"
#include "rpc/rpc_opcodes.h"     /* M0_MDSERVICE_CREATE_OPCODE */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CLOVIS
#include "lib/trace.h"

#define CLOVIS_OSYNC

struct clovis_ios_cob_req {
	struct clovis_cob_req    *icr_cr;
	struct m0_clovis_ast_rc   icr_ar;
	uint32_t                  icr_index;
	uint64_t                  icr_magic;
};

enum clovis_cob_request_flags {
	CLOVIS_COB_REQ_ASYNC = (1 << 1),
	CLOVIS_COB_REQ_SYNC  = (1 << 2),
};

/**
 * Clovis cob request
 */
struct clovis_cob_req {
	struct m0_clovis_op_obj  *cr_oo;
	struct m0_clovis         *cr_cinst;
	uint32_t                  cr_opcode;
	struct m0_fid             cr_fid;
	struct m0_buf             cr_name;
	struct m0_fop            *cr_mds_fop;
	struct m0_fop            *cr_rep_fop;
	struct m0_clovis_ast_rc   cr_ar;
	uint32_t                  cr_cob_type;
	uint32_t                  cr_flags;
	struct m0_fid             cr_pver;
	uint32_t                  cr_icr_nr;
	struct m0_fop           **cr_ios_fop;
	bool                     *cr_ios_completed;
	uint64_t                  cr_magic;
};

/**
 * Cob create/delete fop send deadline (in ns).
 * Used to utilize RPC formation when too many fops
 * are sending to one ioservice.
 */
enum {IOS_COB_REQ_DEADLINE = 2000000};

const struct m0_bob_type cr_bobtype;
M0_BOB_DEFINE(M0_INTERNAL, &cr_bobtype, clovis_cob_req);
const struct m0_bob_type cr_bobtype = {
	.bt_name         = "clovis_cr_bobtype",
	.bt_magix_offset = offsetof(struct clovis_cob_req, cr_magic),
	.bt_magix        = M0_CLOVIS_CR_MAGIC,
	.bt_check        = NULL,
};

static const struct m0_bob_type icr_bobtype;
M0_BOB_DEFINE(static, &icr_bobtype, clovis_ios_cob_req);
static const struct m0_bob_type icr_bobtype = {
	.bt_name         = "icr_bobtype",
	.bt_magix_offset = offsetof(struct clovis_ios_cob_req, icr_magic),
	.bt_magix        = M0_CLOVIS_ICR_MAGIC,
	.bt_check        = NULL,
};

static int clovis_cob_ios_send(struct clovis_cob_req *cob_req, uint32_t idx);
static void clovis_cob_ast_ios_io_send(struct m0_sm_group *grp,
					struct m0_sm_ast  *ast);
static void clovis_cob_body_mem2wire(struct m0_fop_cob     *body,
				     struct m0_cob_attr    *attr,
				     int                    valid,
				     struct clovis_cob_req *cr);
static void clovis_cob_attr_init(uint32_t             op_code,
				 struct m0_cob_attr  *attr,
				 int                 *valid);
/**
 * Checks an IOS COB request is not malformed or corrupted.
 *
 * @param icr IOS COB request to be checked.
 * @return true if the operation is not malformed or false if some error
 * was detected.
 */
static bool clovis_ios_cob_req_invariant(struct clovis_ios_cob_req *icr)
{
	return M0_RC(icr != NULL &&
		     clovis_ios_cob_req_bob_check(icr));
}

/**
 * Copies a name into a m0_fop_str.
 *
 * @param tgt dest buffer.
 * @param name src buffer.
 * @return 0 if success or -ENOMEM if the dest buffer could not be initialised.
 */
static int clovis_cob_name_mem2wire(struct m0_fop_str *tgt,
				    const struct m0_buf *name)
{
	M0_ENTRY();

	M0_PRE(tgt != NULL);
	M0_PRE(name != NULL);

	tgt->s_buf = m0_alloc(name->b_nob);
	if (tgt->s_buf == NULL)
		return M0_ERR(-ENOMEM);

	memcpy(tgt->s_buf, name->b_addr, (int)name->b_nob);
	tgt->s_len = name->b_nob;

	return M0_RC(0);
}

/**
 * Fills a m0_fop_cob so it can be sent to a mdservice.
 *
 * @param body cob fop to be filled.
 * @param oo obj operation the information is retrieved from.
 */
static void clovis_cob_body_mem2wire(struct m0_fop_cob *body,
				 struct m0_cob_attr    *attr,
				 int                    valid,
				 struct clovis_cob_req *cr)
{
	M0_PRE(body != NULL);
	M0_PRE(cr != NULL);

	body->b_tfid = cr->cr_fid;
#ifdef CLOVIS_FOR_M0T1FS
	body->b_pfid = cr->cr_cinst->m0c_root_fid;
#endif

	if (valid & M0_COB_PVER)
		body->b_pver = cr->cr_pver;
	if (valid & M0_COB_NLINK)
		body->b_nlink = attr->ca_nlink;
	body->b_valid |= valid;
}

static void clovis_cob_attr_init(uint32_t            op_code,
			         struct m0_cob_attr *attr,
				 int                *valid)
{
	switch (op_code) {
		case M0_CLOVIS_EO_CREATE:
			/* mds requires nlink > 0 */
			attr->ca_nlink = 1;
			*valid = M0_COB_NLINK | M0_COB_PVER;
			break;
		case M0_CLOVIS_EO_DELETE:
			attr->ca_nlink = 0;
			*valid = M0_COB_NLINK;
			break;
		case M0_CLOVIS_EO_GETATTR:
			memset(attr, 0, sizeof *attr);
			*valid = 0;
			break;
		default:
			M0_IMPOSSIBLE("Operation not supported");
	}
}

/**
 * Allocates and initializes object of clovis_cob_req structures.
 *
 * @param cinst clovis instance.
 * @returns instance of clovis_cob_req if operation succeeds, NULL otherwise.
 */
static struct clovis_cob_req *clovis_cob_req_alloc(struct m0_pool_version *pv)
{
	struct clovis_cob_req *cr;
	uint32_t               pool_width;

	cr = m0_alloc(sizeof *cr);
	if (cr == NULL)
		return NULL;

	clovis_cob_req_bob_init(cr);
	m0_clovis_ast_rc_bob_init(&cr->cr_ar);

	/* Initialises and sets FOP related members. */
	cr->cr_pver = pv->pv_id;
	pool_width  = pv->pv_attr.pa_P;
	M0_ALLOC_ARR(cr->cr_ios_fop, pool_width);
	M0_ALLOC_ARR(cr->cr_ios_completed, pool_width);
	if (cr->cr_ios_fop == NULL || cr->cr_ios_completed == NULL) {
		m0_free(cr->cr_ios_fop);
		m0_free(cr->cr_ios_completed);
		m0_free(cr);
		return NULL;
	}
	return cr;
}

/**
 * Frees clovis_cob_req object
 */
static void clovis_cob_req_free(struct clovis_cob_req *cr)
{
	int i;

	M0_ASSERT(cr != NULL);
	M0_ASSERT(cr->cr_cinst != NULL);

	m0_clovis_ast_rc_bob_fini(&cr->cr_ar);
	clovis_cob_req_bob_fini(cr);

	/* XXX: Can someone else access cr-> fields? do we need to lock? */
	/* Release the mds fop. */
	if (cr->cr_mds_fop != NULL) {
		m0_fop_put_lock(cr->cr_mds_fop);
		cr->cr_mds_fop = NULL;
	}

	/* Release the ios fops. */
	if (cr->cr_ios_fop != NULL) {
		for (i = 0; i < cr->cr_icr_nr; ++i) {
			if (cr->cr_ios_fop[i] != NULL) {
				m0_clovis_cob_ios_fop_fini(cr->cr_ios_fop[i]);
				cr->cr_ios_fop[i] = NULL;
			}
		}
		m0_free0(&cr->cr_ios_fop);
		cr->cr_ios_fop = NULL;
	}

	if (cr->cr_ios_completed != NULL) {
		m0_free0(&cr->cr_ios_completed);
		cr->cr_ios_completed = NULL;
	}
	m0_free(cr);
}

/**
 * Completes an object operation by moving the state of all its state machines
 * to successful states.
 *
 * @param oo object operation being completed.
 */
static void clovis_cob_complete_oo(struct m0_clovis_op_obj *oo)
{
	struct m0_clovis_op *op;
	struct m0_sm_group  *op_grp;
	struct m0_sm_group  *en_grp;

	M0_ENTRY();

	M0_PRE(oo != NULL);
	M0_PRE(m0_sm_group_is_locked(oo->oo_sm_grp));
	op = &oo->oo_oc.oc_op;
	op_grp = &op->op_sm_group;
	en_grp = &op->op_entity->en_sm_group;

	m0_sm_group_lock(en_grp);
	M0_LOG(M0_DEBUG, "entity sm state: %p, %d\n",
	       &oo->oo_oc.oc_op.op_entity->en_sm,
	       oo->oo_oc.oc_op.op_entity->en_sm.sm_state);
	m0_sm_move(&oo->oo_oc.oc_op.op_entity->en_sm, 0, M0_CLOVIS_ES_INIT);
	m0_sm_group_unlock(en_grp);

	m0_sm_group_lock(op_grp);
	m0_sm_move(&op->op_sm, 0, M0_CLOVIS_OS_EXECUTED);
	/* TODO Callbacks */
	m0_clovis_op_executed(op);
	/* XXX: currently we do this straightaway */
	m0_sm_move(&op->op_sm, 0, M0_CLOVIS_OS_STABLE);
	m0_clovis_op_stable(op);
	/* TODO Callbacks */
	m0_sm_group_unlock(op_grp);

	M0_LEAVE();
}

/**
 * Fails a whole object operation and moves the state of its state machines to
 * failed or error states.
 *
 * @param oo object operation to be failed.
 * @param rc error code that explains why the operation is being failed.
 */
static void clovis_cob_fail_oo(struct m0_clovis_op_obj *oo, int rc)
{
	struct m0_clovis_op *op;
	struct m0_sm_group  *op_grp;
	struct m0_sm_group  *en_grp;

	M0_ENTRY();

	M0_PRE(rc != 0);
	M0_PRE(m0_sm_group_is_locked(oo->oo_sm_grp));

	op = &oo->oo_oc.oc_op;
	op_grp = &op->op_sm_group;
	en_grp = &op->op_entity->en_sm_group;

	/* Avoid cancelling rpc items and setting op's state multiple times. */
	if (op->op_sm.sm_rc != 0)
		goto out;

	/*
	 * Move the state machines: op and entity.
	 *
	 * XXX(Sining): entity state is set to FAILED here, what will happen if
	 * this entity is re-used for a new op? The entity has to be reset
	 * before re-use in this case otherwise clovis_entity_namei_op and
	 * m0_clovis_obj_op_prepare will have trouble as they assume the state
	 * of entity has to be INIT. Or does it make sense to ignore the state
	 * checking in these 2 functions?
	 *
	 * What is the point of having states for an entity? Does it make sense
	 * to remove them totally?
	 */
	m0_sm_group_lock(en_grp);
	m0_sm_fail(&op->op_entity->en_sm, M0_CLOVIS_ES_FAILED, rc);
	m0_sm_group_unlock(en_grp);

	m0_sm_group_lock(op_grp);
	m0_sm_fail(&op->op_sm, M0_CLOVIS_OS_FAILED, rc);
	m0_clovis_op_failed(op);
	m0_sm_group_unlock(op_grp);

out:
	M0_LEAVE();
}

/**----------------------------------------------------------------------------*
 *                           COB FOP's for ioservice                           *
 *-----------------------------------------------------------------------------*/

/**
 * Retrieves the clovis_ios_cob_req an rpc item is associated to. This allows
 * figuring out which slot inside the object operation corresponds to the
 * communication with a specific ioservice.
 *
 * @param item RPC item used to communicate to a specific ioservice. It contains
 * both the request sent from clovis and the reply received from the ioservice.
 * @return a clovis_ios_cob_req, which points to a slot within an object
 * operation.
 */
static struct clovis_ios_cob_req *
rpc_item_to_cob_req(struct m0_rpc_item *item)
{
	struct m0_fop                  *fop;
	struct clovis_ios_cob_req   *icr;

	M0_ENTRY();
	M0_PRE(item != NULL);

	fop = m0_rpc_item_to_fop(item);
	icr = (struct clovis_ios_cob_req *)fop->f_opaque;

	M0_LEAVE();
	return icr;
}

/**
 * AST callback that marks a m0_clovis_op_obj to reflect a COB fop has been
 * fully completed by an ioservice.
 * If the rest of the create/delete cob requests have been completed too, the
 * whole m0_clovis_op_obj gets tagged as EXECUTED; the operation has finished.
 *
 * @param grp group the AST is executed in.
 * @param ast callback being executed.
 */
static void clovis_icr_ast_complete(struct m0_sm_group *grp,
				    struct m0_sm_ast *ast)
{
	uint32_t                   i;
	uint32_t                   icr_nr;
	uint32_t                   cob_type;
	struct m0_clovis_ast_rc   *ar;
	struct clovis_cob_req     *cr;
	struct clovis_ios_cob_req *icr;
	struct m0_clovis          *cinst;
	bool                       completed = true;

	M0_ENTRY();

	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	M0_PRE(ast != NULL);

	ar = bob_of(ast, struct m0_clovis_ast_rc, ar_ast, &ar_bobtype);
	M0_PRE(m0_clovis_op_obj_ast_rc_invariant(ar));
	icr = bob_of(ar, struct clovis_ios_cob_req, icr_ar, &icr_bobtype);
	M0_PRE(clovis_ios_cob_req_invariant(icr));
	M0_ASSERT(ar->ar_rc == 0);

	cr = icr->icr_cr;
	M0_ASSERT(cr != NULL);

	icr_nr = cr->cr_icr_nr;
	M0_ASSERT(icr->icr_index < icr_nr);
	cob_type = cr->cr_cob_type;
	M0_ASSERT(M0_IN(cob_type, (M0_COB_IO, M0_COB_MD)));
	cinst = cr->cr_cinst;
	M0_ASSERT(cinst != NULL);

	/* Mark this single create/delete cob fop. */
	cr->cr_ios_completed[icr->icr_index] = true;

	/* Are the other fops completed? */
	i = 0;
	while (i < icr_nr &&(completed = cr->cr_ios_completed[i]))
		++i;

	if (!completed)
		goto out;

	if (cob_type == M0_COB_IO ||
	    cr->cr_oo->oo_oc.oc_op.op_code == M0_CLOVIS_EO_CREATE ) {
		/* Last create/delete cob fop to complete? */
		clovis_cob_complete_oo(cr->cr_oo);
		/* Free CR */
		clovis_cob_req_free(cr);
	} else {
		/*
		 * M0_COB_MD
		 * Just finish creating metadata in selsected io services,
		 * start the 2nd phase now (to prepare COB in all io services).
		 */
		for (i = 0; i < icr_nr; i++)
			cr->cr_ios_completed[i] = false;
		cr->cr_ar.ar_ast.sa_cb = &clovis_cob_ast_ios_io_send;
		m0_sm_ast_post(cr->cr_oo->oo_sm_grp, &cr->cr_ar.ar_ast);
	}

out:
	M0_LEAVE();
}

/**
 * AST callback to fail the whole m0_clovis_op_obj a
 * clovis_ios_cob_req is associated to.
 *
 * @param grp group the AST is executed in.
 * @param ast callback being executed.
 */
static void clovis_icr_ast_fail(struct m0_sm_group *grp,
				    struct m0_sm_ast *ast)
{
	int                        i;
	uint32_t                   icr_nr;
	struct clovis_ios_cob_req *icr;
	struct m0_clovis_ast_rc   *ar;
	struct clovis_cob_req     *cr;

	M0_ENTRY();

	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	M0_PRE(ast != NULL);

	ar = bob_of(ast, struct m0_clovis_ast_rc, ar_ast, &ar_bobtype);
	icr = bob_of(ar, struct clovis_ios_cob_req, icr_ar, &icr_bobtype);
	M0_ASSERT(ar->ar_rc != 0);

	cr = icr->icr_cr;

	/* Cancel any pending fop for this op. */
	icr_nr = cr->cr_icr_nr;
	for (i = 0; i < icr_nr; ++i)
		if (!cr->cr_ios_completed[i] && cr->cr_ios_fop[i] != NULL)
			m0_rpc_item_cancel(&cr->cr_ios_fop[i]->f_item);

	/* Have to fail the whole op. */
	clovis_cob_fail_oo(icr->icr_cr->cr_oo, ar->ar_rc);
	/* Free CR */
	clovis_cob_req_free(cr);
	M0_LEAVE();
}

/**
 * rio_replied RPC callback to be executed whenever a reply to a create/delete
 * cob fop is received from an ioservice.
 *
 * @param item RPC item used to communicate to the ioservice.
 */
static void clovis_cob_ios_rio_replied(struct m0_rpc_item *item)
{
	struct clovis_ios_cob_req  *icr;
	struct m0_fop              *rep_fop;
	struct m0_fop_cob_op_reply *cob_rep;
	struct clovis_cob_req      *cr;
	int                         rc;

	M0_ENTRY();

	M0_PRE(item != NULL);

	icr = rpc_item_to_cob_req(item);
	M0_ASSERT(icr != NULL);
	M0_ASSERT(icr->icr_cr != NULL);
	M0_ASSERT(icr->icr_ar.ar_rc == 0);

	cr = icr->icr_cr;
	/* Failure in rpc? */
	rc = m0_rpc_item_error(item);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "rpc item error = %d", rc);
		goto error;
	}

	/*
 	 * According to m0_rpc_item_error(), it returns 0 when item->ri_reply
 	 * == NULL, this doesn't look right for replies of cob operations.
 	 * We enforce an assert here to ensure it isn't NULL.
 	 */
	M0_ASSERT(item->ri_reply != NULL);
	rep_fop = m0_rpc_item_to_fop(item->ri_reply);
	cob_rep = m0_fop_data(rep_fop);

	/* Failure in operation specific phase? */
	rc = cob_rep->cor_rc;
	if (rc != 0)
		goto error;

	/* Complete this ios create/delete cob request. */
	icr->icr_ar.ar_ast.sa_cb = &clovis_icr_ast_complete;
	m0_sm_ast_post(cr->cr_oo->oo_sm_grp, &icr->icr_ar.ar_ast);

	M0_LEAVE();
	return;

error:
	/*
	 * Note: each icr is only associated to one single
	 * ioservice/rpc_item so only one component should be accessing
	 * it at the same time
	 */
	M0_ASSERT(rc != 0);
	icr->icr_ar.ar_rc = rc;
	icr->icr_ar.ar_ast.sa_cb = &clovis_icr_ast_fail;
	m0_sm_ast_post(icr->icr_cr->cr_oo->oo_sm_grp, &icr->icr_ar.ar_ast);

	M0_LEAVE();
}

/**
 * RPC callbacks for the posting of COB FOPs to ioservices.
 */
static const struct m0_rpc_item_ops clovis_cob_ios_ri_ops = {
	.rio_replied = clovis_cob_ios_rio_replied,
};

M0_INTERNAL struct m0_rpc_session*
m0_clovis_obj_container_id_to_session(struct m0_pool_version *pver,
				      uint64_t container_id)
{
	struct m0_reqh_service_ctx *ios_ctx;

	M0_ENTRY();

	M0_PRE(pver != NULL);
	M0_PRE(container_id < pver->pv_pc->pc_nr_devices);

	ios_ctx = pver->pv_pc->pc_dev2svc[container_id].pds_ctx;
	M0_ASSERT(ios_ctx != NULL);
	M0_ASSERT(ios_ctx->sc_type == M0_CST_IOS);

	if (M0_FI_ENABLED("rpc_session_cancel")) {
		m0_rpc_session_cancel(&ios_ctx->sc_rlink.rlk_sess);
	}

	M0_LEAVE();
	return &ios_ctx->sc_rlink.rlk_sess;
}

/**
 * Populates a fop with the information contained within an object operation.
 * The fop will be sent to an ioservice to request the creation or deletion
 * of a cob.
 *
 * @param oo object operation.
 * @param fop fop being populated.
 * @param cob_fid fid of the cob being created/deleted.
 * @param cob_idx index of the cob inside the object's layout.
 */
static int clovis_cob_ios_fop_populate(struct clovis_cob_req *cr,
				       struct m0_fop        *fop,
				       struct m0_fid        *cob_fid,
				       uint32_t              cob_idx)
{
	int                          valid = 0;
	struct m0_cob_attr           attr;
	struct m0_fop_cob_common    *common;
	struct m0_clovis            *cinst;
	uint32_t                     cob_type;

	M0_ENTRY();

	M0_PRE(cr != NULL);
	M0_PRE(fop != NULL);
	M0_PRE(cob_fid != NULL);

	cob_type = cr->cr_cob_type;

	common = m0_cobfop_common_get(fop);
	M0_ASSERT(common != NULL);

	cinst = cr->cr_cinst;
	M0_ASSERT(cinst != NULL);

	/*
	 * Fill the m0_fop_cob_common. Note: commit c5ba7b47f68 introduced
	 * attributes in a ios cob fop (struct m0_fop_cob c_body), they
	 * have to be set properly before sent on wire.
	 */
	clovis_cob_attr_init(cr->cr_opcode, &attr, &valid);

	clovis_cob_body_mem2wire(&common->c_body, &attr, valid, cr);

	common->c_gobfid = cr->cr_fid;
	common->c_cobfid = *cob_fid;
	common->c_pver   =  cr->cr_pver;
	common->c_cob_type = cob_type;

	/* For special "meta cobs", this would be some special value,
	 * e.g. -1. @todo this will be done in MM hash function task.
	 */
	common->c_cob_idx = cob_idx;

	/* COB may not be created yet  */
	if (fop->f_type != &m0_fop_cob_getattr_fopt)
		common->c_flags |= M0_IO_FLAG_CROW;

	return M0_RC(0);
}

M0_INTERNAL void m0_clovis_cob_ios_fop_fini(struct m0_fop *ios_fop)
{
	struct clovis_ios_cob_req *icr;
	M0_PRE(ios_fop != NULL);

	M0_ENTRY();

	if (ios_fop->f_opaque != NULL) {
		icr = (struct clovis_ios_cob_req *)ios_fop->f_opaque;
		m0_clovis_ast_rc_bob_fini(&icr->icr_ar);
		clovis_ios_cob_req_bob_fini(icr);
		m0_free(icr);
	}

	/*
	 * m0_rpc_item_cancel may have already put the fop and finalised the rpc
	 * item which this fop links to. This may leave the rpc_mach == NULL.
	 */
	if (m0_fop_rpc_machine(ios_fop) != NULL)
		m0_fop_put_lock(ios_fop);

	M0_LEAVE();
}

/**
 * Allocates a cob fop to an ioservice.
 *
 * @param oo object operation being processed.
 * @param i index of the cob.
 */
static struct m0_fop *clovis_cob_ios_fop_get(struct clovis_cob_req *cr,
					     uint32_t i, uint32_t cob_type,
					     struct m0_rpc_session *session)
{
	struct m0_fop                *fop;
	struct m0_fop_type           *ftype = NULL; /* required */
	struct clovis_ios_cob_req *icr;

	if (cr->cr_ios_fop[i] != NULL) {
		/*
		 * oo_ios_fop[i] != NULL could happen in the following 2 cases:
		 * (1) resending a fop when Clovis has out of date pool version,
		 * (2) sending ios cob io fops after ios cob md fops in oostore
		 *     mode.
		 * In the case 2, fops for cob md and io have different values
		 * like index. To make thing simple now, fop is freed first
		 * then allocated.
		 *
		 * TODO: revisit to check if we can re-use fop.
		 */
		fop = cr->cr_ios_fop[i];
		M0_ASSERT(fop->f_opaque != NULL);

		m0_clovis_cob_ios_fop_fini(fop);
		cr->cr_ios_fop[i] = NULL;
	}

	/* Select the fop type to be sent to the ioservice. */
	switch (cr->cr_opcode) {
		case M0_CLOVIS_EO_CREATE:
			ftype = &m0_fop_cob_create_fopt;
			break;
		case M0_CLOVIS_EO_DELETE:
			ftype = &m0_fop_cob_delete_fopt;
			break;
		case M0_CLOVIS_EO_GETATTR:
			ftype = &m0_fop_cob_getattr_fopt;
			break;
		default:
			M0_IMPOSSIBLE("Operation not supported");
	}

	/* Allocate a cob fop. */
	fop = m0_fop_alloc_at(session, ftype);
	if (fop == NULL)
		goto error;

	/* The rpc's callback must know which oo's slot they work on. */
	if (cr->cr_flags & CLOVIS_COB_REQ_ASYNC) {
		M0_ALLOC_PTR(icr);
		if (icr == NULL)
			goto error;
		clovis_ios_cob_req_bob_init(icr);
		m0_clovis_ast_rc_bob_init(&icr->icr_ar);
		icr->icr_index = i;
		icr->icr_cr = cr;
		fop->f_opaque = icr;
	}
	cr->cr_ios_fop[i] = fop;
	return fop;
error:
	if (fop != NULL) {
		m0_clovis_cob_ios_fop_fini(cr->cr_ios_fop[i]);
		cr->cr_ios_fop[i] = NULL;
	}

	return NULL;
}

/**
 * Sends a COB fop to an ioservice.
 *
 * @param oo object operation being processed.
 * @param i index of the cob.
 * @remark This function gets called from an AST. Do not call it from a RPC
 * callback.
 * @remark This function might be used to re-send fop to an ioservice.
 */
static int clovis_cob_ios_send(struct clovis_cob_req *cr, uint32_t idx)
{
	int                     rc;
	uint32_t                cob_idx;
	uint32_t                cob_type;
	struct m0_fid           cob_fid;
	const struct m0_fid    *gob_fid;
	struct m0_clovis       *cinst;
	struct m0_rpc_session  *session;
	struct m0_fop          *fop;
	struct m0_pool_version *pv;

	M0_ENTRY();

	/*
	 * Sanity checks. Note: ios_send may be called via ios_md_send
	 * or ios_io_send. In the case of ios_md_send, the instance
	 * lock has been held.
	 */
	M0_PRE(M0_IN(cr->cr_cob_type, (M0_COB_IO, M0_COB_MD)));
	cob_type = cr->cr_cob_type;

	cinst = cr->cr_cinst;
	M0_PRE(cinst != NULL);

	/* Determine cob fid and idx*/
	gob_fid = &cr->cr_fid;

	pv = m0_pool_version_find(&cinst->m0c_pools_common,
				  &cr->cr_pver);

	M0_ASSERT(pv != NULL);

	if (cob_type == M0_COB_IO) {
		m0_poolmach_gob2cob(&pv->pv_mach, gob_fid, idx, &cob_fid);
		cob_idx = m0_fid_cob_device_id(&cob_fid);
		M0_ASSERT(cob_idx != ~0);
		session = m0_clovis_obj_container_id_to_session(pv, cob_idx);
	} else { /* M0_COB_MD */
		session = m0_reqh_mdpool_service_index_to_session(
				&cinst->m0c_reqh, gob_fid, idx);
		m0_fid_convert_gob2cob(gob_fid, &cob_fid, 0);
		cob_idx = idx;
	}
	M0_ASSERT(cob_idx != ~0);
	M0_ASSERT(session != NULL);

	rc = m0_rpc_session_validate(session);
	if (rc != 0)
		return M0_ERR(rc);

	/* Allocate ios fop if necessary */
	fop = clovis_cob_ios_fop_get(cr, idx, cob_type, session);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* Fill the cob fop. */
	rc = clovis_cob_ios_fop_populate(cr, fop, &cob_fid, cob_idx);
	if (rc != 0)
		goto error;

	/*
	 * Set and send the rpc item. Note: ri_deadline is set to
	 * COB_REQ_DEADLINE from 'now' to allow RPC formation to pack the fops
	 * and to improve network utilization. (See commit 09c0a46368 for
	 * details).
	 */
	fop->f_item.ri_rmachine = m0_fop_session_machine(session);
	fop->f_item.ri_session  = session;
	fop->f_item.ri_prio     = M0_RPC_ITEM_PRIO_MID;
	fop->f_item.ri_deadline = 0;
				/*m0_time_from_now(0, IOS_COB_REQ_DEADLINE);*/
	fop->f_item.ri_nr_sent_max = CLOVIS_RPC_MAX_RETRIES;
	fop->f_item.ri_resend_interval = CLOVIS_RPC_RESEND_INTERVAL;

	if (cr->cr_flags & CLOVIS_COB_REQ_ASYNC) {
		fop->f_item.ri_ops = &clovis_cob_ios_ri_ops;
		rc = m0_rpc_post(&fop->f_item);
		if (rc != 0)
			goto error;
	} else {
		rc = m0_rpc_post_sync(fop, session, NULL, 0);
		if (rc != 0)
			goto error;

	}
	return M0_RC(0);
error:
	M0_ASSERT(rc != 0);
	if (fop != NULL) {
		m0_clovis_cob_ios_fop_fini(cr->cr_ios_fop[idx]);
		cr->cr_ios_fop[idx] = NULL;
	}
	return M0_RC(rc);
}

/**
 * AST callback that contacts multiple ioservices to the cobs that form
 * an object.
 *
 * @param grp group the AST is executed in.
 * @param ast callback being executed.
 */
static void clovis_cob_ast_ios_io_send(struct m0_sm_group *grp,
					     struct m0_sm_ast *ast)
{
	int                      rc;
	uint32_t                 i;
	uint32_t                 icr_nr;
	uint32_t                 pool_width;
	struct m0_clovis_ast_rc *ar;
	struct m0_clovis        *cinst;
	struct m0_pool_version  *pv;
	struct clovis_cob_req   *cr;

	M0_ENTRY();

	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	M0_PRE(ast != NULL);

	ar = bob_of(ast,  struct m0_clovis_ast_rc, ar_ast, &ar_bobtype);
	cr = bob_of(ar, struct clovis_cob_req, cr_ar, &cr_bobtype);

	cinst = cr->cr_cinst;
	M0_ASSERT(cinst != NULL);

	pv = m0_pool_version_find(&cinst->m0c_pools_common, &cr->cr_pver);
	if (pv == NULL) {
		M0_LOG(M0_ERROR, "Failed to get pool version "FID_F,
		       FID_P(&cr->cr_pver));
		rc = M0_ERR(-ENOENT);
		goto error;
	}
	pool_width = pv->pv_attr.pa_P;
	M0_ASSERT(pool_width >= 1);

	/* Send a fop to each ioservice. */
	cr->cr_cob_type = M0_COB_IO;
	cr->cr_icr_nr = pool_width;

	for (i = 0; i < pool_width; i++) {
		rc = clovis_cob_ios_send(cr, i);
		if (rc != 0)
			goto error;
	}

	M0_LEAVE();
	return;
error:
	/* Cancel any pending fop for this op. */
	icr_nr = cr->cr_icr_nr;
	for (i = 0; i < icr_nr; ++i)
		if (!cr->cr_ios_completed[i] && cr->cr_ios_fop[i] != NULL)
			m0_rpc_item_cancel(&cr->cr_ios_fop[i]->f_item);

	clovis_cob_fail_oo(cr->cr_oo, rc);
	/* FREE CR */
	clovis_cob_req_free(cr);
	M0_LEAVE();
}

/**
 * Sends entity namespace fops to a io services(oostore mode only), as part of
 * the processing of a create/delete object operation.
 *
 * @param oo object operation being processed.
 * @return 0 if success or an error code otherwise.
 */
static int clovis_cob_ios_md_send(struct clovis_cob_req *cr)
{
	int               i;
	int               rc = 0;
	int               icr_nr;
	struct m0_clovis *cinst;

	M0_ENTRY();
	M0_PRE(cr != NULL);

	cinst = cr->cr_cinst;
	M0_PRE(cinst != NULL);
	M0_ASSERT(cinst->m0c_config->cc_is_oostore == true);
	M0_ASSERT(cinst->m0c_pools_common.pc_md_redundancy >= 1);

	icr_nr = cinst->m0c_pools_common.pc_md_redundancy;

	/*
	 * Send to each redundant ioservice.
	 * It is possible that ios_io_send is called right before ios_md_send
	 * completes the for loop below (but all M0_COB_MD requests are sent and
	 * replied). So the content of 'oo' may be changed. Be aware of this
	 * race condition!
	 */
	cr->cr_cob_type = M0_COB_MD;
	cr->cr_icr_nr   = icr_nr;

	for (i = 0; i < icr_nr; ++i) {
		rc = clovis_cob_ios_send(cr, i);
		if (rc != 0)
			break;
	}

	return M0_RC(rc);
}

/**----------------------------------------------------------------------------*
 *                           COB FOP's for mdservice                           *
 *-----------------------------------------------------------------------------*/

/**
 * Returns the object operation associated to a given RPC item.Items are sent
 * to services when processing an operation. This function provides a pointer
 * to the original object operation that triggered the communication.
 *
 * @param item RPC item.
 * @return a pointer to the object operation that triggered the creation of the
 * item.
 */
static struct clovis_cob_req* rpc_item_to_cr(struct m0_rpc_item *item)
{
	struct m0_fop         *fop;
	struct clovis_cob_req *cr;

	M0_ENTRY();
	M0_PRE(item != NULL);

	fop = m0_rpc_item_to_fop(item);
	cr = (struct clovis_cob_req *)fop->f_opaque;

	M0_LEAVE();
	return cr;
}

/**
 * AST callback to fail a whole object operation.
 *
 * @param grp group the AST is executed in.
 * @param ast callback being executed.
 */
static void clovis_cob_ast_fail_cr(struct m0_sm_group *grp,
				   struct m0_sm_ast *ast)
{
	uint32_t                 i;
	uint32_t                 icr_nr;
	struct clovis_cob_req   *cr;
	struct m0_clovis_ast_rc *ar;

	M0_ENTRY();

	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	M0_PRE(ast != NULL);

	ar = bob_of(ast,  struct m0_clovis_ast_rc, ar_ast, &ar_bobtype);
	cr = bob_of(ar, struct clovis_cob_req, cr_ar, &cr_bobtype);
	M0_ASSERT(ar->ar_rc != 0);

	/* Cancel any pending fop for this op. */
	icr_nr = cr->cr_icr_nr;
	for (i = 0; i < icr_nr; ++i) {
		if (!cr->cr_ios_completed[i] && cr->cr_ios_fop[i] != NULL) {
			m0_rpc_item_cancel(&cr->cr_ios_fop[i]->f_item);
		}
	}

	clovis_cob_fail_oo(cr->cr_oo, ar->ar_rc);
	/* Free CR */
	clovis_cob_req_free(cr);
	M0_LEAVE();
}

/**
 * AST callback to complete a whole object operation.
 *
 * @param grp group the AST is executed in.
 * @param ast callback being executed.
 */
static void clovis_cob_ast_complete_cr(struct m0_sm_group *grp,
				       struct m0_sm_ast *ast)
{
	struct m0_clovis_ast_rc *ar;
	struct clovis_cob_req   *cr;

	M0_ENTRY();

	M0_PRE(grp != NULL);
	M0_PRE(m0_sm_group_is_locked(grp));
	M0_PRE(ast != NULL);

	ar = bob_of(ast,  struct m0_clovis_ast_rc, ar_ast, &ar_bobtype);
	cr = bob_of(ar, struct clovis_cob_req, cr_ar, &cr_bobtype);
	M0_ASSERT(ar->ar_rc == 0);

	clovis_cob_complete_oo(cr->cr_oo);

	/* Free CR */
	clovis_cob_req_free(cr);
	M0_LEAVE();
}

/**
 * rio_replied RPC callback to be executed whenever a reply to an object
 * namespace request is received. The mdservice gets contacted as a first
 * step when creating/deleting an object. This callback gets executed when
 * a reply arrives from the mdservice. It also gets executed if the RPC
 * component has detected any error.
 *
 * @param item RPC item used to communicate to the mdservice.
 */
static void clovis_cob_mds_rio_replied(struct m0_rpc_item *item)
{
	int                       rc = 0; /* Required. */
	uint32_t                  rep_opcode;
	uint32_t                  req_opcode;
	struct m0_fop            *rep_fop;
	struct m0_fop            *req_fop;
	struct m0_fop_create_rep *create_rep;
	struct m0_fop_unlink_rep *unlink_rep;
	struct m0_clovis         *cinst;
	struct clovis_cob_req    *cr;
#ifdef CLOVIS_OSYNC
	struct m0_reqh_service_ctx  *ctx;
	struct m0_be_tx_remid       *remid = NULL;
#endif

	M0_ENTRY();

	M0_PRE(item != NULL);

	cr = rpc_item_to_cr(item);
	M0_ASSERT(cr != NULL);

	cinst = cr->cr_cinst;
	M0_ASSERT(cinst != NULL);

	req_fop = cr->cr_mds_fop;
	M0_ASSERT(req_fop != NULL);

	M0_ASSERT(cr->cr_oo != NULL);

	/* Failure in rpc? */
	rc = m0_rpc_item_error(item);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "rpc item error = %d", rc);
		goto error;
	}

	M0_ASSERT(item->ri_reply != NULL);
	rep_fop = m0_rpc_item_to_fop(item->ri_reply);
	req_opcode = m0_fop_opcode(req_fop);
	rep_opcode = m0_fop_opcode(rep_fop);

	/* Failure in operation specific phase? */
	switch (rep_opcode) {
	case M0_MDSERVICE_CREATE_REP_OPCODE:
		M0_ASSERT(req_opcode == M0_MDSERVICE_CREATE_OPCODE);

		create_rep = m0_fop_data(rep_fop);
		rc = create_rep->c_body.b_rc;
		remid = &create_rep->c_mod_rep.fmr_remid;
		break;

	case M0_MDSERVICE_UNLINK_REP_OPCODE:
		M0_ASSERT(req_opcode == M0_MDSERVICE_UNLINK_OPCODE);

		unlink_rep = m0_fop_data(rep_fop);
		rc = unlink_rep->u_body.b_rc;
		remid = &unlink_rep->u_mod_rep.fmr_remid;
		break;

	default:
		M0_IMPOSSIBLE("Unsupported opcode:%d.", rep_opcode);
		break;
	}

	if (rc != 0)
		goto error;

	/* Update pending transaction number */
	if (remid != NULL) {
		ctx = m0_reqh_service_ctx_from_session(item->ri_session);
		clovis_sync_record_update(ctx, NULL, cr->cr_oo != NULL ?
					  &cr->cr_oo->oo_oc.oc_op : NULL, remid);
	}

	/*
	 * Commit 2332f298 introduced a optimisation for CREATE
	 * which delays creating COBs to the first time they are
	 * written to.
	 */
	if (cr->cr_opcode == M0_CLOVIS_EO_CREATE) {
		cr->cr_ar.ar_ast.sa_cb = &clovis_cob_ast_complete_cr;
		m0_sm_ast_post(cr->cr_oo->oo_sm_grp, &cr->cr_ar.ar_ast);
	} else {
		cr->cr_ar.ar_ast.sa_cb = &clovis_cob_ast_ios_io_send;
		m0_sm_ast_post(cr->cr_oo->oo_sm_grp, &cr->cr_ar.ar_ast);
	}

	M0_LEAVE();
	return;

error:
	M0_ASSERT(rc != 0);
	cr->cr_ar.ar_ast.sa_cb = &clovis_cob_ast_fail_cr;
	cr->cr_ar.ar_rc = rc;
	m0_sm_ast_post(cr->cr_oo->oo_sm_grp, &cr->cr_ar.ar_ast);
	M0_LEAVE();
}

/**
 * RPC callbacks for the posting of COB fops to mdservices.
 */
static const struct m0_rpc_item_ops clovis_cob_mds_ri_ops = {
	.rio_replied = clovis_cob_mds_rio_replied,
};

/**
 * Populates a COB fop for the namespace operation.
 * This type of fop is sent to the mdservice to create/delete new objects.
 *
 * @param oo object operation whose processing triggers this call. Contains the
 * info to populate the fop with.
 * @param fop fop being stuffed.
 */
static int clovis_cob_mds_fop_populate(struct clovis_cob_req *cr,
				       struct m0_fop         *fop)
{
	int                      rc = 0;
	int                      valid = 0;
	struct m0_cob_attr       attr;
	struct m0_fop_create    *create;
	struct m0_fop_unlink    *unlink;
	struct m0_fop_getattr   *getattr;
	struct m0_fop_cob       *req;

	M0_ENTRY();

	M0_PRE(fop != NULL);

	clovis_cob_attr_init(cr->cr_opcode, &attr, &valid);

	switch (m0_fop_opcode(fop)) {
	case M0_MDSERVICE_CREATE_OPCODE:
		create = m0_fop_data(fop);
		req = &create->c_body;
		clovis_cob_body_mem2wire(req, &attr, valid, cr);
#ifdef CLOVIS_FOR_M0T1FS
		rc = clovis_cob_name_mem2wire(&create->c_name, &cr->cr_name);
#endif
		break;
	case M0_MDSERVICE_UNLINK_OPCODE:
		unlink = m0_fop_data(fop);
		req = &unlink->u_body;
		clovis_cob_body_mem2wire(req, &attr, valid, cr);
#ifdef CLOVIS_FOR_M0T1FS
		rc = clovis_cob_name_mem2wire(&unlink->u_name, &cr->cr_name);
#endif
		break;
	case M0_MDSERVICE_GETATTR_OPCODE:
		getattr      = m0_fop_data(fop);
		req          = &getattr->g_body;
		req->b_tfid  = cr->cr_fid;
		req->b_valid = 0;
		break;
	default:
		rc = -ENOSYS;
		M0_IMPOSSIBLE("Can't send message of <unimplemented> type.");
	}

	return M0_RC(rc);
}

#ifdef CLOVIS_FOR_M0T1FS /* if we want m0t1fs to reflect the changes */
/**
 * Retrieves the rpc session for the mdservice to contact when creating/deleting
 * an object. To maintain backwards compability with m0t1fs the session is
 * calculated using the file's name.
 *
 * @param cinst clovis instance.
 * @param filename name of the file being created/deleted.
 * @param len length of the filename.
 * @return rpc session established with the mdservice.
 */
static struct m0_rpc_session *
filename_to_mds_session(struct m0_clovis *cinst,
			const unsigned char *filename,
			m0_bcount_t len)
{
	unsigned int                  hash;
	struct m0_reqh_service_ctx   *mds_ctx;
	const struct m0_pools_common *pc;

	M0_ENTRY();

	M0_PRE(cinst != NULL);
	M0_PRE(filename != NULL);
	M0_PRE(len > 0);

	/* XXX implement use_cache_hint */

	/* XXX: possible uint overflow */
	/* XXX: no guarantee unsigned int == UINT32, we need a better way to
	 * check this */
	M0_ASSERT(len < (m0_bcount_t)UINT32_MAX);
	hash = m0_full_name_hash(filename, (unsigned int)len);

	pc = &cinst->m0c_pools_common;
	mds_ctx = pc->pc_mds_map[hash % pc->pc_nr_svcs[M0_CST_MDS]];
	M0_ASSERT(mds_ctx != NULL);

	M0_LEAVE();
	return &mds_ctx->sc_rlink.rlk_sess;
}

#else
/**
 * Retrieves the rpc session for the mdservice to contact when creating/deleting
 * an object. Clovis selects the mdservice to contact using the object's fid.
 *
 * @param cinst clovis instance.
 * @param fid fid of the object.
 * @return rpc session established with the mdservice.
 */
static struct m0_rpc_session *
fid_to_mds_session(struct m0_clovis *cinst, const struct m0_fid *fid)
{
	const struct m0_pools_common 		*pc;
	struct m0_clovis_service_context        *mds_ctx;

	M0_ENTRY();

	M0_PRE(cinst != NULL);

	/* XXX implement use_cache_hint */

	pc = &cinst->m0c_pools_common;
	mds_ctx = pc->pc_mds_map[fid->f_key % pc->pc_nr_svcs[M0_CST_MDS]];
	M0_ASSERT(mds_ctx != NULL);

	M0_LEAVE();
	return &mds_ctx->sc_rlink.rlk_sess;
}
#endif

/**
 * Sends COB fops to a mdservice, as part of the processing of a object
 * operation.
 *
 * @param oo object operation being processed.
 * @return 0 if success or an error code otherwise.
 */
static int clovis_cob_mds_send(struct clovis_cob_req *cr)
{
	int                    rc;
	struct m0_fop         *fop;
	struct m0_clovis      *cinst;
	struct m0_rpc_session *session;
	struct m0_fop_type    *ftype = NULL; /* Required */

	M0_ENTRY();

	M0_PRE(cr != NULL);

	cinst = cr->cr_cinst;
	M0_PRE(cinst != NULL);

	/* Get the mdservice's session. */
#ifdef CLOVIS_FOR_M0T1FS
	session = filename_to_mds_session(cinst,
		(unsigned char *)cr->cr_name.b_addr, cr->cr_name.b_nob);
#else
	session = fid_to_mds_session(cinst, &cr->cr_fid);
#endif

	M0_ASSERT(session != NULL);

	rc = m0_rpc_session_validate(session);
	if (rc != 0)
		return M0_ERR(rc);

	/* Select the fop type to be sent to the mdservice. */
	switch (cr->cr_opcode) {
		case M0_CLOVIS_EO_CREATE:
			ftype = &m0_fop_create_fopt;
			break;
		case M0_CLOVIS_EO_DELETE:
			ftype = &m0_fop_unlink_fopt;
			break;
		case M0_CLOVIS_EO_GETATTR:
			ftype = &m0_fop_getattr_fopt;
			break;
		default:
			M0_IMPOSSIBLE("Operation not supported:%d", cr->cr_opcode);
	}

	fop = m0_fop_alloc_at(session, ftype);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	rc = clovis_cob_mds_fop_populate(cr, fop);
	if (rc != 0)
		goto error;

	fop->f_opaque                  = cr;
	fop->f_item.ri_session         = session;
	fop->f_item.ri_prio            = M0_RPC_ITEM_PRIO_MID;
	fop->f_item.ri_deadline        = 0;
	fop->f_item.ri_nr_sent_max     = CLOVIS_RPC_MAX_RETRIES;
	fop->f_item.ri_resend_interval = CLOVIS_RPC_RESEND_INTERVAL;
	cr->cr_mds_fop = fop;

	if (cr->cr_flags & CLOVIS_COB_REQ_ASYNC) {
		fop->f_item.ri_ops = &clovis_cob_mds_ri_ops;
		rc = m0_rpc_post(&fop->f_item);
		if (rc != 0)
			goto error;
	} else {
		rc = m0_rpc_post_sync(fop, session, NULL, 0);
		if (rc != 0)
			goto error;
		cr->cr_rep_fop = m0_rpc_item_to_fop(fop->f_item.ri_reply);
	}
	return M0_RC(0);
error:
	if (fop != NULL) {
		m0_fop_put_lock(cr->cr_mds_fop);
		cr->cr_mds_fop = NULL;
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0_clovis_cob_send(struct m0_clovis_op_obj *oo)
{
	int                     rc;
	struct m0_clovis       *cinst;
	struct clovis_cob_req  *cr;
	struct m0_pool_version *pv;

	M0_ENTRY();
	M0_PRE(oo != NULL);

	cinst = m0_clovis__oo_instance(oo);
	M0_ASSERT(cinst != NULL);

	pv = m0_pool_version_find(&cinst->m0c_pools_common,
				  &oo->oo_pver);

	cr = clovis_cob_req_alloc(pv);
	if(cr == NULL) {
		return M0_ERR(-ENOMEM);
	}

	cr->cr_cinst   = cinst;
	cr->cr_opcode  = oo->oo_oc.oc_op.op_code;
	cr->cr_fid     = oo->oo_fid;
#ifdef CLOVIS_FOR_M0T1FS
	cr->cr_name = oo->oo_name;
#endif
	cr->cr_oo  = oo;
	cr->cr_flags |= CLOVIS_COB_REQ_ASYNC;

	if (!cinst->m0c_config->cc_is_oostore)
		/* Initiate the op by sending a fop to the mdservice. */
		rc = clovis_cob_mds_send(cr);
	else
		/* Send fops to redundant IOS's */
		rc = clovis_cob_ios_md_send(cr);

	if (rc != 0) {
		cr->cr_ar.ar_ast.sa_cb = &clovis_cob_ast_fail_cr;
		cr->cr_ar.ar_rc = rc;
		m0_sm_ast_post(oo->oo_sm_grp, &cr->cr_ar.ar_ast);
	}

	return M0_RC(rc);
}

M0_INTERNAL int clovis_cob_rep_fop_attr(struct clovis_cob_req *cr,
					struct m0_fop_cob     *cob_attr,
					int                    idx)
{
	int                              rc;
	struct m0_fop_getattr_rep       *getattr_rep;
	struct m0_fop_cob_getattr_reply *getattr_ios_rep;
	struct m0_clovis                *cinst;

	M0_PRE(cr != NULL);
	cinst = cr->cr_cinst;

	if (!cinst->m0c_config->cc_is_oostore) {
		getattr_rep = m0_fop_data(cr->cr_rep_fop);
		rc = getattr_rep->g_rc;
		if (rc == 0)
			memcpy((void *)cob_attr, (void *)&getattr_rep->g_body,
				sizeof *cob_attr);
	} else {
		getattr_ios_rep = m0_fop_data(m0_rpc_item_to_fop(
					      cr->cr_ios_fop[idx]->f_item.ri_reply));

		rc = getattr_ios_rep->cgr_rc;
		if(rc == 0)
			memcpy((void *)cob_attr, (void *)&getattr_ios_rep->cgr_body,
			       sizeof *cob_attr);
	}

	return M0_RC(rc);
}

M0_INTERNAL int clovis_cob_getattr(struct clovis_cob_req *cr,
				   struct m0_fop_cob     *cob_attr)
{
	int                              rc = 0; /* required */
	int                              i;
	struct m0_clovis                *cinst;
	struct m0_pool_version          *pv;

	M0_PRE(cr != NULL);
	cinst = cr->cr_cinst;

	cr->cr_opcode   = M0_CLOVIS_EO_GETATTR;
	cr->cr_cob_type = M0_COB_MD;

	rc = m0_pool_version_get(&cinst->m0c_pools_common, &pv);
	M0_ASSERT(rc == 0);

	if (!cinst->m0c_config->cc_is_oostore) {
		/* Initiate the op by sending a fop to the mdservice. */
		rc = clovis_cob_mds_send(cr);
		if (rc == 0 && (cr->cr_flags & CLOVIS_COB_REQ_SYNC))
			rc = clovis_cob_rep_fop_attr(cr, cob_attr, 0);
	} else {
		for(i = 0; i < cinst->m0c_pools_common.pc_md_redundancy; i++) {
			/* Send fops to redundant IOS' */
			rc = clovis_cob_ios_send(cr, i);
			if (rc != 0)
				continue;

			if (cr->cr_flags & CLOVIS_COB_REQ_SYNC) {
				rc = clovis_cob_rep_fop_attr(cr, cob_attr, i);
				if (rc == -ENOENT)
					continue;
				else
					break;
			}

		}
	}

	return M0_RC(rc);
}

M0_INTERNAL int m0_clovis__cob_poolversion_get(struct m0_clovis_obj *obj)
{
	int                     rc;
	struct m0_clovis       *cinst;
	struct clovis_cob_req  *cr;
	struct m0_fop_cob      *cob_attr;
	struct m0_pool_version *pv;
#ifdef CLOVIS_FOR_M0T1FS
	char                   *object_fid_s;
	struct m0_fid           object_fid;
#endif

	M0_ENTRY();

	M0_PRE(obj != NULL);
	cinst = m0_clovis__obj_instance(obj);

	rc = m0_pool_version_get(&cinst->m0c_pools_common, &pv);
	M0_ASSERT(rc == 0);

	cr = clovis_cob_req_alloc(pv);
	if (cr == NULL)
		return -ENOMEM;

	m0_fid_gob_make(&cr->cr_fid,
			obj->ob_entity.en_id.u_hi, obj->ob_entity.en_id.u_lo);

	cr->cr_flags |= CLOVIS_COB_REQ_SYNC;
	cr->cr_cinst  = cinst;
#ifdef CLOVIS_FOR_M0T1FS
	/* Set the object's parent's fid. */
	if (!m0_fid_is_set(&cinst->m0c_root_fid) ||
	    !m0_fid_is_valid(&cinst->m0c_root_fid)) {
		clovis_cob_req_free(cr);
		rc = -EINVAL;
		return M0_ERR(rc);
	}

	m0_fid_gob_make(&object_fid, obj->ob_entity.en_id.u_hi,
			obj->ob_entity.en_id.u_lo);

	/* Generate a valid oo_name. */
	object_fid_s = m0_alloc(M0_FID_STR_LEN);
	if (object_fid_s == NULL) {
		clovis_cob_req_free(cr);
		return M0_ERR(-ENOMEM);
	}

	rc = m0_fid_print(object_fid_s, M0_FID_STR_LEN, &object_fid);
	if (rc <= 0) {
		clovis_cob_req_free(cr);
		return M0_ERR(rc);
	}

	cr->cr_name.b_addr = object_fid_s;
	cr->cr_name.b_nob  = strlen(object_fid_s);
#endif
	cob_attr = m0_alloc(sizeof *cob_attr);
	if (cob_attr == NULL) {
		clovis_cob_req_free(cr);
		return M0_ERR(-ENOMEM);
	}

	memset(cob_attr, 0, sizeof *cob_attr);
	rc = clovis_cob_getattr(cr, cob_attr);
	if (rc != 0) {
		m0_free(cob_attr);
		clovis_cob_req_free(cr);
		return rc == -ENOENT ? M0_RC(rc) : M0_ERR(rc);
	}

	if (m0_pool_version_find(&cinst->m0c_pools_common,
					 &cob_attr->b_pver) == NULL) {
		clovis_cob_req_free(cr);
		m0_free(cob_attr);
		M0_LOG(M0_ERROR, "Unable to find a suitable pool version");
		return M0_ERR(-EINVAL);
	}

	obj->ob_attr.oa_pver = cob_attr->b_pver;

#ifdef CLOVIS_FOR_M0T1FS
	m0_free(object_fid_s);
#endif
	m0_free(cob_attr);
	clovis_cob_req_free(cr);
	return M0_RC(0);
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
