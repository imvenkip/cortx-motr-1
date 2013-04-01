/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
#include "ut/ut.h"
#include "lib/finject.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "reqh/reqh.h"
#include "fop/ut/fop_put_norpc.h"
#include "rm/rm.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"
#include "rm/rm_foms.c"          /* To access static APIs. */
#include "ut/ut.h"		/* m0_ut_fom_phase_set() */

enum test_type {
	RM_UT_FULL_CREDITS_TEST=1,
	RM_UT_PARTIAL_CREDITS_TEST,
	RM_UT_INVALID_CREDITS_TEST,
	RM_UT_MEMFAIL_TEST,
};

static struct m0_fom_locality  dummy_loc;
static struct m0_rm_loan      *test_loan;
struct m0_reqh		       reqh;
static struct m0_dbenv         dbenv;

extern void m0_remotes_tlist_add(struct m0_tl *tl, struct m0_rm_remote *rem);
extern void m0_remotes_tlist_del(struct m0_rm_remote *rem);
extern const struct m0_tl_descr m0_remotes_tl;

/*
 *****************
 * Common test functions for test cases in this file.
 ******************
 */
static void rmfoms_utinit(void)
{
	int rc;
	rc = m0_dbenv_init(&dbenv, "something", 0);
	M0_UT_ASSERT(rc == 0);

	rc = M0_REQH_INIT(&reqh,
			.rhia_dtm       = (void*)1,
			.rhia_db        = &dbenv,
			.rhia_mdstore   = (void*)1,
			.rhia_fol       = (void*)1,
			.rhia_svc       = (void*)1,
			.rhia_addb_stob = NULL);
	M0_UT_ASSERT(rc == 0);
	m0_reqh_start(&reqh);
	dummy_loc.fl_dom = &reqh.rh_fom_dom;
        m0_sm_group_init(&dummy_loc.fl_group);
}

static void rmfoms_utfini(void)
{
        m0_sm_group_fini(&dummy_loc.fl_group);
	m0_reqh_services_terminate(&reqh);
	m0_reqh_fini(&reqh);
}

/*
 * Allocate desired FOP and populate test-data in it.
 */
static struct m0_fop *fop_alloc(enum m0_rm_incoming_type fomtype)
{
	struct m0_fop *fop = NULL;

	switch (fomtype) {
	case M0_RIT_BORROW:
		fop = m0_fop_alloc(&m0_rm_fop_borrow_fopt, NULL);
		M0_UT_ASSERT(fop != NULL);
		break;
	case M0_RIT_REVOKE:
		fop = m0_fop_alloc(&m0_rm_fop_revoke_fopt, NULL);
		M0_UT_ASSERT(fop != NULL);
		break;
	default:
		M0_IMPOSSIBLE("Invalid RM-FOM type");
		break;
	}
	return fop;
}

/*
 * Accept a RM-FOM. Delete FOP within FOM.
 */
static void fop_dealloc(struct m0_fop *fop)
{
	M0_UT_ASSERT(fop != NULL);
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put(fop);
}

/*
 * Create and initialise RM FOMs.
 */
static void fom_create(enum m0_rm_incoming_type fomtype,
		       bool err_test,
		       struct m0_fop *fop,
		       struct m0_fom **fom)
{
	int            rc = 0;
	struct m0_fom *base_fom;

	if (err_test)
		m0_fi_enable_once("m0_alloc", "fail_allocation");

	switch (fomtype) {
	case M0_RIT_BORROW:
		rc = borrow_fom_create(fop, fom, &reqh);
		break;
	case M0_RIT_REVOKE:
		rc = revoke_fom_create(fop, fom, &reqh);
		break;
	default:
		M0_IMPOSSIBLE("Invalid RM-FOM type");
	}
	M0_UT_ASSERT(ergo(err_test, rc == -ENOMEM));
	M0_UT_ASSERT(ergo(!err_test, rc == 0));

	if (!err_test) {
		base_fom = *fom;
		base_fom->fo_fop = fop;

		base_fom->fo_loc = &dummy_loc;
		base_fom->fo_loc->fl_dom->fd_reqh = &reqh;
		M0_CNT_INC(base_fom->fo_loc->fl_foms);
		m0_fom_sm_init(base_fom);
	}
}

