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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/22/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/misc.h"
#include "reqh/reqh.h"
#include "net/lnet/lnet.h"
#include "colibri/colibri_setup.h"
#include "sns/repair/ut/cp_common.h"

/* Global structures for setting up colibri service. */
static const char log_file_name[] = "sr_ut.errlog";
static char *sns_repair_ut_svc[] = { "colibri_setup", "-r", "-p", "-T", "LINUX",
				     "-D", "sr_db", "-S", "sr_stob",
				     "-e", "lnet:0@lo:12345:34:1" ,
				     "-s", "sns_repair"};

static struct c2_net_xprt *sr_xprts[] = {
	&c2_net_lnet_xprt,
};

static struct c2_colibri        sctx;
struct c2_reqh_service         *service;
static FILE                    *lfile;
struct c2_reqh_service         *service;
static struct c2_reqh          *reqh;
static struct c2_semaphore      sem;
static struct c2_stob          *stob;
static struct c2_dtx            tx;

/* Global structures for write copy packet. */
static struct c2_sns_repair_ag w_sag;
static struct c2_sns_repair_cp w_sns_cp;
static struct c2_bufvec        w_bv;

/* Global structures for read copy packet. */
static struct c2_sns_repair_ag r_sag;
static struct c2_sns_repair_cp r_sns_cp;
static struct c2_bufvec        r_bv;

static struct c2_stob_id sid = {
	.si_bits = {
		.u_hi = 1,
		.u_lo = 1
	}
};

/*
 * Copy packet will typically have a single segment with its size equal to
 * size of copy packet (unit).
 */
enum {
	SEG_NR = 1,
	SEG_SIZE = C2_CP_SIZE,
};

/*
 * Starts colibri service, which internally creates and sets up stob domain.
 * This stob domain is used in read and write phases of the copy packet.
 */
