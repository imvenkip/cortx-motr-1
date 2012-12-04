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
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 07/23/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/ut.h"
#include "rm/rm.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"
#include "rm/rm_fops.c"          /* To access static APIs. */

static struct m0_rm_loan *test_loan;
static struct m0_rm_remote remote;

static void post_borrow_validate(int err);
static void borrow_reply_populate(struct m0_fop_rm_borrow_rep *breply,
				  int err);
static void post_borrow_cleanup(struct m0_rpc_item *item, int err);
static void borrow_fop_validate(struct m0_fop_rm_borrow *bfop);
static void post_revoke_validate(int err);
static void revoke_fop_validate(struct m0_fop_rm_revoke *rfop);
static void post_revoke_cleanup(struct m0_rpc_item *item, int err);
static void revoke_reply_populate(struct m0_fom_error_rep *rreply,
				  int err);

/*
 *****************
 * Common test functions for test cases in this file.
 ******************
 */
static void rmfops_utinit(void)
{
	m0_rm_fop_init();
}

static void rmfops_utfini(void)
{
	m0_rm_fop_fini();
}

/*
 * Prepare parameters (request) for testing RM-FOP-send functions.
 */
static void request_param_init(enum m0_rm_incoming_type reqtype)
{
	M0_SET0(&test_data.rd_in);
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner, reqtype,
			    RIP_NONE, RIF_LOCAL_WAIT);

	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.cr_datum = NENYA;
	test_data.rd_in.rin_ops = &rings_incoming_ops;

	M0_SET0(&test_data.rd_credit);
	m0_rm_credit_init(&test_data.rd_credit, &test_data.rd_owner);
	test_data.rd_credit.cr_datum = NENYA;
	test_data.rd_credit.cr_ops = &rings_credit_ops;

	M0_ALLOC_PTR(test_data.rd_owner.ro_creditor);
	M0_UT_ASSERT(test_data.rd_owner.ro_creditor != NULL);
	m0_rm_remote_init(test_data.rd_owner.ro_creditor,
			  test_data.rd_owner.ro_resource);
	m0_cookie_init(&test_data.rd_owner.ro_creditor->rem_cookie,
		       &test_data.rd_owner.ro_id);
	M0_ALLOC_PTR(test_loan);
	M0_UT_ASSERT(test_loan != NULL);
	m0_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);
	remote.rem_state = REM_FREED;
	m0_rm_remote_init(&remote, &test_data.rd_res.rs_resource);
	m0_cookie_init(&remote.rem_cookie, &test_data.rd_owner.ro_id);
	test_loan->rl_other = &remote;
	m0_rm_loan_init(test_loan, &test_data.rd_credit, &remote);
	m0_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);
}

/*
 * Finalise parameters (request) parameters for testing RM-FOP-send functions.
 */
static void request_param_fini()
{
	m0_free(test_loan);
	m0_free(test_data.rd_owner.ro_creditor);
	test_data.rd_owner.ro_creditor = NULL;
}

/*
 * Validate FOP and other data structures after RM-FOP-send function is called.
 */
static void rm_req_fop_validate(enum m0_rm_incoming_type reqtype)
{
	struct m0_fop_rm_borrow *bfop;
	struct m0_fop_rm_revoke *rfop;
	struct m0_rm_pin	*pin;
	struct m0_rm_loan	*loan;
	struct m0_rm_outgoing	*og;
	struct rm_out		*oreq;
	uint32_t		 pins_nr = 0;

	m0_tl_for(pi, &test_data.rd_in.rin_pins, pin) {

		M0_UT_ASSERT(pin->rp_flags == M0_RPF_TRACK);
		/* It's time to introduce ladder_of() */
		loan = bob_of(pin->rp_credit, struct m0_rm_loan,
			      rl_credit, &loan_bob);
		og = container_of(loan, struct m0_rm_outgoing, rog_want);
		oreq = container_of(og, struct rm_out, ou_req);

		switch (reqtype) {
		case M0_RIT_BORROW:
			bfop = m0_fop_data(&oreq->ou_fop);
			borrow_fop_validate(bfop);
			break;
		case M0_RIT_REVOKE:
			rfop = m0_fop_data(&oreq->ou_fop);
			revoke_fop_validate(rfop);
			break;
		default:
			break;
		}

		m0_rm_ur_tlist_del(pin->rp_credit);
		m0_fop_fini(&oreq->ou_fop);
		rm_out_fini(oreq);

		pi_tlink_del_fini(pin);
		pr_tlink_del_fini(pin);
		m0_free(pin);

		++pins_nr;
	} m0_tl_endfor;
	M0_UT_ASSERT(pins_nr == 1);
}