static void fom_fini(struct m0_fom *fom, enum m0_rm_incoming_type fomtype)
{
	struct m0_fop *fop = fom->fo_fop;

	m0_ut_fom_phase_set(fom, M0_FOPH_FINISH);

	fom_fop_put_norpc(fom);

	switch (fomtype) {
	case M0_RIT_BORROW:
		borrow_fom_fini(fom);
		break;
	case M0_RIT_REVOKE:
		revoke_fom_fini(fom);
		break;
	default:
		M0_IMPOSSIBLE("Invalid RM-FOM type");
	}
	/* xxx_fom_fini() releases fom. Hence fop pointers are stroed */
	fop_dealloc(fop);
}

/*
 * A generic RM-FOM-delete verification UT function. Check memory usage.
 */
static void fom_fini_test(enum m0_rm_incoming_type fomtype)
{
	size_t	       tot_mem;
	size_t	       base_mem;
	struct m0_fom *fom;
	struct m0_fop *fop;

	/*
	 * 1. Allocate FOM object of interest
	 * 2. Calculate memory usage before and after object allocation
	 *    and de-allocation.
	 */
	base_mem = m0_allocated();
	fop = fop_alloc(fomtype);

	fom_create(fomtype, false, fop, &fom);

	/*
	 * Ensure - after fom_fini() memory usage drops back to original value
	 */
	fom_fini(fom, fomtype);
	tot_mem = m0_allocated();
	M0_UT_ASSERT(tot_mem == base_mem);
}

static void fom_create_test(enum m0_rm_incoming_type fomtype,
			    bool err_test)
{
	struct m0_fom *fom = NULL;
	struct m0_fop *fop;

	fop = fop_alloc(fomtype);
	fom_create(fomtype, err_test, fop, &fom);
	if (!err_test) {
		M0_UT_ASSERT(fom != NULL);
		m0_ut_fom_phase_set(fom, M0_FOPH_SUCCESS);
		fom_fini(fom, fomtype);
	} else
		fop_dealloc(fop);
}

/*
 *****************
 * RM Borrow-FOM test functions
 ******************
 */
/*
 * Populate the fake (test) RM-BORROW FOP.
 */
static void brw_fop_populate(struct m0_fom *fom, enum test_type test)
{
	struct m0_rm_fop_borrow *brw_fop;
	struct m0_rm_credit	 credit;

	brw_fop = m0_fop_data(fom->fo_fop);
	M0_UT_ASSERT(brw_fop != NULL);

	brw_fop->bo_base.rrq_policy = RIP_NONE;
	brw_fop->bo_base.rrq_flags = RIF_LOCAL_WAIT;

	m0_cookie_init(&brw_fop->bo_creditor.ow_cookie,
		       &test_data.rd_owner.ro_id);
	m0_cookie_init(&brw_fop->bo_base.rrq_owner.ow_cookie,
		       &test_data.rd_owner.ro_id);
	m0_rm_credit_init(&credit, &test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_CREDITS_TEST:
	case RM_UT_MEMFAIL_TEST:
		credit.cr_datum = ALLRINGS;
		break;
	case RM_UT_PARTIAL_CREDITS_TEST:
		credit.cr_datum = VILYA;
		break;
	case RM_UT_INVALID_CREDITS_TEST:
		credit.cr_datum = INVALID_RING;
		break;
	}
	m0_rm_credit_encode(&credit, &brw_fop->bo_base.rrq_credit.cr_opaque);
	m0_rm_credit_fini(&credit);
}

static void brw_test_cleanup(void)
{
	struct m0_rm_credit *credit;
	struct m0_rm_loan  *loan;

	m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_sublet, credit) {
		m0_rm_ur_tlink_del_fini(credit);
		loan = container_of(credit, struct m0_rm_loan, rl_credit);
		m0_rm_loan_fini(loan);
		m0_free(loan);
	} m0_tl_endfor;
}

/*
 * Validate the test results.
 */
