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
 * Original creation date: 03/26/2013
 */

#include "lib/misc.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh.h"
#include "sns/cm/net.c"
#include "sns/cm/ag.c"
#include "sns/cm/ut/cp_common.h"
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "reqh/reqh_service.h"
#include "cm/proxy.h"
#include "ut/ut_rpc_machine.h"

#define DUMMY_DBNAME      "dummy-db"
#define DUMMY_COB_ID      20
#define DUMMY_SERVER_ADDR "0@lo:12345:34:10"

/* Receiver side. */
static struct m0_reqh   *s0_reqh;
struct m0_cm            *cm;
struct m0_sns_cm        *scm;
struct m0_reqh_service  *scm_service;
struct m0_cm_aggr_group *ag_cpy;
struct m0_sns_cm_ag      sns_ag;
struct m0_stob_domain   *sdom;
static struct m0_dtx     tx;
static struct m0_stob   *stob;

/*
 * Global structures for read copy packet used for verification.
 * (Receiver side).
 */
static struct m0_sns_cm_ag        r_sag;
static struct m0_sns_cm_cp        r_sns_cp;
static struct m0_net_buffer       r_buf;
static struct m0_net_buffer_pool  r_nbp;

/* Sender side. */
enum {
        CLIENT_COB_DOM_ID  = 44,
        SESSION_SLOTS      = 2,
        MAX_RPCS_IN_FLIGHT = 5,
        STOB_UPDATE_DELAY  = 1,
        MAX_RETRIES        = 5,
        CP_SINGLE          = 1,
        FAIL_NR            = 1,
        BUF_NR             = 4,
        SEG_NR             = 256,
        SEG_SIZE           = 4096,
        START_DATA         = 101,
};

static struct m0_net_domain  client_net_dom;
static struct m0_dbenv       client_dbenv;
static struct m0_cob_domain  client_cob_dom;
static struct m0_net_xprt   *xprt = &m0_net_lnet_xprt;
static struct m0_semaphore   sem;
static struct m0_semaphore   cp_sem;
static struct m0_semaphore   read_cp_sem;

const char client_addr[]    = "0@lo:12345:34:2";
const char server_addr[]    = "0@lo:12345:34:1";
const char client_db_name[] = "cm_client_db";
static const char send_db[] = "send-db";

static struct m0_rpc_client_ctx cctx = {
        .rcx_net_dom            = &client_net_dom,
        .rcx_local_addr         = client_addr,
        .rcx_remote_addr        = server_addr,
        .rcx_db_name            = client_db_name,
        .rcx_dbenv              = &client_dbenv,
        .rcx_cob_dom_id         = CLIENT_COB_DOM_ID,
        .rcx_cob_dom            = &client_cob_dom,
        .rcx_nr_slots           = SESSION_SLOTS,
        .rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
};

static struct m0_ut_rpc_mach_ctx  rmach_ctx;
struct m0_cm                      sender_cm;
struct m0_reqh_service           *sender_cm_service;
extern struct m0_cm_type          sender_cm_cmt;
struct m0_cm_cp                   sender_cm_cp;
struct m0_mero                    sender_mero = { .cc_pool_width = 10 };
struct m0_reqh_context            sender_rctx = { .rc_mero = &sender_mero };

/* Global structures for copy packet to be sent (Sender side). */
static struct m0_sns_cm_ag        s_sag;
static struct m0_sns_cm_cp        s_sns_cp;
static struct m0_net_buffer       s_buf[BUF_NR];
static struct m0_net_buffer_pool  nbp;
static struct m0_cm_proxy         sender_cm_proxy;
static struct m0_cm_proxy         recv_cm_proxy;

static struct m0_stob_id sid = {
        .si_bits = {
                .u_hi = 0,
                .u_lo = 4
        }
};

static struct m0_cm_ag_id ag_id = {
        .ai_hi = {
                .u_hi = 1,
                .u_lo = 1
        },
        .ai_lo = {
                .u_hi = 0,
                .u_lo = 1
        }
};

static void cp_cm_proxy_init(struct m0_cm_proxy *proxy, const char *endpoint);
M0_INTERNAL void cob_create(struct m0_dbenv *dbenv, struct m0_cob_domain *cdom,
			    uint64_t cont, struct m0_fid *gfid, uint32_t cob_idx);
