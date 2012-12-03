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

static struct c2_rm_loan *test_loan;
static struct c2_rm_remote remote;

static void post_borrow_validate(int err);
static void borrow_reply_populate(struct c2_fop_rm_borrow_rep *breply,
				  int err);
static void post_borrow_cleanup(struct c2_rpc_item *item, int err);
static void borrow_fop_validate(struct c2_fop_rm_borrow *bfop);
static void post_revoke_validate(int err);
static void revoke_fop_validate(struct c2_fop_rm_revoke *rfop);
static void post_revoke_cleanup(struct c2_rpc_item *item, int err);
static void revoke_reply_populate(struct c2_fom_error_rep *rreply,
				  int err);

/*
 *****************
 * Common test functions for test cases in this file.
 ******************
 */
static void rmfops_utinit(void)
{
	c2_rm_fop_init();
}

static void rmfops_utfini(void)
{
	c2_rm_fop_fini();
}

/*
 * Prepare parameters (request) for testing RM-FOP-send functions.
 */
static void request_param_init(enum c2_rm_incoming_type reqtype)
{
	C2_SET0(&test_data.rd_in);
	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner, reqtype,
			    RIP_NONE, RIF_LOCAL_WAIT);

	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.ri_datum = NENYA;
	test_data.rd_in.rin_ops = &rings_incoming_ops;

	C2_SET0(&test_data.rd_right);
	c2_rm_right_init(&test_data.rd_right, &test_data.rd_owner);
	test_data.rd_right.ri_datum = NENYA;
	test_data.rd_right.ri_ops = &rings_right_ops;

	C2_ALLOC_PTR(test_data.rd_owner.ro_creditor);
	C2_UT_ASSERT(test_data.rd_owner.ro_creditor != NULL);
	c2_rm_remote_init(test_data.rd_owner.ro_creditor,
			  test_data.rd_owner.ro_resource);
	c2_cookie_init(&test_data.rd_owner.ro_creditor->rem_cookie,
		       &test_data.rd_owner.ro_id);
	C2_ALLOC_PTR(test_loan);
	C2_UT_ASSERT(test_loan != NULL);
	c2_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);
	remote.rem_state = REM_FREED;
	c2_rm_remote_init(&remote, &test_data.rd_res.rs_resource);
	c2_cookie_init(&remote.rem_cookie, &test_data.rd_owner.ro_id);
	test_loan->rl_other = &remote;
	c2_rm_loan_init(test_loan, &test_data.rd_right, &remote);
	c2_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);
}

/*
 * Finalise parameters (request) parameters for testing RM-FOP-send functions.
 */
static void request_param_fini()
{
	c2_free(test_loan);
	c2_free(test_data.rd_owner.ro_creditor);
	test_data.rd_owner.ro_creditor = NULL;
}

/*
 * Validate FOP and other data structures after RM-FOP-send function is called.
 */
static void rm_req_fop_validate(enum c2_rm_incoming_type reqtype)
{
	struct c2_fop_rm_borrow *bfop;
	struct c2_fop_rm_revoke *rfop;
	struct c2_rm_pin	*pin;
	struct c2_rm_loan	*loan;
	struct c2_rm_outgoing	*og;
	struct rm_out		*oreq;
	uint32_t		 pins_nr = 0;

	c2_tl_for(pi, &test_data.rd_in.rin_pins, pin) {

		C2_UT_ASSERT(pin->rp_flags == C2_RPF_TRACK);
		/* It's time to introduce ladder_of() */
		loan = bob_of(pin->rp_right, struct c2_rm_loan,
			      rl_right, &loan_bob);
		og = container_of(loan, struct c2_rm_outgoing, rog_want);
		oreq = container_of(og, struct rm_out, ou_req);

		switch (reqtype) {
		case C2_RIT_BORROW:
			bfop = c2_fop_data(&oreq->ou_fop);
			borrow_fop_validate(bfop);
			break;
		case C2_RIT_REVOKE:
			rfop = c2_fop_data(&oreq->ou_fop);
			revoke_fop_validate(rfop);
			break;
		default:
			break;
		}

		c2_rm_ur_tlist_del(pin->rp_right);
		c2_fop_fini(&oreq->ou_fop);
		rm_out_fini(oreq);

		pi_tlink_del_fini(pin);
		pr_tlink_del_fini(pin);
		c2_free(pin);

		++pins_nr;
	} c2_tl_endfor;
	C2_UT_ASSERT(pins_nr == 1);
}