/*
 * Create reply for each FOP.
 */
static struct m0_rpc_item *rm_reply_create(enum m0_rm_incoming_type reqtype,
					   int err)
{
	struct m0_fop_rm_borrow_rep *breply;
	struct m0_fom_error_rep     *rreply;

	struct m0_fop		    *fop;
	struct m0_rm_pin	    *pin;
	struct m0_rm_loan	    *loan;
	struct m0_rm_outgoing	    *og;
	struct m0_rpc_item	    *item = NULL;
	struct m0_rm_owner	    *owner;
	struct rm_out		    *oreq;
	uint32_t		     pins_nr = 0;

	m0_tl_for(pi, &test_data.rd_in.rin_pins, pin) {

		M0_UT_ASSERT(pin->rp_flags == M0_RPF_TRACK);
		/* It's time to introduce ladder_of() */
		loan = bob_of(pin->rp_credit, struct m0_rm_loan,
			      rl_credit, &loan_bob);
		og = container_of(loan, struct m0_rm_outgoing, rog_want);
		oreq = container_of(og, struct rm_out, ou_req);

		M0_ALLOC_PTR(fop);
		M0_UT_ASSERT(fop != NULL);
		switch (reqtype) {
		case M0_RIT_BORROW:
			item = &oreq->ou_fop.f_item;
			/* Initialise Reply FOP */
			m0_fop_init(fop, &m0_fop_rm_borrow_rep_fopt, NULL);
			M0_UT_ASSERT(m0_fop_data_alloc(fop) == 0);
			breply = m0_fop_data(fop);
			borrow_reply_populate(breply, err);
			item->ri_reply = &fop->f_item;
			break;
		case M0_RIT_REVOKE:
			item = &oreq->ou_fop.f_item;
			/* Initialise Reply FOP */
			m0_fop_init(fop, &m0_fom_error_rep_fopt, NULL);
			M0_UT_ASSERT(m0_fop_data_alloc(fop) == 0);
			rreply = m0_fop_data(fop);
			revoke_reply_populate(rreply, err);
			item->ri_reply = &fop->f_item;
			break;
		default:
			break;
		}
		owner = og->rog_want.rl_credit.cr_owner;
		/* Delete the pin so that owner_balance() does not process it */
		m0_rm_owner_lock(owner);
		pi_tlink_del_fini(pin);
		pr_tlink_del_fini(pin);
		m0_rm_owner_unlock(owner);
		m0_free(pin);
		++pins_nr;
	} m0_tl_endfor;
	M0_UT_ASSERT(pins_nr == 1);

	return item;
}

/*
 * Test a reply FOP.
 */