static uint64_t cp_single_get(const struct m0_cm_aggr_group *ag)
{
        return CP_SINGLE;
}

static void cp_ag_fini(struct m0_cm_aggr_group *ag)
{
        M0_PRE(ag != NULL);

        m0_cm_aggr_group_fini(ag);
}

static bool cp_ag_can_fini(struct m0_cm_aggr_group *ag, struct m0_cm_cp *cp)
{
        struct m0_sns_cm_ag *sag = ag2snsag(ag);
        uint64_t             nr_cps;

        M0_PRE(ag != NULL && cp != NULL);

        if (ag->cag_has_incoming) {
                nr_cps = m0_cm_cp_nr(cp);
                return nr_cps == ag->cag_cp_global_nr - sag->sag_fnr;
        } else
                return ag->cag_freed_cp_nr == ag->cag_cp_local_nr + sag->sag_fnr;
}

static const struct m0_cm_aggr_group_ops group_ops = {
        .cago_local_cp_nr = &cp_single_get,
        .cago_fini        = &cp_ag_fini,
	.cago_ag_can_fini = &cp_ag_can_fini,
};

/* Over-ridden copy packet FOM fini. */
static void dummy_fom_fini(struct m0_fom *fom)
{
        struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);
        m0_cm_cp_fini(cp);
        m0_semaphore_up(&cp_sem);
}

/* Over-ridden copy packet FOM locality (using single locality). */
static uint64_t dummy_fom_locality(const struct m0_fom *fom)
{
        return 0;
}

/* Over-ridden copy packet FOM tick. */
static int dummy_fom_tick(struct m0_fom *fom)
{
        struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);

        switch (m0_fom_phase(fom)) {
        case M0_FOM_PHASE_INIT:
                m0_fom_phase_set(fom, M0_CCP_XFORM);
                m0_semaphore_up(&sem);
                return cp->c_ops->co_action[M0_CCP_XFORM](cp);
        case M0_CCP_XFORM:
                m0_fom_phase_set(fom, M0_CCP_SEND);
                return cp->c_ops->co_action[M0_CCP_SEND](cp);
        case M0_CCP_SEND:
                m0_fom_phase_set(fom, M0_CCP_SEND_WAIT);
                return cp->c_ops->co_action[M0_CCP_SEND_WAIT](cp);
        case M0_CCP_SEND_WAIT:
                m0_fom_phase_set(fom, M0_CCP_FINI);
                return M0_FSO_WAIT;
        default:
                M0_IMPOSSIBLE("Bad State");
                return 0;
        }

}

static void dummy_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
        fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

/* Over-ridden copy packet FOM ops. */
static struct m0_fom_ops cp_fom_ops = {
        .fo_fini          = dummy_fom_fini,
        .fo_tick          = dummy_fom_tick,
        .fo_home_locality = dummy_fom_locality,
        .fo_addb_init     = dummy_fom_addb_init
};

/* Over-ridden copy packet init phase. */
static int dummy_cp_init(struct m0_cm_cp *cp)
{
        /* This is used to ensure that ast has been posted. */
        m0_semaphore_up(&sem);
        return M0_FSO_AGAIN;
}

/* Passthorugh phase for testing purpose. */
static int dummy_cp_phase(struct m0_cm_cp *cp)
{
        return M0_FSO_AGAIN;
}

/* Passthorugh for testing purpose. */
static void dummy_cp_complete(struct m0_cm_cp *cp)
{
}

const struct m0_cm_cp_ops cp_dummy_ops = {
        .co_action = {
                [M0_CCP_INIT]          = &dummy_cp_init,
                [M0_CCP_READ]          = &dummy_cp_phase,
                [M0_CCP_WRITE]         = &dummy_cp_phase,
                [M0_CCP_IO_WAIT]       = &dummy_cp_phase,
                [M0_CCP_XFORM]         = &dummy_cp_phase,
                [M0_CCP_SW_CHECK]      = &dummy_cp_phase,
                [M0_CCP_SEND]          = &m0_sns_cm_cp_send,
                [M0_CCP_SEND_WAIT]     = &m0_sns_cm_cp_send_wait,
                [M0_CCP_RECV_INIT]     = &dummy_cp_phase,
                [M0_CCP_RECV_WAIT]     = &dummy_cp_phase,
                [M0_CCP_FINI]          = &dummy_cp_phase,
        },
        .co_action_nr                  = M0_CCP_NR,
        .co_phase_next                 = &m0_sns_cm_cp_phase_next,
        .co_invariant                  = &m0_sns_cm_cp_invariant,
        .co_complete                   = &dummy_cp_complete,
};