/*
 * Create reply for each FOP.
 */
static struct c2_rpc_item *rm_reply_create(enum c2_rm_incoming_type reqtype,
					   int err)
{
	struct c2_fop_rm_borrow_rep *breply;
	struct c2_fom_error_rep     *rreply;

	struct c2_fop		    *fop;
	struct c2_rm_pin	    *pin;
	struct c2_rm_loan	    *loan;
	struct c2_rm_outgoing	    *og;
	struct c2_rpc_item	    *item = NULL;
	struct c2_rm_owner	    *owner;
	struct rm_out		    *oreq;
	uint32_t		     pins_nr = 0;

	c2_tl_for(pi, &test_data.rd_in.rin_pins, pin) {

		C2_UT_ASSERT(pin->rp_flags == C2_RPF_TRACK);
		/* It's time to introduce ladder_of() */
		loan = bob_of(pin->rp_right, struct c2_rm_loan,
			      rl_right, &loan_bob);
		og = container_of(loan, struct c2_rm_outgoing, rog_want);
		oreq = container_of(og, struct rm_out, ou_req);

		C2_ALLOC_PTR(fop);
		C2_UT_ASSERT(fop != NULL);
		switch (reqtype) {
		case C2_RIT_BORROW:
			item = &oreq->ou_fop.f_item;
			/* Initialise Reply FOP */
			c2_fop_init(fop, &c2_fop_rm_borrow_rep_fopt, NULL);
			C2_UT_ASSERT(c2_fop_data_alloc(fop) == 0);
			breply = c2_fop_data(fop);
			borrow_reply_populate(breply, err);
			item->ri_reply = &fop->f_item;
			break;
		case C2_RIT_REVOKE:
			item = &oreq->ou_fop.f_item;
			/* Initialise Reply FOP */
			c2_fop_init(fop, &c2_fom_error_rep_fopt, NULL);
			C2_UT_ASSERT(c2_fop_data_alloc(fop) == 0);
			rreply = c2_fop_data(fop);
			revoke_reply_populate(rreply, err);
			item->ri_reply = &fop->f_item;
			break;
		default:
			break;
		}
		owner = og->rog_want.rl_right.ri_owner;
		/* Delete the pin so that owner_balance() does not process it */
		c2_rm_owner_lock(owner);
		pi_tlink_del_fini(pin);
		pr_tlink_del_fini(pin);
		c2_rm_owner_unlock(owner);
		c2_free(pin);
		++pins_nr;
	} c2_tl_endfor;
	C2_UT_ASSERT(pins_nr == 1);

	return item;
}

/*
 * Test a reply FOP.
 */
static void reply_test(enum c2_rm_incoming_type reqtype, int err)
{
	int		    rc;
	struct c2_rpc_item *item;

	request_param_init(reqtype);

	c2_fi_enable_once("c2_rm_request_out", "no-rpc");
	switch (reqtype) {
	case C2_RIT_BORROW:
		test_data.rd_in.rin_flags |= RIF_MAY_BORROW;
		rc = c2_rm_request_out(C2_ROT_BORROW, &test_data.rd_in, NULL,
				       &test_data.rd_right);
		C2_UT_ASSERT(rc == 0);
		item = rm_reply_create(C2_RIT_BORROW, err);
		borrow_reply(item);
		post_borrow_validate(err);
		post_borrow_cleanup(item, err);
		outreq_free(item);
		break;
	case C2_RIT_REVOKE:
		test_data.rd_in.rin_flags |= RIF_MAY_REVOKE;
		rc = c2_rm_request_out(C2_ROT_REVOKE, &test_data.rd_in,
				       test_loan, &test_loan->rl_right);
		item = rm_reply_create(C2_RIT_REVOKE, err);
		c2_rm_owner_lock(&test_data.rd_owner);
		c2_rm_ur_tlist_add(&test_data.rd_owner.ro_sublet, &test_loan->rl_right);
		c2_rm_owner_unlock(&test_data.rd_owner);
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
		C2_IMPOSSIBLE("Invalid RM-FOM type");
	}

	request_param_fini();
}

/*
 * Test a request FOP.
 */