static int cs_init(void)
{
	int rc;

	C2_SET0(&sctx);

	lfile = fopen(log_file_name, "w+");
	C2_UT_ASSERT(lfile != NULL);

	rc = c2_cs_init(&sctx, sr_xprts, ARRAY_SIZE(sr_xprts), lfile);
	C2_UT_ASSERT(rc == 0);

	rc = c2_cs_setup_env(&sctx, ARRAY_SIZE(sns_repair_ut_svc),
			     sns_repair_ut_svc);
	C2_UT_ASSERT(rc == 0);

	rc = c2_cs_start(&sctx);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

/* Finalises the colibri service. */
static void cs_fini(void)
{
	c2_cs_fini(&sctx);
	fclose(lfile);
}

/* Over-ridden copy packet FOM fini. */
static void dummy_fom_fini(struct c2_fom *fom)
{
	c2_cm_cp_fini(container_of(fom, struct c2_cm_cp, c_fom));
}

/* Over-ridden copy packet FOM locality (using single locality). */
static uint64_t dummy_fom_locality(const struct c2_fom *fom)
{
	return 0;
}

/*
 * Over-ridden copy packet FOM tick. This is not taken from production code
 * to keep things simple.
 */
static int dummy_fom_tick(struct c2_fom *fom)
{
	struct c2_cm_cp *cp = container_of(fom, struct c2_cm_cp, c_fom);
	return cp->c_ops->co_action[c2_fom_phase(fom)](cp);
}

/* Over-ridden copy packet FOM ops. */
static struct c2_fom_ops dummy_cp_fom_ops = {
	.fo_fini          = dummy_fom_fini,
	.fo_tick          = dummy_fom_tick,
	.fo_home_locality = dummy_fom_locality
};

/* Over-ridden copy packet init phase. */
static int dummy_cp_init(struct c2_cm_cp *cp)
{
	/* This is used to ensure that ast has been posted. */
	c2_semaphore_up(&sem);
	return cp->c_ops->co_phase_next(cp);
}

/*
 * Over-ridden copy packet read phase. This is used when write operation of
 * copy packet has to be tested. In this case, read phase will simply be a
 * passthrough phase.
 */
static int dummy_cp_read(struct c2_cm_cp *cp)
{
	cp->c_io_op = C2_CM_CP_READ;
	return cp->c_ops->co_phase_next(cp);
}

/*
 * Over-ridden copy packet write phase. This is used when read operation of
 * copy packet has to be tested. In this case, write phase will simply be a
 * passthrough phase.
 */
static int dummy_cp_write(struct c2_cm_cp *cp)
{
	cp->c_io_op = C2_CM_CP_WRITE;
	return cp->c_ops->co_phase_next(cp);
}

/* Passthorugh phase for testing purpose. */
static int dummy_cp_xform(struct c2_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

/* Passthorugh for testing purpose. */
static void dummy_cp_complete(struct c2_cm_cp *cp)
{
}

/* Passthorugh for testing purpose. */
static int dummy_cp_fini(struct c2_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

/*
 * Over-ridden copy packet read io wait phase. This is used when write operation
 * of copy packet has to be tested. In this case, read io wait phase will
 * simply be a passthrough phase.
 */
static int dummy_cp_read_io_wait(struct c2_cm_cp *cp)
{
	return cp->c_io_op == C2_CM_CP_READ ?
	       cp->c_ops->co_phase_next(cp) :
	       c2_sns_repair_cp_io_wait(cp);
}

/*
 * Over-ridden copy packet write io wait phase. This is used when read operation
 * of copy packet has to be tested. In this case, write io wait phase will
 * simply be a passthrough phase.
 */
static int dummy_cp_write_io_wait(struct c2_cm_cp *cp)
{
	return cp->c_io_op == C2_CM_CP_WRITE ?
	       cp->c_ops->co_phase_next(cp) :
	       c2_sns_repair_cp_io_wait(cp);
}

const struct c2_cm_cp_ops write_cp_dummy_ops = {
	.co_action = {
		[C2_CCP_INIT]    = &dummy_cp_init,
		[C2_CCP_READ]    = &dummy_cp_read,
		[C2_CCP_WRITE]   = &c2_sns_repair_cp_write,
		[C2_CCP_IO_WAIT] = &dummy_cp_read_io_wait,
		[C2_CCP_XFORM]   = &dummy_cp_xform,
		[C2_CCP_FINI]    = &dummy_cp_fini,
	},
	.co_action_nr            = C2_CCP_NR,
	.co_phase_next           = &c2_sns_repair_cp_phase_next,
	.co_complete             = &dummy_cp_complete,
};

void write_post(void)
{
	int                    rc;
	struct c2_stob_domain *sdom;

	c2_semaphore_init(&sem, 0);
	cp_prepare(&w_sns_cp.rc_base, &w_bv, SEG_NR, SEG_SIZE, &w_sag, 'e',
		   &dummy_cp_fom_ops);
	w_sns_cp.rc_sid = sid;
	w_sag.sag_spare_cobfid.f_container = sid.si_bits.u_hi;
	w_sag.sag_spare_cobfid.f_key = sid.si_bits.u_lo;
	w_sag.sag_spare_cob_index = 0;
	w_sns_cp.rc_base.c_ops = &write_cp_dummy_ops;

	sdom = c2_cs_stob_domain_find(reqh, &sid);
	C2_UT_ASSERT(sdom != NULL);

	c2_dtx_init(&tx);
	rc = sdom->sd_ops->sdo_tx_make(sdom, &tx);
	C2_ASSERT(rc == 0);

	/*
	 * Create a stob. In actual repair scenario, this will already be
	 * created by the IO path.
	 */
	rc = c2_stob_create_helper(sdom, &tx, &sid, &stob);
	C2_UT_ASSERT(rc == 0);

	c2_stob_put(stob);
	c2_dtx_done(&tx);

	c2_fom_queue(&w_sns_cp.rc_base.c_fom, reqh);

	/* Wait till ast gets posted. */
	c2_semaphore_down(&sem);

        /*
	 * Wait until all the foms in the request handler locality runq are
	 * processed. This is required for further validity checks.
	 */
	c2_reqh_shutdown_wait(reqh);
}

const struct c2_cm_cp_ops read_cp_dummy_ops = {
	.co_action = {
		[C2_CCP_INIT]    = &dummy_cp_init,
		[C2_CCP_READ]    = &c2_sns_repair_cp_read,
		[C2_CCP_WRITE]   = &dummy_cp_write,
		[C2_CCP_IO_WAIT] = &dummy_cp_write_io_wait,
		[C2_CCP_XFORM]   = &dummy_cp_xform,
		[C2_CCP_FINI]    = &dummy_cp_fini,
	},
	.co_action_nr            = C2_CCP_NR,
	.co_phase_next           = &c2_sns_repair_cp_phase_next,
	.co_complete             = &dummy_cp_complete,
};

static void read_post(void)
{
	c2_semaphore_init(&sem, 0);

	/*
	 * Purposefully fill the read bv with spaces i.e. ' '. This should get
	 * replaced by 'e', when the data is read. This is due to the fact
	 * that write operation is writing 'e' to the bv.
	 */
	cp_prepare(&r_sns_cp.rc_base, &r_bv, SEG_NR, SEG_SIZE, &r_sag, ' ',
		   &dummy_cp_fom_ops);
	r_sns_cp.rc_sid = sid;
	r_sns_cp.rc_base.c_ops = &read_cp_dummy_ops;

	c2_fom_queue(&r_sns_cp.rc_base.c_fom, reqh);

        /* Wait till ast gets posted. */
	c2_semaphore_down(&sem);

        /*
         * Wait until all the foms in the request handler locality runq are
         * processed. This is required for further validity checks.
         */
	c2_reqh_shutdown_wait(reqh);
}

static void test_cp_write_read(void)
{
	int rc;

	rc = cs_init();
	C2_ASSERT(rc == 0);

	reqh = c2_cs_reqh_get(&sctx, "sns_repair");
	C2_UT_ASSERT(reqh != NULL);

	/*
	 * Write using a dummy copy packet. This data which is written, will
	 * be used by the next copy packet to read.
	 */
	write_post();

	/* Read the previously written bv. */
	read_post();

	/*
	 * Compare the bv that is read with the previously written bv.
	 * This verifies the correctness of both write and read operation.
	 */
	bv_compare(&r_bv, &w_bv, SEG_NR, SEG_SIZE);

	bv_free(&r_bv);
	bv_free(&w_bv);
	cs_fini();
}

const struct c2_test_suite snsrepair_storage_ut = {
	.ts_name = "snsrepair_storage-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "cp_write_read", test_cp_write_read },
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
