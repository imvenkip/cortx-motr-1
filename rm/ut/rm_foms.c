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

#include "lib/memory.h"
#include "lib/ut.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "ut/rpc.h"
#include "rm/rm.h"
#include "reqh/reqh.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"
#include "rm/rm_foms.c"          /* To access static APIs. */

enum rights_test_type {
	RM_UT_FULL_RIGHTS_TEST=1,
	RM_UT_PARTIAL_RIGHTS_TEST,
	RM_UT_INVALID_RIGHTS_TEST,
};

static struct c2_fom_locality  dummy_loc;
static struct c2_rm_loan      *test_loan;
struct c2_reqh		       reqh;

/*
 *****************
 * Common test functions for test cases in this file.
 ******************
 */
static void fom_phase_set(struct c2_fom *fom, int phase)
{
	if (c2_fom_phase(fom) == C2_FOPH_SUCCESS) {
		c2_fom_phase_set(fom, C2_FOPH_FOL_REC_ADD);
		c2_fom_phase_set(fom, C2_FOPH_TXN_COMMIT);
	} else if (c2_fom_phase(fom) == C2_FOPH_FAILURE) {
		c2_fom_phase_set(fom, C2_FOPH_TXN_ABORT);
	}

	if (C2_IN(c2_fom_phase(fom), (C2_FOPH_TXN_COMMIT,
				      C2_FOPH_TXN_ABORT)))
		c2_fom_phase_set(fom, C2_FOPH_QUEUE_REPLY);
	c2_fom_phase_set(fom, phase);
}

static void rmfoms_utinit(void)
{
	int rc;

	c2_rm_fop_init();
	rc = c2_reqh_init(&reqh, (void *)1, (void *)1,
			  (void *)1, (void *)1, NULL);
	C2_UT_ASSERT(rc == 0);
	dummy_loc.fl_dom = &reqh.rh_fom_dom;
        c2_sm_group_init(&dummy_loc.fl_group);
}

static void rmfoms_utfini(void)
{
        c2_sm_group_fini(&dummy_loc.fl_group);
	c2_reqh_fini(&reqh);
	c2_rm_fop_fini();
}

/*
 * Create and initialise RM FOMs.
 */
static void fom_create(enum c2_rm_incoming_type fomtype,
		       struct c2_fop *fop, struct c2_fom **fom)
{
	int		rc;
	struct c2_fom  *base_fom;

	switch (fomtype) {
	case C2_RIT_BORROW:
		rc = borrow_fom_create(fop, fom);
		break;
	case C2_RIT_REVOKE:
		rc = revoke_fom_create(fop, fom);
		break;
	default:
		C2_IMPOSSIBLE("Invalid RM-FOM type");
	}
	C2_UT_ASSERT(rc == 0);

	base_fom = *fom;
	base_fom->fo_fop = fop;

	base_fom->fo_loc = &dummy_loc;
	base_fom->fo_loc->fl_dom->fd_reqh = &reqh;
	C2_CNT_INC(base_fom->fo_loc->fl_foms);
	c2_fom_sm_init(base_fom);
}

static void fom_fini(struct c2_fom *fom, enum c2_rm_incoming_type fomtype)
{
	struct c2_fop  *reply_fop;

	reply_fop = fom->fo_rep_fop;
	fom_phase_set(fom, C2_FOPH_FINISH);

	switch (fomtype) {
	case C2_RIT_BORROW:
		borrow_fom_fini(fom);
		break;
	case C2_RIT_REVOKE:
		revoke_fom_fini(fom);
		break;
	default:
		C2_IMPOSSIBLE("Invalid RM-FOM type");
	}
}

/*
 * Allocate desired FOP and populate test-data in it.
 */
static struct c2_fop *fop_alloc(enum c2_rm_incoming_type fomtype)
{
	struct c2_fop *fop;

	switch (fomtype) {
	case C2_RIT_BORROW:
		fop = c2_fop_alloc(&c2_fop_rm_borrow_fopt, NULL);
		C2_UT_ASSERT(fop != NULL);
		break;
	case C2_RIT_REVOKE:
		fop = c2_fop_alloc(&c2_fop_rm_revoke_fopt, NULL);
		C2_UT_ASSERT(fop != NULL);
		break;
	default:
		C2_IMPOSSIBLE("Invalid RM-FOM type");
		break;
	}
	return fop;
}

/*
 * Accept a RM-FOM. Delete FOP within FOM.
 */
static void fop_dealloc(struct c2_fom *fom)
{
	c2_fop_free(fom->fo_fop);
}