static void request_test(enum c2_rm_incoming_type reqtype)
{
	int rc;

	request_param_init(reqtype);

	c2_fi_enable_once("c2_rm_request_out", "no-rpc");
	switch (reqtype) {
	case C2_RIT_BORROW:
		test_data.rd_in.rin_flags |= RIF_MAY_BORROW;
		rc = c2_rm_request_out(C2_ROT_BORROW, &test_data.rd_in, NULL,
				       &test_data.rd_right);
		break;
	case C2_RIT_REVOKE:
		test_data.rd_in.rin_flags |= RIF_MAY_REVOKE;
		rc = c2_rm_request_out(C2_ROT_REVOKE, &test_data.rd_in,
				       test_loan, &test_loan->rl_right);
		break;
	default:
		C2_IMPOSSIBLE("Invalid RM-FOM type");
	}
	C2_UT_ASSERT(rc == 0);

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
	bool got_right;

	c2_rm_owner_lock(&test_data.rd_owner);
	got_right = !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed) &&
		    !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]);
	c2_rm_owner_unlock(&test_data.rd_owner);
	C2_UT_ASSERT(ergo(err, !got_right));
	C2_UT_ASSERT(ergo(err == 0, got_right));
}

static void borrow_reply_populate(struct c2_fop_rm_borrow_rep *breply,
				  int err)
{
	int rc;

	breply->br_rc.rerr_rc = err;

	if (err == 0) {
		rc = c2_rm_right_encode(&test_data.rd_right,
					&breply->br_right.ri_opaque);
		C2_UT_ASSERT(rc == 0);
	}
}

static void post_borrow_cleanup(struct c2_rpc_item *item, int err)
{
	struct c2_rm_right	    *right;
	struct c2_rm_loan	    *loan;
	struct c2_fop		    *fop;
	struct c2_fop_rm_borrow_rep *breply;
	struct c2_fop_rm_borrow	    *bfop;

	/*
	 * Clean-up borrow request and reply FOPs first.
	 */
	fop = c2_rpc_item_to_fop(item->ri_reply);
	breply = c2_fop_data(fop);

	c2_buf_free(&breply->br_right.ri_opaque);
	c2_fop_fini(fop);
	c2_free(fop);

	fop = c2_rpc_item_to_fop(item);
	bfop = c2_fop_data(fop);
	c2_buf_free(&bfop->bo_base.rrq_right.ri_opaque);

	/*
	 * A borrow error leaves owner lists unaffected.
	 * If borrow succeeds, the owner lists are updated. Hence they
	 * need to be cleaned-up.
	 */
	if (err)
		return;

	c2_rm_owner_lock(&test_data.rd_owner);
	c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED], right) {
		c2_rm_ur_tlink_del_fini(right);
		c2_free(right);
	} c2_tl_endfor;

	c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_borrowed, right) {
		c2_rm_ur_tlink_del_fini(right);
		loan = bob_of(right, struct c2_rm_loan, rl_right, &loan_bob);
		c2_free(loan);
	} c2_tl_endfor;
	c2_rm_owner_unlock(&test_data.rd_owner);
}

/*
 * Check if c2_rm_request_out() has filled the FOP correctly
 */
static void borrow_fop_validate(struct c2_fop_rm_borrow *bfop)
{
	struct c2_rm_owner *owner;
	struct c2_rm_right  right;
	int		    rc;

	owner = c2_cookie_of(&bfop->bo_base.rrq_owner.ow_cookie,
			     struct c2_rm_owner, ro_id);

	C2_UT_ASSERT(owner != NULL);
	C2_UT_ASSERT(owner == &test_data.rd_owner);

	c2_rm_right_init(&right, &test_data.rd_owner);
	right.ri_ops = &rings_right_ops;
	rc = c2_rm_right_decode(&right, &bfop->bo_base.rrq_right.ri_opaque);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(right.ri_datum == test_data.rd_right.ri_datum);
	c2_rm_right_fini(&right);

	C2_UT_ASSERT(bfop->bo_base.rrq_policy == RIP_NONE);
	C2_UT_ASSERT(bfop->bo_base.rrq_flags &
			(RIF_LOCAL_WAIT | RIF_MAY_BORROW));
	c2_buf_free(&bfop->bo_base.rrq_right.ri_opaque);
}

/*
 * Test function for c2_rm_request_out() for BORROW FOP.
 */
static void borrow_request_test()
{
	request_test(C2_RIT_BORROW);
}

/*
 * Test function for borrow_reply().
 */