static void brw_fom_state_validate(struct m0_fom *fom, int32_t rc,
				   enum test_type test)
{
	struct m0_rm_fop_borrow *brw_fop;

	m0_rm_owner_lock(&test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_CREDITS_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_SUCCESS);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		M0_UT_ASSERT(
		    !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet));
		M0_UT_ASSERT(
			m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		M0_UT_ASSERT(
			m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_HELD]));
		break;
	case RM_UT_PARTIAL_CREDITS_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_SUCCESS);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		M0_UT_ASSERT(
		    !m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_sublet));
		M0_UT_ASSERT(
			!m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		M0_UT_ASSERT(
			m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_HELD]));
		break;
	case RM_UT_INVALID_CREDITS_TEST:
	case RM_UT_MEMFAIL_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_FAILURE);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		break;
	}
	m0_rm_owner_unlock(&test_data.rd_owner);
	brw_fop = m0_fop_data(fom->fo_fop);
	M0_UT_ASSERT(brw_fop != NULL);
	m0_buf_free(&brw_fop->bo_base.rrq_credit.cr_opaque);
}

/*
 * Test function for testing BORROW FOM functions.
 */
static void brw_fom_state_test(enum test_type test)
{
	struct m0_fom *fom;
	struct m0_fop *fop;
	int	       rc;

	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* Add self-loan to the test owner object */
	rm_test_owner_capital_raise(&test_data.rd_owner, &test_data.rd_credit);

	fop = fop_alloc(M0_RIT_BORROW);

	/*
	 * Create FOM and set the FOM phase to start processing of the request.
	 */
	fom_create(M0_RIT_BORROW, false, fop, &fom);
	M0_UT_ASSERT(fom != NULL);
	brw_fop_populate(fom, test);

	m0_ut_fom_phase_set(fom, FOPH_RM_REQ_START);

	/*
	 * Call the first phase of FOM.
	 */
	if (test == RM_UT_MEMFAIL_TEST)
		m0_fi_enable_once("rings_credit_copy", "fail_copy");
	rc = borrow_fom_tick(fom);
	M0_UT_ASSERT(m0_fom_phase(fom) == FOPH_RM_REQ_FINISH);
	M0_UT_ASSERT(rc == M0_FSO_AGAIN);

	/*
	 * Call the second phase of FOM.
	 */
	rc = borrow_fom_tick(fom);
	brw_fom_state_validate(fom, rc, test);

	fom_fini(fom, M0_RIT_BORROW);
	brw_test_cleanup();
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

/*
 * Test function for brw_fom_create().
 */
static void brw_fom_create_test(void)
{
	/* 1. Test memory failure */
	fom_create_test(M0_RIT_BORROW, true);
	/* 2. Test success */
	fom_create_test(M0_RIT_BORROW, false);
}

/*
 * Test function for brw_fom_fini().
 */
static void brw_fom_fini_test(void)
{
	fom_fini_test(M0_RIT_BORROW);
}

/*
 *****************
 * RM Revoke-FOM test functions
 ******************
 */
static void rvk_data_setup(enum test_type test)
{
	struct m0_rm_credit *credit;
	struct m0_rm_remote *remote;

	M0_ALLOC_PTR(credit);
	M0_UT_ASSERT(credit != NULL);
	m0_rm_credit_init(credit, &test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_CREDITS_TEST:
	case RM_UT_MEMFAIL_TEST:
		credit->cr_datum = (uint64_t)VILYA;
		break;
	case RM_UT_PARTIAL_CREDITS_TEST:
		credit->cr_datum = (uint64_t)ALLRINGS;
		break;
	case RM_UT_INVALID_CREDITS_TEST:
		credit->cr_datum = (uint64_t)NENYA;
		break;
	}

	M0_ALLOC_PTR(test_loan);
	M0_UT_ASSERT(test_loan != NULL);

	M0_ALLOC_PTR(remote);
	M0_UT_ASSERT(remote != NULL);
	m0_rm_remote_init(remote, test_data.rd_owner.ro_resource);
	remote->rem_state = REM_OWNER_LOCATED;
	m0_cookie_init(&remote->rem_cookie, &test_data.rd_owner.ro_id);
	m0_remotes_tlist_add(&test_data.rd_res.rs_resource.r_remote, remote);

	m0_rm_loan_init(test_loan, credit, remote);
	test_loan->rl_id = M0_RM_LOAN_SELF_ID + test;
	m0_cookie_init(&test_loan->rl_cookie, &test_loan->rl_id);

	m0_rm_owner_lock(&test_data.rd_owner);
	m0_rm_ur_tlist_add(&test_data.rd_owner.ro_borrowed,
			   &test_loan->rl_credit);
	m0_rm_ur_tlist_add(&test_data.rd_owner.ro_owned[OWOS_CACHED], credit);

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(
			     &test_data.rd_owner.ro_owned[OWOS_CACHED]));
	m0_rm_owner_unlock(&test_data.rd_owner);
}