static void reply_test(enum m0_rm_incoming_type reqtype, int err)
{
	int		    rc;
	struct m0_rpc_item *item;

	request_param_init(reqtype);

	m0_fi_enable_once("m0_rm_request_out", "no-rpc");
	switch (reqtype) {
	case M0_RIT_BORROW:
		test_data.rd_in.rin_flags |= RIF_MAY_BORROW;
		rc = m0_rm_request_out(M0_ROT_BORROW, &test_data.rd_in, NULL,
				       &test_data.rd_credit);
		M0_UT_ASSERT(rc == 0);
		item = rm_reply_create(M0_RIT_BORROW, err);
		borrow_reply(item);
		post_borrow_validate(err);
		post_borrow_cleanup(item, err);
		outreq_free(item);
		break;
	case M0_RIT_REVOKE:
		test_data.rd_in.rin_flags |= RIF_MAY_REVOKE;
		rc = m0_rm_request_out(M0_ROT_REVOKE, &test_data.rd_in,
				       test_loan, &test_loan->rl_credit);
		item = rm_reply_create(M0_RIT_REVOKE, err);
		m0_rm_owner_lock(&test_data.rd_owner);
		m0_rm_ur_tlist_add(&test_data.rd_owner.ro_sublet, &test_loan->rl_credit);
		m0_rm_owner_unlock(&test_data.rd_owner);
		revoke_reply(item);
		post_revoke_validate(err);
		post_revoke_cleanup(item, err);
		outreq_free(item);
		/*
		 * When revoke succeeds, loan is de-allocted through
		 * revoke_reply(). If revoke fails, post_revoke_cleanup(),
		 * de-allocates the loan. Set test_loan to NULL. Otherwise,
		 * it will result in double free().
		 */
		test_loan = NULL;
		break;
	default:
		M0_IMPOSSIBLE("Invalid RM-FOM type");
	}

	request_param_fini();
}

/*
 * Test a request FOP.
 */
static void request_test(enum m0_rm_incoming_type reqtype)
{
	int rc;

	request_param_init(reqtype);

	m0_fi_enable_once("m0_rm_request_out", "no-rpc");
	switch (reqtype) {
	case M0_RIT_BORROW:
		test_data.rd_in.rin_flags |= RIF_MAY_BORROW;
		rc = m0_rm_request_out(M0_ROT_BORROW, &test_data.rd_in, NULL,
				       &test_data.rd_credit);
		break;
	case M0_RIT_REVOKE:
		test_data.rd_in.rin_flags |= RIF_MAY_REVOKE;
		rc = m0_rm_request_out(M0_ROT_REVOKE, &test_data.rd_in,
				       test_loan, &test_loan->rl_credit);
		break;
	default:
		M0_IMPOSSIBLE("Invalid RM-FOM type");
	}
	M0_UT_ASSERT(rc == 0);

	rm_req_fop_validate(reqtype);
	request_param_fini();
}

/*
 *****************
 * RM Borrow-FOP test functions
 ******************
 */
/*
 * Validates the owner lists after receiving BORROW-reply.
 */
static void post_borrow_validate(int err)
{
	bool got_credit;

	m0_rm_owner_lock(&test_data.rd_owner);
	got_credit = !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed) &&
		    !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]);
	m0_rm_owner_unlock(&test_data.rd_owner);
	M0_UT_ASSERT(ergo(err, !got_credit));
	M0_UT_ASSERT(ergo(err == 0, got_credit));
}

static void borrow_reply_populate(struct m0_fop_rm_borrow_rep *breply,
				  int err)
{
	int rc;

	breply->br_rc.rerr_rc = err;

	if (err == 0) {
		rc = m0_rm_credit_encode(&test_data.rd_credit,
					&breply->br_credit.cr_opaque);
		M0_UT_ASSERT(rc == 0);
	}
}