#ifdef RM_ITEM_FREE
/*
 * A generic RM-FOM-delete verification UT function. Check memory usage.
 */
static void fom_fini_test(enum c2_rm_incoming_type fomtype)
{
	size_t	       tot_mem;
	size_t	       base_mem;
	struct c2_fom *fom;
	struct c2_fop *fop;

	/*
	 * 1. Allocate FOM object of interest
	 * 2. Calculate memory usage before and after object allocation
	 *    and de-allocation.
	 */
	base_mem = c2_allocated();
	fop = fop_alloc(fomtype);

	fom_create(fomtype, fop, &fom);

	/*
	 * Ensure - after fom_fini() memory usage drops back to original value
	 */
	fom_fini(fom, fomtype);
	fop_dealloc(fom);
	tot_mem = c2_allocated();
	C2_UT_ASSERT(tot_mem == base_mem);
}
#endif

static void fom_create_test(enum c2_rm_incoming_type fomtype)
{
	struct c2_fom *fom;
	struct c2_fop *fop;

	fop = fop_alloc(fomtype);

	fom_create(fomtype, fop, &fom);
	C2_UT_ASSERT(fom != NULL);
	fop_dealloc(fom);
	fom_phase_set(fom, C2_FOPH_SUCCESS);
	fom_fini(fom, fomtype);
}

/*
 *****************
 * RM Borrow-FOM test functions
 ******************
 */
/*
 * Populate the fake (test) RM-BORROW FOP.
 */
static void brw_fop_populate(struct c2_fom *fom, enum rights_test_type test)
{
	struct c2_fop_rm_borrow *brw_fop;
	struct c2_rm_right	 right;

	brw_fop = c2_fop_data(fom->fo_fop);
	C2_UT_ASSERT(brw_fop != NULL);

	brw_fop->bo_base.rrq_policy = RIP_NONE;
	brw_fop->bo_base.rrq_flags = RIF_LOCAL_WAIT;

	c2_cookie_init(&brw_fop->bo_creditor.ow_cookie,
		       &test_data.rd_owner.ro_id);
	c2_cookie_init(&brw_fop->bo_base.rrq_owner.ow_cookie,
		       &test_data.rd_owner.ro_id);
	c2_rm_right_init(&right, &test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_RIGHTS_TEST:
		right.ri_datum = ALLRINGS;
		break;
	case RM_UT_PARTIAL_RIGHTS_TEST:
		right.ri_datum = VILYA;
		break;
	case RM_UT_INVALID_RIGHTS_TEST:
		right.ri_datum = INVALID_RING;
		break;
	}
	c2_rm_right_encode(&right, &brw_fop->bo_base.rrq_right.ri_opaque);
	c2_rm_right_fini(&right);
}

static void brw_test_cleanup()
{
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_sublet, right) {
		c2_rm_ur_tlink_del_fini(right);
		loan = container_of(right, struct c2_rm_loan, rl_right);
		c2_rm_loan_fini(loan);
		c2_free(loan);
	} c2_tl_endfor;
}

/*
 * Validate the test results.
 */
static void brw_fom_state_validate(struct c2_fom *fom, int32_t rc,
				   enum rights_test_type test)
{
	struct c2_fop_rm_borrow *brw_fop;

	c2_rm_owner_lock(&test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_SUCCESS);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		C2_UT_ASSERT(
		    !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet));
		C2_UT_ASSERT(
			c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		break;
	case RM_UT_PARTIAL_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_SUCCESS);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		C2_UT_ASSERT(
		    !c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet));
		C2_UT_ASSERT(
			!c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		break;
	case RM_UT_INVALID_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_FAILURE);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		break;
	}
	c2_rm_owner_unlock(&test_data.rd_owner);
	brw_fop = c2_fop_data(fom->fo_fop);
	C2_UT_ASSERT(brw_fop != NULL);
	c2_buf_free(&brw_fop->bo_base.rrq_right.ri_opaque);
}
/*
 * Test function for testing BORROW FOM functions.
 */