/*
 * Populate the fake (test) RM-REVOKE FOP.
 */
static void rvk_fop_populate(struct m0_fom *fom)
{
	struct m0_rm_fop_revoke *rvk_fop;
	struct m0_rm_credit	 credit;

	rvk_fop = m0_fop_data(fom->fo_fop);
	M0_UT_ASSERT(rvk_fop != NULL);

	rvk_fop->rr_base.rrq_policy = RIP_NONE;
	rvk_fop->rr_base.rrq_flags = RIF_LOCAL_WAIT;

	m0_rm_credit_init(&credit, &test_data.rd_owner);
	credit.cr_datum = VILYA;
	m0_rm_credit_encode(&credit, &rvk_fop->rr_base.rrq_credit.cr_opaque);

	m0_cookie_init(&rvk_fop->rr_loan.lo_cookie, &test_loan->rl_id);
	m0_cookie_init(&rvk_fop->rr_base.rrq_owner.ow_cookie,
		       &test_data.rd_owner.ro_id);
	m0_rm_credit_fini(&credit);
}

static void rvk_test_cleanup(void)
{
	struct m0_rm_credit *credit;
	struct m0_rm_remote *remote;
	struct m0_rm_loan   *loan;

	m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_owned[OWOS_CACHED], credit) {
		m0_rm_ur_tlink_del_fini(credit);
		m0_rm_credit_fini(credit);
		m0_free(credit);
	} m0_tl_endfor;

	m0_tl_for(m0_rm_ur, &test_data.rd_owner.ro_borrowed, credit) {
		m0_rm_ur_tlink_del_fini(credit);
		loan = container_of(credit, struct m0_rm_loan, rl_credit);
		remote = loan->rl_other;
		m0_rm_loan_fini(loan);
		m0_free(loan);
	} m0_tl_endfor;

	m0_tl_for(m0_remotes, &test_data.rd_res.rs_resource.r_remote, remote) {
		m0_remotes_tlist_del(remote);
		m0_rm_remote_fini(remote);
		m0_free(remote);
	} m0_tl_endfor;
}

/*
 * Validate the test results.
 */
static void rvk_fom_state_validate(struct m0_fom *fom, int32_t rc,
				   enum test_type test)
{
	struct m0_rm_fop_revoke *rvk_fop;

	m0_rm_owner_lock(&test_data.rd_owner);
	switch (test) {
	case RM_UT_FULL_CREDITS_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_SUCCESS);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		M0_UT_ASSERT(
			m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		M0_UT_ASSERT(
		    m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
		break;
	case RM_UT_PARTIAL_CREDITS_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_SUCCESS);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		M0_UT_ASSERT(
			!m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		M0_UT_ASSERT(
			!m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_borrowed));
		break;
	case RM_UT_INVALID_CREDITS_TEST:
	case RM_UT_MEMFAIL_TEST:
		M0_UT_ASSERT(m0_fom_phase(fom) == M0_FOPH_FAILURE);
		M0_UT_ASSERT(rc == M0_FSO_AGAIN);
		M0_UT_ASSERT(
			!m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_owned[OWOS_CACHED]));
		M0_UT_ASSERT(
			!m0_rm_ur_tlist_is_empty(
				&test_data.rd_owner.ro_borrowed));
		break;
	}
	rvk_test_cleanup();
	m0_rm_owner_unlock(&test_data.rd_owner);
	rvk_fop = m0_fop_data(fom->fo_fop);
	M0_UT_ASSERT(rvk_fop != NULL);
	m0_buf_free(&rvk_fop->rr_base.rrq_credit.cr_opaque);
}