static void borrow_reply_test()
{
	/* 1. Test borrow-success */
	reply_test(C2_RIT_BORROW, 0);

	/* 2. Test borrow-failure */
	reply_test(C2_RIT_BORROW, -ETIMEDOUT);
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

	c2_rm_owner_lock(&test_data.rd_owner);
	sublet = !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet);
	owned = !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]);
	c2_rm_owner_unlock(&test_data.rd_owner);

	/* If revoke fails, right remains in sublet list */
	C2_UT_ASSERT(ergo(err, sublet && !owned));
	/* If revoke succeds, right will be part of cached list*/
	C2_UT_ASSERT(ergo(err == 0, owned && !sublet));
}

/*
 * Check if c2_rm_request_out() has filled the FOP correctly
 */
static void revoke_fop_validate(struct c2_fop_rm_revoke *rfop)
{
	struct c2_rm_owner *owner;
	struct c2_rm_right  right;
	struct c2_rm_loan  *loan;
	int		    rc;

	owner = c2_cookie_of(&rfop->rr_base.rrq_owner.ow_cookie,
			     struct c2_rm_owner, ro_id);

	C2_UT_ASSERT(owner != NULL);
	C2_UT_ASSERT(owner == &test_data.rd_owner);

	loan = c2_cookie_of(&rfop->rr_loan.lo_cookie, struct c2_rm_loan, rl_id);
	C2_UT_ASSERT(loan != NULL);
	C2_UT_ASSERT(loan == test_loan);

	c2_rm_right_init(&right, &test_data.rd_owner);
	right.ri_ops = &rings_right_ops;
	rc = c2_rm_right_decode(&right, &rfop->rr_base.rrq_right.ri_opaque);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(right.ri_datum == test_data.rd_right.ri_datum);
	c2_rm_right_fini(&right);

	C2_UT_ASSERT(rfop->rr_base.rrq_policy == RIP_NONE);
	C2_UT_ASSERT(rfop->rr_base.rrq_flags &
		     (RIF_LOCAL_WAIT | RIF_MAY_REVOKE));
	c2_buf_free(&rfop->rr_base.rrq_right.ri_opaque);
}

static void post_revoke_cleanup(struct c2_rpc_item *item, int err)
{
	struct c2_fop_rm_reovke_rep *rreply;
	struct c2_fop_rm_revoke	    *rfop;
	struct c2_rm_right	    *right;
	struct c2_rm_loan	    *loan;
	struct c2_fop		    *fop;

	/*
	 * Clean-up revoke request and reply FOPs first.
	 */
	fop = c2_rpc_item_to_fop(item->ri_reply);
	rreply = c2_fop_data(fop);

	c2_fop_fini(fop);
	c2_free(fop);

	fop = c2_rpc_item_to_fop(item);
	rfop = c2_fop_data(fop);
	c2_buf_free(&rfop->rr_base.rrq_right.ri_opaque);

	/*
	 * After a successful revoke, sublet right is transferred to
	 * OWOS_CACHED. Otherwise it remains in the sublet list.
	 * Clean up the lists.
	 */
	c2_rm_owner_lock(&test_data.rd_owner);
	if (err == 0) {
		c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED],
			  right) {
			c2_rm_ur_tlink_del_fini(right);
			c2_free(right);
		} c2_tl_endfor;
	} else {
		c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_sublet, right) {
			c2_rm_ur_tlink_del_fini(right);
			loan = bob_of(right, struct c2_rm_loan,
				      rl_right, &loan_bob);
			c2_free(loan);
		} c2_tl_endfor;
	}
	c2_rm_owner_unlock(&test_data.rd_owner);
}

static void revoke_reply_populate(struct c2_fom_error_rep *rreply,
				  int err)
{
	rreply->rerr_rc = err;
}

/*
 * Test function for c2_rm_request_out() for REVOKE FOP.
 */
static void revoke_request_test()
{
	request_test(C2_RIT_REVOKE);
}

/*
 * Test function for revoke_reply().
 */
static void revoke_reply_test()
{
	/* 1. Test revoke-success */
	reply_test(C2_RIT_REVOKE, 0);

	/* 2. Test revoke-failure */
	reply_test(C2_RIT_REVOKE, -ETIMEDOUT);
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

	/* 1. Test c2_rm_request_out() - sending BORROW FOP */
	borrow_request_test();

	/* 2. Test borrow_reply() - reply for BORROW FOP */
	borrow_reply_test();

	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void revoke_fop_funcs_test(void)
{
	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* 1. Test c2_rm_request_out() - sending REVOKE FOP */
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