/* Over-ridden read copy packet FOM tick. */
static int dummy_read_fom_tick(struct m0_fom *fom)
{
        struct m0_cm_cp *cp = container_of(fom, struct m0_cm_cp, c_fom);
        return cp->c_ops->co_action[m0_fom_phase(fom)](cp);
}

/* Over-ridden read copy packet FOM fini. */
static void dummy_read_fom_fini(struct m0_fom *fom)
{
        m0_cm_cp_fini(container_of(fom, struct m0_cm_cp, c_fom));
        m0_semaphore_up(&read_cp_sem);
}

/* Over-ridden read copy packet FOM ops. */
static struct m0_fom_ops read_cp_fom_ops = {
        .fo_fini          = dummy_read_fom_fini,
        .fo_tick          = dummy_read_fom_tick,
        .fo_home_locality = dummy_fom_locality,
        .fo_addb_init     = dummy_fom_addb_init
};

/* Over-ridden copy packet init phase for read copy packet. */
static int dummy_read_cp_init(struct m0_cm_cp *cp)
{
        /* This is used to ensure that ast has been posted. */
        m0_semaphore_up(&sem);
        return cp->c_ops->co_phase_next(cp);
}

/* Passthorugh phase for testing purpose. */
static int dummy_read_cp_phase(struct m0_cm_cp *cp)
{
        return cp->c_ops->co_phase_next(cp);
}

static void buffers_verify()
{
        int  i;
        int  j;
        int  rc;
        int  cnt = 0;
        char str[SEG_SIZE];

        for (i = 0; i < BUF_NR; ++i) {
                for (j = 0; j < SEG_NR; ++j) {
                        memset(str, (START_DATA + i), SEG_SIZE);
                        rc = memcmp(r_buf.nb_buffer.ov_buf[cnt], str, SEG_SIZE);
                        M0_UT_ASSERT(rc == 0);
                        cnt++;
            }
        }
}