/*
 * Test function for to test REVOKE FOM states().
 */
static void rvk_fom_state_test(enum test_type test)
{
	struct m0_fom *fom;
	struct m0_fop *fop;
	int	       rc;

	/* Initialise hierarchy of RM objects */
	rm_utdata_init(&test_data, OBJ_OWNER);

	rvk_data_setup(test);

	fop = fop_alloc(M0_RIT_REVOKE);

	fom_create(M0_RIT_REVOKE, false, fop, &fom);
	M0_UT_ASSERT(fom != NULL);

	rvk_fop_populate(fom);

	m0_ut_fom_phase_set(fom, FOPH_RM_REQ_START);
	/*
	 * Call the first FOM phase.
	 */
	if (test == RM_UT_MEMFAIL_TEST)
		m0_fi_enable_once("rings_credit_copy", "fail_copy");
	rc = revoke_fom_tick(fom);
	M0_UT_ASSERT(m0_fom_phase(fom) == FOPH_RM_REQ_FINISH);
	M0_UT_ASSERT(rc == M0_FSO_AGAIN);

	/*
	 * Call the second FOM phase.
	 */
	rc = revoke_fom_tick(fom);
	rvk_fom_state_validate(fom, rc, test);

	fom_fini(fom, M0_RIT_REVOKE);
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

/*
 * Test function for rvk_fom_create().
 */
static void rvk_fom_create_test(void)
{
	/* 1. Test memory failure */
	fom_create_test(M0_RIT_REVOKE, true);

	/* 2. Test success */
	fom_create_test(M0_RIT_REVOKE, false);
}

/*
 * Test function for rvk_fom_fini().
 */
static void rvk_fom_fini_test(void)
{
	fom_fini_test(M0_RIT_REVOKE);
}

/*
 *****************
 * Top level test functions
 ******************
 */
static void borrow_fom_funcs_test(void)
{
	/* Test for brw_fom_create() */
	brw_fom_create_test();

	/* Test for brw_fom_fini() */
	brw_fom_fini_test();

	/* 1. Test borrowing part (partial) of the credits available */
	brw_fom_state_test(RM_UT_PARTIAL_CREDITS_TEST);

	/* 2. Test borrowing full credits available */
	brw_fom_state_test(RM_UT_FULL_CREDITS_TEST);
	/*
	 * 3. Test borrowing an invalid credit (not available)
	 *    Tests failure in request pre-processing.
	 */
	brw_fom_state_test(RM_UT_INVALID_CREDITS_TEST);

	/* 4. Test failure in Borrow (post-processing) */
	brw_fom_state_test(RM_UT_MEMFAIL_TEST);

}

static void revoke_fom_funcs_test(void)
{
	/* Test for rvk_fom_create() */
	rvk_fom_create_test();

	/* Test for rvk_fom_fini() */
	rvk_fom_fini_test();

	/* 1. Test revoke of entire credits that were borrowed */
	rvk_fom_state_test(RM_UT_FULL_CREDITS_TEST);

	/* 2. Test revoke of part of credits that were borrowed */
	rvk_fom_state_test(RM_UT_PARTIAL_CREDITS_TEST);

	/* 3. Test revoke of an invalid credit (failure in pre-processing) */
	rvk_fom_state_test(RM_UT_INVALID_CREDITS_TEST);

	/* 4. Test revoke post-processing failure */
	rvk_fom_state_test(RM_UT_MEMFAIL_TEST);
	rvk_fom_state_test(RM_UT_PARTIAL_CREDITS_TEST);

}

void rm_fom_funcs_test(void)
{
	rmfoms_utinit();
	m0_sm_group_lock(&dummy_loc.fl_group);
	borrow_fom_funcs_test();
	revoke_fom_funcs_test();
	m0_sm_group_unlock(&dummy_loc.fl_group);
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