static void post_borrow_cleanup(struct m0_rpc_item *item, int err)
{
	struct m0_rm_credit	    *credit;
	struct m0_rm_loan	    *loan;
	struct m0_fop		    *fop;
	struct m0_fop_rm_borrow_rep *breply;
	struct m0_fop_rm_borrow	    *bfop;

	/*
	 * Clean-up borrow request and reply FOPs first.
	 */
	fop = m0_rpc_item_to_fop(item->ri_reply);
	breply = m0_fop_data(fop);

	m0_buf_free(&breply->br_credit.cr_opaque);
	m0_fop_fini(fop);
	m0_free(fop);

	fop = m0_rpc_item_to_fop(item);
	bfop = m0_fop_data(fop);
	m0_buf_free(&bfop->bo_base.rrq_credit.cr_opaque);

	/*
	 * A borrow error leaves owner lists unaffected.
	 * If borrow succeeds, the owner lists are updated. Hence they
	 * need to be cleaned-up.
	 */
	if (err)
		return;

	m0_rm_owner_lock(&test_data.rd_owner);
	m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED], credit) {
		m0_rm_ur_tlink_del_fini(credit);
		m0_free(credit);
	} m0_tl_endfor;

	m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_borrowed, credit) {
		m0_rm_ur_tlink_del_fini(credit);
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		m0_free(loan);
	} m0_tl_endfor;
	m0_rm_owner_unlock(&test_data.rd_owner);
}

/*
 * Check if m0_rm_request_out() has filled the FOP correctly
 */
static void borrow_fop_validate(struct m0_fop_rm_borrow *bfop)
{
	struct m0_rm_owner *owner;
	struct m0_rm_credit  credit;
	int		    rc;

	owner = m0_cookie_of(&bfop->bo_base.rrq_owner.ow_cookie,
			     struct m0_rm_owner, ro_id);

	M0_UT_ASSERT(owner != NULL);
	M0_UT_ASSERT(owner == &test_data.rd_owner);

	m0_rm_credit_init(&credit, &test_data.rd_owner);
	credit.cr_ops = &rings_credit_ops;
	rc = m0_rm_credit_decode(&credit, &bfop->bo_base.rrq_credit.cr_opaque);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(credit.cr_datum == test_data.rd_credit.cr_datum);
	m0_rm_credit_fini(&credit);

	M0_UT_ASSERT(bfop->bo_base.rrq_policy == RIP_NONE);
	M0_UT_ASSERT(bfop->bo_base.rrq_flags &
			(RIF_LOCAL_WAIT | RIF_MAY_BORROW));
	m0_buf_free(&bfop->bo_base.rrq_credit.cr_opaque);
}

/*
 * Test function for m0_rm_request_out() for BORROW FOP.
 */
static void borrow_request_test()
{
	request_test(M0_RIT_BORROW);
}

/*
 * Test function for borrow_reply().
 */
static void borrow_reply_test()
{
	/* 1. Test borrow-success */
	reply_test(M0_RIT_BORROW, 0);

	/* 2. Test borrow-failure */
	reply_test(M0_RIT_BORROW, -ETIMEDOUT);
}

/*
 *****************
 * RM Revoke-FOM test functions
 ******************
 */
/*
 * Validates the owner lists after receiving REVOKE-reply.
 */
static void post_revoke_validate(int err)
{
	bool sublet;
	bool owned;

	m0_rm_owner_lock(&test_data.rd_owner);
	sublet = !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet);
	owned = !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]);
	m0_rm_owner_unlock(&test_data.rd_owner);

	/* If revoke fails, credit remains in sublet list */
	M0_UT_ASSERT(ergo(err, sublet && !owned));
	/* If revoke succeds, credit will be part of cached list*/
	M0_UT_ASSERT(ergo(err == 0, owned && !sublet));
}

/*
 * Check if m0_rm_request_out() has filled the FOP correctly
 */
