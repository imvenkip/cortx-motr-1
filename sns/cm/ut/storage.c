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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/22/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/misc.h"
#include "reqh/reqh.h"
#include "mero/setup.h"
#include "net/net.h"
#include "sns/cm/ut/cp_common.h"

struct m0_reqh_service          *service;
static struct m0_reqh           *reqh;
static struct m0_semaphore       sem;
static struct m0_stob           *stob;
static struct m0_dtx             tx;
static struct m0_net_buffer_pool nbp;

/* Global structures for write copy packet. */
static struct m0_sns_cm_ag  w_sag;
static struct m0_sns_cm_cp  w_sns_cp;
static struct m0_net_buffer w_buf;

/* Global structures for read copy packet. */
static struct m0_sns_cm_ag  r_sag;
static struct m0_sns_cm_cp  r_sns_cp;
static struct m0_net_buffer r_buf;

static struct m0_stob_id sid = {
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
	SEG_SIZE = 4096,
};

/* Over-ridden copy packet FOM fini. */
static void dummy_fom_fini(struct m0_fom *fom)
{
	m0_cm_cp_fini(container_of(fom, struct m0_cm_cp, c_fom));
}

/* Over-ridden copy packet FOM locality (using single locality). */
static uint64_t dummy_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

/*
 * Over-ridden copy packet FOM tick. This is not taken from production code
 * to keep things simple.
 */
static int dummy_fom_tick(struct m0_fom *fom)
{
	struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);
	return cp->c_ops->co_action[m0_fom_phase(fom)](cp);
}

static void dummy_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
        /**
         * @todo: Do the actual impl, need to set MAGIC, so that
         * m0_fom_init() can pass
         */
        fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

/* Over-ridden copy packet FOM ops. */
static struct m0_fom_ops dummy_cp_fom_ops = {
	.fo_fini          = dummy_fom_fini,
	.fo_tick          = dummy_fom_tick,
	.fo_home_locality = dummy_fom_locality,
	.fo_addb_init     = dummy_addb_init
};

/* Over-ridden copy packet init phase. */
static int dummy_cp_init(struct m0_cm_cp *cp)
{
	/* This is used to ensure that ast has been posted. */
	m0_semaphore_up(&sem);
	return cp->c_ops->co_phase_next(cp);
}

/*
 * Over-ridden copy packet read phase. This is used when write operation of
 * copy packet has to be tested. In this case, read phase will simply be a
 * passthrough phase.
 */
static int dummy_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	cp->c_ops->co_phase_next(cp);
	return M0_FSO_AGAIN;
}

/*
 * Over-ridden copy packet write phase. This is used when read operation of
 * copy packet has to be tested. In this case, write phase will simply be a
 * passthrough phase.
 */
static int dummy_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	cp->c_ops->co_phase_next(cp);
	return M0_FSO_AGAIN;
}