/* Passthorugh phase for testing purpose. */
static int dummy_read_cp_xform(struct m0_cm_cp *cp)
{
        buffers_verify();
        return cp->c_ops->co_phase_next(cp);
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

const struct m0_cm_cp_ops read_cp_ops = {
        .co_action = {
                [M0_CCP_INIT]          = &dummy_read_cp_init,
                [M0_CCP_READ]          = &m0_sns_cm_cp_read,
                [M0_CCP_WRITE]         = &dummy_cp_write,
                [M0_CCP_IO_WAIT]       = &dummy_cp_write_io_wait,
                [M0_CCP_XFORM]         = &dummy_read_cp_xform,
		[M0_CCP_SW_CHECK]      = &dummy_read_cp_phase,
                [M0_CCP_SEND]          = &dummy_read_cp_phase,
                [M0_CCP_SEND_WAIT]     = &dummy_read_cp_phase,
                [M0_CCP_RECV_INIT]     = &dummy_read_cp_phase,
                [M0_CCP_RECV_WAIT]     = &dummy_read_cp_phase,
                [M0_CCP_FINI]          = &dummy_read_cp_phase,
        },
        .co_action_nr                  = M0_CCP_NR,
        .co_phase_next                 = &m0_sns_cm_cp_phase_next,
        .co_invariant                  = &m0_sns_cm_cp_invariant,
        .co_complete                   = &dummy_cp_complete,
};

static void ag_setup(struct m0_sns_cm_ag *sag)
{
        sag->sag_base.cag_transformed_cp_nr = 0;
        sag->sag_fnr = FAIL_NR;
        sag->sag_base.cag_ops = &group_ops;
        sag->sag_base.cag_id = ag_id;
        sag->sag_base.cag_cp_local_nr =
                      sag->sag_base.cag_ops->cago_local_cp_nr(&sag->sag_base);
        sag->sag_base.cag_cp_global_nr = sag->sag_base.cag_cp_local_nr +
                                          FAIL_NR;
	aggr_grps_in_tlink_init(&sag->sag_base);
        aggr_grps_out_tlink_init(&sag->sag_base);
}

/*
 * Read the copy packet which has completed its fom cycle and ended up
 * writing the data which was sent onwire. After reading, verify the
 * data for correctness.
 */
static void read_and_verify()
{
        char data;

        m0_semaphore_init(&read_cp_sem, 0);

        ag_setup(&r_sag);

        r_buf.nb_pool = &r_nbp;
        /*
         * Purposefully fill the read bv with spaces i.e. ' '. This should get
         * replaced by appropriate data, when the data is read.
         */
        data = ' ';
        cp_prepare(&r_sns_cp.sc_base, &r_buf, SEG_NR * BUF_NR, SEG_SIZE,
                   &r_sag, data, &read_cp_fom_ops, s0_reqh, 0, false, cm);

        r_sns_cp.sc_sid = sid;
        r_sns_cp.sc_index = 0;
        r_sns_cp.sc_base.c_ops = &read_cp_ops;
        m0_fom_queue(&r_sns_cp.sc_base.c_fom, s0_reqh);

        m0_semaphore_down(&read_cp_sem);
}

/* Create and add the aggregation group to the list in copy machine. */
static void receiver_ag_create()
{
        int                  i;
        struct m0_sns_cm_cp *sns_cp;

        ag_setup(&sns_ag);
        sns_ag.sag_base.cag_cm = cm;
        m0_mutex_init(&sns_ag.sag_base.cag_mutex);
        aggr_grps_in_tlink_init(&sns_ag.sag_base);
        aggr_grps_out_tlink_init(&sns_ag.sag_base);
        M0_ALLOC_ARR(sns_ag.sag_fc, FAIL_NR);
        M0_UT_ASSERT(sns_ag.sag_fc != NULL);
        for (i = 0; i < sns_ag.sag_fnr; ++i) {
		sns_ag.sag_fc[i].fc_tgt_cobfid.f_container = sid.si_bits.u_hi;
		sns_ag.sag_fc[i].fc_tgt_cobfid.f_key = sid.si_bits.u_lo;
                sns_cp = &sns_ag.sag_fc[i].fc_tgt_acc_cp;
                m0_sns_cm_acc_cp_init(sns_cp, &sns_ag);
                sns_cp->sc_base.c_data_seg_nr = SEG_NR * BUF_NR;
                sns_cp->sc_sid = sid;
		m0_cm_lock(cm);
                M0_UT_ASSERT(m0_sns_cm_buf_attach(&scm->sc_obp.sb_bp,
						  &sns_cp->sc_base) == 0);
		m0_cm_unlock(cm);
        }

        m0_cm_lock(cm);
        m0_cm_aggr_group_add(cm, &sns_ag.sag_base, true);
        ag_cpy = m0_cm_aggr_group_locate(cm, &ag_id, true);
        m0_cm_unlock(cm);
        M0_UT_ASSERT(&sns_ag.sag_base == ag_cpy);
}

static void receiver_stob_create()
{
        struct m0_cob_domain *cdom;
        struct m0_dbenv      *dbenv;
	struct m0_fid         gfid = {0, 4};
        int                   rc;

	M0_UT_ASSERT(m0_ios_cdom_get(s0_reqh, &cdom) == 0);
	dbenv = s0_reqh->rh_dbenv;

	cob_create(dbenv, cdom, 0, &gfid, 0);
        sdom = m0_cs_stob_domain_find(s0_reqh, &sid);
        M0_UT_ASSERT(sdom != NULL);

        m0_dtx_init(&tx);
        rc = sdom->sd_ops->sdo_tx_make(sdom, &tx);
        M0_UT_ASSERT(rc == 0);
        /*
         * Create a stob. In actual repair scenario, this will already be
         * created by the IO path.
         */
        rc = m0_stob_create_helper(sdom, &tx, &sid, &stob);
        M0_UT_ASSERT(rc == 0);

        m0_stob_put(stob);
        m0_dtx_done(&tx);
}

static void cm_ready(struct m0_cm *cm)
{
	m0_cm_lock(cm);
	m0_cm_state_set(cm, M0_CMS_READY);
	m0_cm_unlock(cm);
}

static void receiver_init()
{
        M0_UT_ASSERT(cs_init(&sctx) == 0);
        s0_reqh = m0_cs_reqh_get(&sctx, "sns_cm");
        M0_UT_ASSERT(s0_reqh != NULL);

        scm_service = m0_reqh_service_find(m0_reqh_service_type_find("sns_cm"),
                                       s0_reqh);
        M0_UT_ASSERT(scm_service != NULL);
        cm = container_of(scm_service, struct m0_cm, cm_service);
        M0_UT_ASSERT(cm != NULL);
        scm = cm2sns(cm);
        scm->sc_op = SNS_REPAIR;
        M0_ALLOC_ARR(scm->sc_it.si_fdata, FAIL_NR);
        M0_UT_ASSERT(scm->sc_it.si_fdata != NULL);
        scm->sc_it.si_fdata[0] = 1;
	scm->sc_failures_nr = 1;
	cm->cm_pm = m0_ios_poolmach_get(cm->cm_service.rs_reqh);
	M0_UT_ASSERT(cm->cm_pm != NULL);
	m0_cm_lock(cm);
	cm->cm_ops->cmo_ready(cm);
	m0_cm_unlock(cm);
	cm_ready(cm);
        M0_UT_ASSERT(m0_cm_start(cm) == 0);

        while (m0_fom_domain_is_idle(&s0_reqh->rh_fom_dom) ||
                        !m0_cm_cp_pump_is_complete(&cm->cm_cp_pump))
                usleep(200);

        receiver_ag_create();
        receiver_stob_create();
        cp_cm_proxy_init(&recv_cm_proxy, client_addr);
        m0_cm_lock(cm);
        m0_cm_proxy_add(cm, &recv_cm_proxy);
        m0_cm_unlock(cm);
}

static void sender_cm_cp_free(struct m0_cm_cp *cp)
{
}

static bool sender_cm_cp_invariant(const struct m0_cm_cp *cp)
{
        return true;
}

static const struct m0_cm_cp_ops sender_cm_cp_ops = {
        .co_invariant = sender_cm_cp_invariant,
        .co_free = sender_cm_cp_free
};

static struct m0_cm_cp* sender_cm_cp_alloc(struct m0_cm *cm)
{
        sender_cm_cp.c_ops = &sender_cm_cp_ops;
        return &sender_cm_cp;
}

static int sender_cm_setup(struct m0_cm *cm)
{
        return 0;
}

static int sender_cm_start(struct m0_cm *cm)
{
        return 0;
}

static int sender_cm_stop(struct m0_cm *cm)
{
        return 0;
}

static int sender_cm_data_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
        return -ENODATA;
}