static void brw_fom_state_test(enum rights_test_type test)
{
	struct c2_fom	      *fom;
	struct c2_fop	      *fop;
	struct rm_request_fom *rfom;
	int		       rc;

	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* Add self-loan to the test owner object */
	rm_test_owner_capital_raise(&test_data.rd_owner, &test_data.rd_right);

	fop = fop_alloc(C2_RIT_BORROW);

	fom_create(C2_RIT_BORROW, fop, &fom);
	C2_UT_ASSERT(fom != NULL);

	brw_fop_populate(fom, test);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	fom_phase_set(fom, FOPH_RM_REQ_START);
	/*
	 * Call the first phase of FOM.
	 */
	rc = borrow_fom_tick(fom);
	C2_UT_ASSERT(c2_fom_phase(fom) == FOPH_RM_REQ_FINISH);
	C2_UT_ASSERT(rc == C2_FSO_AGAIN);

	/*
	 * Call the second phase of FOM.
	 */
	rc = borrow_fom_tick(fom);
	brw_fom_state_validate(fom, rc, test);

	fop_dealloc(fom);
	fom_fini(fom, C2_RIT_BORROW);
	brw_test_cleanup();
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

/*
 * Test function for brw_fom_create().
 */
static void brw_fom_create_test()
{
	fom_create_test(C2_RIT_BORROW);
}

#ifdef RPC_ITEM_FREE
/*
 * Test function for brw_fom_fini().
 */
static void brw_fom_fini_test()
{
	fom_fini_test(C2_RIT_BORROW);
}
#endif

/*
 *****************
 * RM Revoke-FOM test functions
 ******************
 */
static void rvk_data_setup(enum rights_test_type test)
{
	struct c2_rm_right *right;

	/*
	 * This right will be finalised in c2_rm_revoke_commit().
	 */
	C2_ALLOC_PTR(right);
	C2_UT_ASSERT(right != NULL);
	c2_rm_right_init(right, &test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_RIGHTS_TEST:
		right->ri_datum = (uint64_t)VILYA;
		break;
	case RM_UT_PARTIAL_RIGHTS_TEST:
		right->ri_datum = (uint64_t)ALLRINGS;
		break;
	case RM_UT_INVALID_RIGHTS_TEST:
		right->ri_datum = (uint64_t)NENYA;
		break;
	}

	C2_ALLOC_PTR(test_loan);
	C2_UT_ASSERT(test_loan != NULL);

	c2_rm_loan_init(test_loan, right, NULL);

	test_loan->rl_id = C2_RM_LOAN_SELF_ID + test;

	C2_ALLOC_PTR(test_loan->rl_other);
	C2_UT_ASSERT(test_loan->rl_other != NULL);
	c2_rm_remote_init(test_loan->rl_other, test_data.rd_owner.ro_resource);
	test_loan->rl_other->rem_state = REM_OWNER_LOCATED;
	c2_cookie_init(&test_loan->rl_other->rem_cookie,
		       &test_data.rd_owner.ro_id);
	c2_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);
	c2_rm_owner_lock(&test_data.rd_owner);
	c2_rm_ur_tlist_add(&test_data.rd_owner.ro_borrowed,
			   &test_loan->rl_right);
	c2_rm_ur_tlist_add(&test_data.rd_owner.ro_owned[OWOS_CACHED], right);

	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(
			     &test_data.rd_owner.ro_owned[OWOS_CACHED]));
	c2_rm_owner_unlock(&test_data.rd_owner);
}

/*
 * Populate the fake (test) RM-REVOKE FOP.
 */
static void rvk_fop_populate(struct c2_fom *fom)
{
	struct c2_fop_rm_revoke *rvk_fop;
	struct c2_rm_right	 right;

	rvk_fop = c2_fop_data(fom->fo_fop);
	C2_UT_ASSERT(rvk_fop != NULL);

	rvk_fop->rr_base.rrq_policy = RIP_NONE;
	rvk_fop->rr_base.rrq_flags = RIF_LOCAL_WAIT;

	c2_rm_right_init(&right, &test_data.rd_owner);
	right.ri_datum = VILYA;
	c2_rm_right_encode(&right, &rvk_fop->rr_base.rrq_right.ri_opaque);

	c2_cookie_init(&rvk_fop->rr_loan.lo_cookie, &test_loan->rl_id);
	c2_cookie_init(&rvk_fop->rr_base.rrq_owner.ow_cookie,
		       &test_data.rd_owner.ro_id);
	c2_rm_right_fini(&right);
}

static void rvk_test_cleanup()
{
	struct c2_rm_right *right;
	struct c2_rm_loan  *loan;

	c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED], right) {
		c2_rm_ur_tlink_del_fini(right);
		c2_rm_right_fini(right);
		c2_free(right);
	} c2_tl_endfor;

	c2_tl_for(c2_rm_ur, &test_data.rd_owner.ro_borrowed, right) {
		c2_rm_ur_tlink_del_fini(right);
		loan = container_of(right, struct c2_rm_loan, rl_right);
		c2_rm_loan_fini(loan);
		c2_free(loan);
	} c2_tl_endfor;
}