/* Passthorugh phase for testing purpose. */
static int dummy_cp_xform(struct m0_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

/* Passthorugh for testing purpose. */
static void dummy_cp_complete(struct m0_cm_cp *cp)
{
}

/* Passthorugh for testing purpose. */
static int dummy_cp_fini(struct m0_cm_cp *cp)
{
	return cp->c_ops->co_phase_next(cp);
}

/*
 * Over-ridden copy packet read io wait phase. This is used when write operation
 * of copy packet has to be tested. In this case, read io wait phase will
 * simply be a passthrough phase.
 */
static int dummy_cp_read_io_wait(struct m0_cm_cp *cp)
{
	return cp->c_io_op == M0_CM_CP_READ ?
	       cp->c_ops->co_phase_next(cp) :
	       m0_sns_cm_cp_io_wait(cp);
}

/*
 * Over-ridden copy packet write io wait phase. This is used when read operation
 * of copy packet has to be tested. In this case, write io wait phase will
 * simply be a passthrough phase.
 */
static int dummy_cp_write_io_wait(struct m0_cm_cp *cp)
{
	return cp->c_io_op == M0_CM_CP_WRITE ?
	       cp->c_ops->co_phase_next(cp) :
	       m0_sns_cm_cp_io_wait(cp);
}

const struct m0_cm_cp_ops write_cp_dummy_ops = {
	.co_action = {
		[M0_CCP_INIT]       = &dummy_cp_init,
		[M0_CCP_READ]       = &dummy_cp_read,
		[M0_CCP_WRITE]      = &m0_sns_cm_cp_write,
		[M0_CCP_IO_WAIT]    = &dummy_cp_read_io_wait,
		[M0_CCP_XFORM]      = &dummy_cp_xform,
                [M0_CCP_SEND]       = &m0_sns_cm_cp_send,
                [M0_CCP_RECV]       = &m0_sns_cm_cp_recv,
		[M0_CCP_FINI]       = &dummy_cp_fini,
	},
	.co_action_nr               = M0_CCP_NR,
	.co_phase_next              = &m0_sns_cm_cp_phase_next,
	.co_invariant               = &m0_sns_cm_cp_invariant,
	.co_complete                = &dummy_cp_complete,
};

void write_post(void)
{
	int                           rc;
	struct m0_stob_domain        *sdom;
	struct m0_sns_cm_ag_tgt_addr  tgt_addr;

	m0_semaphore_init(&sem, 0);
	w_buf.nb_pool = &nbp;
	cp_prepare(&w_sns_cp.sc_base, &w_buf, SEG_NR, SEG_SIZE,
		   &w_sag, 'e', &dummy_cp_fom_ops, reqh, 0, false);
	w_sns_cp.sc_base.c_ops = &write_cp_dummy_ops;
	w_sns_cp.sc_sid = sid;
	tgt_addr.tgt_cobfid.f_container = sid.si_bits.u_hi;
	tgt_addr.tgt_cobfid.f_key = sid.si_bits.u_lo;
	tgt_addr.tgt_cob_index = 0;
	w_sag.sag_tgts = &tgt_addr;
	w_sag.sag_base.cag_cp_local_nr = 1;
	w_sag.sag_fnr = 1;

	sdom = m0_cs_stob_domain_find(reqh, &sid);
	M0_UT_ASSERT(sdom != NULL);

	m0_dtx_init(&tx);
	rc = sdom->sd_ops->sdo_tx_make(sdom, &tx);
	M0_ASSERT(rc == 0);
	/*
	 * Create a stob. In actual repair scenario, this will already be
	 * created by the IO path.
	 */
	rc = m0_stob_create_helper(sdom, &tx, &sid, &stob);
	M0_UT_ASSERT(rc == 0);

	m0_stob_put(stob);
	m0_dtx_done(&tx);

	m0_fom_queue(&w_sns_cp.sc_base.c_fom, reqh);

	/* Wait till ast gets posted. */
	m0_semaphore_down(&sem);

        /*
	 * Wait until all the foms in the request handler locality runq are
	 * processed. This is required for further validity checks.
	 */
	m0_reqh_fom_domain_idle_wait(reqh);
}

const struct m0_cm_cp_ops read_cp_dummy_ops = {
	.co_action = {
		[M0_CCP_INIT]       = &dummy_cp_init,
		[M0_CCP_READ]       = &m0_sns_cm_cp_read,
		[M0_CCP_WRITE]      = &dummy_cp_write,
		[M0_CCP_IO_WAIT]    = &dummy_cp_write_io_wait,
		[M0_CCP_XFORM]      = &dummy_cp_xform,
                [M0_CCP_SEND]       = &m0_sns_cm_cp_send,
                [M0_CCP_RECV]       = &m0_sns_cm_cp_recv,
		[M0_CCP_FINI]       = &dummy_cp_fini,
	},
	.co_action_nr               = M0_CCP_NR,
	.co_phase_next              = &m0_sns_cm_cp_phase_next,
	.co_invariant               = &m0_sns_cm_cp_invariant,
	.co_complete                = &dummy_cp_complete,
};

static void read_post(void)
{
	m0_semaphore_init(&sem, 0);

	r_buf.nb_pool = &nbp;
	/*
	 * Purposefully fill the read bv with spaces i.e. ' '. This should get
	 * replaced by 'e', when the data is read. This is due to the fact
	 * that write operation is writing 'e' to the bv.
	 */
	cp_prepare(&r_sns_cp.sc_base, &r_buf, SEG_NR, SEG_SIZE,
		   &r_sag, ' ', &dummy_cp_fom_ops, reqh, 0, false);
	r_sns_cp.sc_base.c_ops = &read_cp_dummy_ops;
	r_sag.sag_base.cag_cp_local_nr = 1;
	r_sag.sag_fnr = 1;
	r_sns_cp.sc_sid = sid;
	m0_fom_queue(&r_sns_cp.sc_base.c_fom, reqh);

        /* Wait till ast gets posted. */
	m0_semaphore_down(&sem);

        /*
         * Wait until all the foms in the request handler locality runq are
         * processed. This is required for further validity checks.
         */
	m0_reqh_fom_domain_idle_wait(reqh);
}

static void test_cp_write_read(void)
{
	int                    rc;

	rc = sns_cm_ut_server_start();
	M0_ASSERT(rc == 0);

	reqh = m0_cs_reqh_get(&sctx, "sns_cm");
	M0_UT_ASSERT(reqh != NULL);

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
	bv_compare(&r_buf.nb_buffer, &w_buf.nb_buffer, SEG_NR, SEG_SIZE);

	bv_free(&r_buf.nb_buffer);
	bv_free(&w_buf.nb_buffer);
	sns_cm_ut_server_stop();
}

const struct m0_test_suite snscm_storage_ut = {
	.ts_name = "snscm_storage-ut",
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