static void sender_cm_fini(struct m0_cm *cm)
{
}

static void sender_cm_complete(struct m0_cm *cm)
{
}

static const struct m0_cm_ops sender_cm_ops = {
        .cmo_setup     = sender_cm_setup,
        .cmo_start     = sender_cm_start,
        .cmo_stop      = sender_cm_stop,
        .cmo_cp_alloc  = sender_cm_cp_alloc,
        .cmo_data_next = sender_cm_data_next,
        .cmo_complete  = sender_cm_complete,
        .cmo_fini      = sender_cm_fini
};

static int sender_cm_service_start(struct m0_reqh_service *service)
{
        struct  m0_cm *cm;

        cm = container_of(service, struct m0_cm, cm_service);
        return m0_cm_setup(cm);
}

static void sender_cm_service_stop(struct m0_reqh_service *service)
{
        struct m0_cm *cm = container_of(service, struct m0_cm, cm_service);
        m0_cm_fini(cm);
}

static void sender_cm_service_fini(struct m0_reqh_service *service)
{
        sender_cm_service = NULL;
        M0_SET0(&sender_cm);
}

static const struct m0_reqh_service_ops sender_cm_service_ops = {
        .rso_start = sender_cm_service_start,
        .rso_stop  = sender_cm_service_stop,
        .rso_fini  = sender_cm_service_fini
};