/*
 * Validate the test results.
 */
static void rvk_fom_state_validate(struct c2_fom *fom, int32_t rc,
				   enum rights_test_type test)
{
	struct c2_fop_rm_revoke *rvk_fop;

	c2_rm_owner_lock(&test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_SUCCESS);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		C2_UT_ASSERT(
			c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		C2_UT_ASSERT(
		    c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
		break;
	case RM_UT_PARTIAL_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_SUCCESS);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		C2_UT_ASSERT(
			!c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		C2_UT_ASSERT(
			!c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_borrowed));
		rvk_test_cleanup();
		break;
	case RM_UT_INVALID_RIGHTS_TEST:
		C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_FAILURE);
		C2_UT_ASSERT(rc == C2_FSO_AGAIN);
		C2_UT_ASSERT(
			!c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		C2_UT_ASSERT(
			!c2_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_borrowed));
		rvk_test_cleanup();
		break;
	}
	c2_rm_owner_unlock(&test_data.rd_owner);
	rvk_fop = c2_fop_data(fom->fo_fop);
	C2_UT_ASSERT(rvk_fop != NULL);
	c2_buf_free(&rvk_fop->rr_base.rrq_right.ri_opaque);
}

/*
 * Test function for to test REVOKE FOM states().
 */
static void rvk_fom_state_test(enum rights_test_type test)
{
	struct c2_fom	      *fom;
	struct c2_fop	      *fop;
	struct rm_request_fom *rfom;
	int		       rc;

	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	rvk_data_setup(test);

	fop = fop_alloc(C2_RIT_REVOKE);

	fom_create(C2_RIT_REVOKE, fop, &fom);
	C2_UT_ASSERT(fom != NULL);

	rvk_fop_populate(fom);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	fom_phase_set(fom, FOPH_RM_REQ_START);
	/*
	 * Call the first FOM phase.
	 */
	rc = revoke_fom_tick(fom);
	C2_UT_ASSERT(c2_fom_phase(fom) == FOPH_RM_REQ_FINISH);
	C2_UT_ASSERT(rc == C2_FSO_AGAIN);

	/*
	 * Call the second FOM phase.
	 */
	rc = revoke_fom_tick(fom);
	rvk_fom_state_validate(fom, rc, test);

	fop_dealloc(fom);
	fom_fini(fom, C2_RIT_REVOKE);
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

/*
 * Test function for rvk_fom_create().
 */
static void rvk_fom_create_test()
{
	fom_create_test(C2_RIT_REVOKE);
}

#ifdef RPC_ITEM_FREE
/*
 * Test function for rvk_fom_fini().
 */
static void rvk_fom_fini_test()
{
	fom_fini_test(C2_RIT_REVOKE);
}
#endif

/*
 *****************
 * Top level test functions
 ******************
 */
static void borrow_fom_funcs_test(void)
{
	/* Test for brw_fom_create() */
	brw_fom_create_test();

#ifdef RPC_ITEM_FREE
	/* Test for brw_fom_fini() */
	brw_fom_fini_test();
#endif

	/* 1. Test borrowing part (partial) of the rights available */
	brw_fom_state_test(RM_UT_PARTIAL_RIGHTS_TEST);

	/* 2. Test borrowing full rights available */
	brw_fom_state_test(RM_UT_FULL_RIGHTS_TEST);

	/* 3. Test borrowing an invalid right (not available) */
	brw_fom_state_test(RM_UT_INVALID_RIGHTS_TEST);

}

static void revoke_fom_funcs_test(void)
{
	/* Test for rvk_fom_create() */
	rvk_fom_create_test();

#ifdef RPC_ITEM_FREE
	/* Test for rvk_fom_fini() */
	rvk_fom_fini_test();
#endif

	/* 1. Test revoke of entire rights that were borrowed */
	rvk_fom_state_test(RM_UT_FULL_RIGHTS_TEST);

	/* 2. Test revoke of part of rights that were borrowed */
	rvk_fom_state_test(RM_UT_PARTIAL_RIGHTS_TEST);

	/* 3. Test revoke of an invalid right */
	rvk_fom_state_test(RM_UT_INVALID_RIGHTS_TEST);

}

void rm_fom_funcs_test()
{
	rmfoms_utinit();
	c2_sm_group_lock(&dummy_loc.fl_group);
	borrow_fom_funcs_test();
	revoke_fom_funcs_test();
	c2_sm_group_unlock(&dummy_loc.fl_group);
	rmfoms_utfini();
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