static void revoke_fop_validate(struct m0_fop_rm_revoke *rfop)
{
	struct m0_rm_owner *owner;
	struct m0_rm_credit  credit;
	struct m0_rm_loan  *loan;
	int		    rc;

	owner = m0_cookie_of(&rfop->rr_base.rrq_owner.ow_cookie,
			     struct m0_rm_owner, ro_id);

	M0_UT_ASSERT(owner != NULL);
	M0_UT_ASSERT(owner == &test_data.rd_owner);

	loan = m0_cookie_of(&rfop->rr_loan.lo_cookie, struct m0_rm_loan, rl_id);
	M0_UT_ASSERT(loan != NULL);
	M0_UT_ASSERT(loan == test_loan);

	m0_rm_credit_init(&credit, &test_data.rd_owner);
	credit.cr_ops = &rings_credit_ops;
	rc = m0_rm_credit_decode(&credit, &rfop->rr_base.rrq_credit.cr_opaque);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(credit.cr_datum == test_data.rd_credit.cr_datum);
	m0_rm_credit_fini(&credit);

	M0_UT_ASSERT(rfop->rr_base.rrq_policy == RIP_NONE);
	M0_UT_ASSERT(rfop->rr_base.rrq_flags &
		     (RIF_LOCAL_WAIT | RIF_MAY_REVOKE));
	m0_buf_free(&rfop->rr_base.rrq_credit.cr_opaque);
}

static void post_revoke_cleanup(struct m0_rpc_item *item, int err)
{
	struct m0_fop_rm_reovke_rep *rreply;
	struct m0_fop_rm_revoke	    *rfop;
	struct m0_rm_credit	    *credit;
	struct m0_rm_loan	    *loan;
	struct m0_fop		    *fop;

	/*
	 * Clean-up revoke request and reply FOPs first.
	 */
	fop = m0_rpc_item_to_fop(item->ri_reply);
	rreply = m0_fop_data(fop);

	m0_fop_fini(fop);
	m0_free(fop);

	fop = m0_rpc_item_to_fop(item);
	rfop = m0_fop_data(fop);
	m0_buf_free(&rfop->rr_base.rrq_credit.cr_opaque);

	/*
	 * After a successful revoke, sublet credit is transferred to
	 * OWOS_CACHED. Otherwise it remains in the sublet list.
	 * Clean up the lists.
	 */
	m0_rm_owner_lock(&test_data.rd_owner);
	if (err == 0) {
		m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED],
			  credit) {
			m0_rm_ur_tlink_del_fini(credit);
			m0_free(credit);
		} m0_tl_endfor;
	} else {
		m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_sublet, credit) {
			m0_rm_ur_tlink_del_fini(credit);
			loan = bob_of(credit, struct m0_rm_loan,
				      rl_credit, &loan_bob);
			m0_free(loan);
		} m0_tl_endfor;
	}
	m0_rm_owner_unlock(&test_data.rd_owner);
}

static void revoke_reply_populate(struct m0_fom_error_rep *rreply,
				  int err)
{
	rreply->rerr_rc = err;
}

/*
 * Test function for m0_rm_request_out() for REVOKE FOP.
 */
static void revoke_request_test()
{
	request_test(M0_RIT_REVOKE);
}

/*
 * Test function for revoke_reply().
 */
static void revoke_reply_test()
{
	/* 1. Test revoke-success */
	reply_test(M0_RIT_REVOKE, 0);

	/* 2. Test revoke-failure */
	reply_test(M0_RIT_REVOKE, -ETIMEDOUT);
}

/*
 *****************
 * Top level test functions
 ******************
 */
static void borrow_fop_funcs_test(void)
{
	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* 1. Test m0_rm_request_out() - sending BORROW FOP */
	borrow_request_test();

	/* 2. Test borrow_reply() - reply for BORROW FOP */
	borrow_reply_test();

	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void revoke_fop_funcs_test(void)
{
	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* 1. Test m0_rm_request_out() - sending REVOKE FOP */
	revoke_request_test();

	/* 2. Test revoke_reply() - reply for REVOKE FOP */
	revoke_reply_test();

	rm_utdata_fini(&test_data, OBJ_OWNER);
}

void rm_fop_funcs_test()
{
	rmfops_utinit();
	borrow_fop_funcs_test();
	revoke_fop_funcs_test();
	rmfops_utfini();
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