static int sender_cm_service_allocate(struct m0_reqh_service **service,
                                      struct m0_reqh_service_type *stype,
                                      struct m0_reqh_context *rctx)
{
        struct m0_cm *cm = &sender_cm;

        *service = &cm->cm_service;
        (*service)->rs_ops = &sender_cm_service_ops;
        (*service)->rs_sm.sm_state = M0_RST_INITIALISING;

        return m0_cm_init(cm, container_of(stype, struct m0_cm_type, ct_stype),
                          &sender_cm_ops);
}

static const struct m0_reqh_service_type_ops sender_cm_service_type_ops = {
        .rsto_service_allocate = sender_cm_service_allocate,
};

M0_CM_TYPE_DECLARE(sender_cm, &sender_cm_service_type_ops, "sender_cm",
                   &m0_addb_ct_ut_service);

void sender_service_alloc_init()
{
        int rc;
        /* Internally calls m0_cm_init(). */
        M0_ASSERT(sender_cm_service == NULL);
        rc = m0_reqh_service_allocate(&sender_cm_service,
				      &sender_cm_cmt.ct_stype,
                                      &sender_rctx);
        M0_ASSERT(rc == 0);
        m0_reqh_service_init(sender_cm_service, &rmach_ctx.rmc_reqh, NULL);
}

M0_TL_DECLARE(proxy_cp, M0_EXTERN, struct m0_cm_cp);

static void cp_cm_proxy_init(struct m0_cm_proxy *proxy, const char *endpoint)
{
	proxy->px_sw.sw_lo = ag_id;
	proxy->px_sw.sw_hi = ag_id;
	proxy->px_endpoint = endpoint;
	proxy_tlink_init(proxy);
	proxy_cp_tlist_init(&proxy->px_pending_cps);
	m0_mutex_init(&proxy->px_mutex);
}

static void sender_init()
{
        int rc;

	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);

        rc = m0_cm_type_register(&sender_cm_cmt);
        M0_UT_ASSERT(rc == 0);
        sender_service_alloc_init();
        rc = m0_reqh_service_start(sender_cm_service);
        M0_UT_ASSERT(rc == 0);
        rc = m0_ios_poolmach_init(sender_cm_service);
        M0_UT_ASSERT(rc == 0);
	cm_ready(&sender_cm);
        rc = m0_cm_start(&sender_cm);
        M0_UT_ASSERT(rc == 0);

        while (m0_fom_domain_is_idle(&rmach_ctx.rmc_reqh.rh_fom_dom) ||
                        !m0_cm_cp_pump_is_complete(&sender_cm.cm_cp_pump))
                usleep(200);

        rc = m0_net_domain_init(&client_net_dom, xprt, &m0_addb_proc_ctx);
        M0_UT_ASSERT(rc == 0);

        rc = m0_rpc_client_start(&cctx);
        M0_UT_ASSERT(rc == 0);
        rc = m0_rpc_client_connect(&sender_cm_proxy.px_conn,
				   &sender_cm_proxy.px_session,
                                   &cctx.rcx_rpc_machine,
                                   cctx.rcx_remote_addr,
                                   cctx.rcx_max_rpcs_in_flight,
                                   cctx.rcx_nr_slots);
	M0_UT_ASSERT(rc == 0);
	cp_cm_proxy_init(&sender_cm_proxy,
		sender_cm_proxy.px_conn.c_rpcchan->rc_destep->nep_addr);
        m0_cm_lock(&sender_cm);
	m0_cm_proxy_add(&sender_cm, &sender_cm_proxy);
        m0_cm_unlock(&sender_cm);
}

static void receiver_fini()
{
        m0_cm_lock(cm);
        m0_cm_proxy_del(cm, &recv_cm_proxy);
        m0_cm_unlock(cm);
        m0_cm_stop(cm);
        m0_free(scm->sc_it.si_fdata);
	m0_free(sns_ag.sag_fc);
        cs_fini(&sctx);
}

static void sender_fini()
{
        int rc;
        int i;

        m0_cm_lock(&sender_cm);
	m0_cm_proxy_del(&sender_cm, &sender_cm_proxy);
        m0_cm_unlock(&sender_cm);
	m0_cm_proxy_rpc_conn_close(&sender_cm_proxy);
        /* Fini the sender side. */
        rc = m0_cm_stop(&sender_cm);
        M0_UT_ASSERT(rc == 0);
        rc = m0_rpc_client_stop(&cctx);
        M0_UT_ASSERT(rc == 0);
        m0_net_domain_fini(&client_net_dom);
	m0_reqh_fom_domain_idle_wait(&rmach_ctx.rmc_reqh);
        m0_ios_poolmach_fini(sender_cm_service);
        m0_reqh_service_stop(sender_cm_service);
        m0_reqh_service_fini(sender_cm_service);
        m0_cm_type_deregister(&sender_cm_cmt);
	m0_ut_rpc_mach_fini(&rmach_ctx);

        for (i = 0; i < BUF_NR; ++i)
                bv_free(&s_buf[i].nb_buffer);
        bv_free(&r_buf.nb_buffer);

        m0_semaphore_fini(&sem);
        m0_semaphore_fini(&cp_sem);
        m0_semaphore_fini(&read_cp_sem);
}

static void test_fini()
{
        sender_fini();
        receiver_fini();
}

static void test_init()
{
        receiver_init();
        sender_init();
}

static void test_cp_send_recv_verify()
{
        int                   i;
        char                  data;
        struct m0_net_buffer *nbuf;

        test_init();

        m0_semaphore_init(&sem, 0);
        m0_semaphore_init(&cp_sem, 0);
        ag_setup(&s_sag);

        for (i = 0; i < BUF_NR; ++i)
                s_buf[i].nb_pool = &nbp;

        data = START_DATA;
        cp_prepare(&s_sns_cp.sc_base, &s_buf[0], SEG_NR, SEG_SIZE, &s_sag, data,
                   &cp_fom_ops, sender_cm_service->rs_reqh, 0, false,
		   &sender_cm);
        for (i = 1; i < BUF_NR; ++i) {
                data = i + START_DATA;
                bv_populate(&s_buf[i].nb_buffer, data, SEG_NR, SEG_SIZE);
                m0_cm_cp_buf_add(&s_sns_cp.sc_base, &s_buf[i]);
                s_buf[i].nb_pool->nbp_seg_nr = SEG_NR;
                s_buf[i].nb_pool->nbp_seg_size = SEG_SIZE;
                s_buf[i].nb_pool->nbp_buf_nr = 1;
        }
        m0_tl_for(cp_data_buf, &s_sns_cp.sc_base.c_buffers, nbuf) {
                M0_UT_ASSERT(nbuf != NULL);
        } m0_tl_endfor;

        m0_bitmap_init(&s_sns_cp.sc_base.c_xform_cp_indices,
                       s_sag.sag_base.cag_cp_global_nr);
        s_sns_cp.sc_base.c_ops = &cp_dummy_ops;
        /* Set some bit to true. */
        m0_bitmap_set(&s_sns_cp.sc_base.c_xform_cp_indices, 1, true);
        s_sns_cp.sc_base.c_cm_proxy = &sender_cm_proxy;
        s_sns_cp.sc_sid = sid;
        s_sns_cp.sc_index = 0;
	s_sns_cp.sc_base.c_data_seg_nr = SEG_NR * BUF_NR;
	/* Assume this as accumulator copy packet to be sent on remote side. */
	s_sns_cp.sc_base.c_ag_cp_idx = ~0;
	m0_cm_lock(&sender_cm);
	m0_cm_aggr_group_add(&sender_cm, &s_sag.sag_base, true);
	m0_cm_unlock(&sender_cm);

        m0_fom_queue(&s_sns_cp.sc_base.c_fom, &rmach_ctx.rmc_reqh);

        /* Wait till ast gets posted. */
        m0_semaphore_down(&sem);
        m0_semaphore_down(&cp_sem);
        sleep(STOB_UPDATE_DELAY);

        read_and_verify();

        test_fini();
}

const struct m0_test_suite snscm_net_ut = {
        .ts_name = "snscm_net-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "cp-send-recv-verify", test_cp_send_recv_verify },
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
