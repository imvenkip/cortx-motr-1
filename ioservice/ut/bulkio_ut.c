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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *		    Madhavrao Vemuri <madhav_vemuri@xyratec.com>
 * Original creation date: 09/29/2011
 */

#include "lib/ut.h"
#include "lib/list.h"
#include "lib/trace.h"
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "ioservice/io_fops.h"	/* c2_io_fop */

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "rpc/rpc2.h"		/* c2_rpc_bulk, c2_rpc_bulk_buf */
#include "rpc/rpclib.h"		/* c2_rpc_ctx */
#include "reqh/reqh.h"		/* c2_reqh */
#include "net/net.h"		/* C2_NET_QT_PASSIVE_BULK_SEND */
#include "ut/rpc.h"		/* c2_rpc_client_init, c2_rpc_server_init */
#include "ut/cs_service.h"	/* ds1_service_type */
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/thread.h"		/* C2_THREAD_INIT */
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "lib/misc.h"		/* C2_SET_ARR0 */
#include "reqh/reqh_service.h"	/* c2_reqh_service */

#include "ioservice/io_foms.h"
#include "ioservice/io_service.h"

#include "ioservice/io_fops.c"	/* To access static apis for testing. */
#include "ioservice/io_foms.c"
enum IO_UT_VALUES {
	IO_FIDS_NR		= 16,
	IO_SEGS_NR		= 16,
	IO_SEQ_LEN		= 8,
	IO_FOPS_NR		= 16,
	MAX_SEGS_NR		= 256,
	IO_SEG_SIZE		= 4096,
	IO_XPRT_NR		= 1,
	IO_FID_SINGLE		= 1,
	IO_RPC_ITEM_TIMEOUT	= 300,
	IO_SEG_START_OFFSET	= IO_SEG_SIZE,
	IO_FOP_SINGLE		= 1,
	IO_ADDR_LEN		= 32,
	IO_SEG_STEP		= 64,
	IO_CLIENT_COBDOM_ID	= 21,
	IO_SERVER_COBDOM_ID	= 29,
	IO_RPC_SESSION_SLOTS	= 8,
	IO_RPC_MAX_IN_FLIGHT	= 32,
	IO_RPC_CONN_TIMEOUT	= 60,
};

C2_TL_DESCR_DECLARE(rpcbulk, extern);
extern const struct c2_net_buffer_callbacks rpc_bulk_cb;
extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;
static void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size);

/* Structure containing data needed for UT. */
struct bulkio_params {
	/* Fids of global files. */
	struct c2_fop_file_fid		  bp_fids[IO_FIDS_NR];

	/* Tracks offsets for global fids. */
	uint64_t			  bp_offsets[IO_FIDS_NR];

	/* In-memory fops for read IO. */
	struct c2_io_fop		**bp_rfops;

	/* In-memory fops for write IO. */
	struct c2_io_fop		**bp_wfops;

	/* Read buffers to which data will be transferred. */
	struct c2_net_buffer		**bp_iobuf;

	/* Threads to post rpc items to rpc layer. */
	struct c2_thread		**bp_threads;

	/*
	 * Standard buffers containing a data pattern.
	 * Primarily used for data verification in read and write IO.
	 */
	char				 *bp_readbuf;
	char				 *bp_writebuf;

	/* Structures used by client-side rpc code. */
	struct c2_dbenv			  bp_cdbenv;
	struct c2_cob_domain		  bp_ccbdom;
	char				  bp_cendpaddr[IO_ADDR_LEN];
	char				  bp_cdbname[IO_ADDR_LEN];
	struct c2_net_domain		  bp_cnetdom;
	struct c2_rpc_client_ctx	 *bp_cctx;

	/* Structures used by server-side rpc code. */
	char				  bp_sdbfile[IO_ADDR_LEN];
	char				  bp_sstobfile[IO_ADDR_LEN]; 
	char				  bp_slogfile[IO_ADDR_LEN];
	struct c2_rpc_server_ctx	 *bp_sctx;

	struct c2_net_xprt		 *bp_xprt;
};

/* Pointer to global structure bulkio_params. */
struct bulkio_params *bp;

/* A structure used to pass as argument to io threads. */
struct thrd_arg {
	/* Index in fops array to be posted to rpc layer. */
	int			ta_index;
	/* Type of fop to be sent (read/write). */
	enum C2_RPC_OPCODES	ta_op;
};

/*
static char			  bp_cendpaddr[] = "127.0.0.1:23134:2";
static char			  bp_cdbname[]	= "bulk_c_db";
static char			  bp_sdbfile[]	= "bulkio_ut.db";
static char			  bp_sstobfile[]	= "bulkio_ut_stob";
static char			  bp_slogfile[]	= "bulkio_ut.log";
*/

#define S_ENDP_ADDR		  "127.0.0.1:23134:1"
#define S_ENDPOINT		  "bulk-sunrpc:"S_ENDP_ADDR
#define S_DBFILE		  "bulkio_ut.db"
#define S_STOBFILE		  "bulkio_ut_stob"

static struct c2_rpc_client_ctx c_rctx = {
	.rcx_remote_addr	= S_ENDP_ADDR,
	.rcx_cob_dom_id		= IO_CLIENT_COBDOM_ID,
	.rcx_nr_slots		= IO_RPC_SESSION_SLOTS,
	.rcx_max_rpcs_in_flight	= IO_RPC_MAX_IN_FLIGHT,
	.rcx_timeout_s		= IO_RPC_CONN_TIMEOUT,
};

/* Input arguments for colibri server setup. */
static char *server_args[]	= {"bulkio_ut", "-r", "-T", "linux", "-D",
				   S_DBFILE, "-S", S_STOBFILE, "-e",
				   S_ENDPOINT, "-s", "ioservice"};

/* Array of services to be started by colibri server. */
static struct c2_reqh_service_type *stypes[] = {
	&ds1_service_type,
	&ds2_service_type,
};

/*
 * Colibri server rpc context. Can't use C2_RPC_SERVER_CTX_DECLARE_SIMPLE()
 * since it limits the scope of struct c2_rpc_server_ctx to the function
 * where it is declared.
 */
static struct c2_rpc_server_ctx s_rctx = {
	.rsx_xprts_nr		= IO_XPRT_NR,
	.rsx_argv		= server_args,
	.rsx_argc		= ARRAY_SIZE(server_args),
	.rsx_service_types	= stypes,
	.rsx_service_types_nr	= ARRAY_SIZE(stypes),
};

static int io_fop_dummy_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_server_write_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int ut_io_fom_cob_rw_create(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_server_read_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_stob_create_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int check_write_fom_state_transition(struct c2_fom *fom);
static int check_read_fom_state_transition(struct c2_fom *fom);

struct c2_fop_type_ops bulkio_stob_create_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_write_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_read_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

/*
 * An alternate io fop type op vector to test bulk client functionality only.
 * Only .fto_fom_init is pointed to a UT function which tests the received
 * io fop is sane and bulk io transfer is taking place properly using data
 * from io fop. Rest all ops are same as io_fop_rwv_ops.
 */
struct c2_fop_type_ops bulkio_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

static struct c2_fom_type_ops bulkio_dummy_fom_type_ops = {
	.fto_create = io_fop_dummy_fom_create,
};

static struct c2_fom_type_ops bulkio_server_write_fom_type_ops = {
	.fto_create = io_fop_server_write_fom_create,
};

static struct c2_fom_type_ops bulkio_server_read_fom_type_ops = {
	.fto_create = io_fop_server_read_fom_create,
};

static struct c2_fom_type_ops bulkio_stob_create_fom_type_ops = {
	.fto_create = io_fop_stob_create_fom_create,
};

static struct c2_fom_type_ops ut_io_fom_cob_rw_type_ops = {
	.fto_create = ut_io_fom_cob_rw_create,
};

static struct c2_fom_type bulkio_dummy_fom_type = {
	.ft_ops = &bulkio_dummy_fom_type_ops,
};

static struct c2_fom_type bulkio_server_write_fom_type = {
	.ft_ops = &bulkio_server_write_fom_type_ops,
};

static struct c2_fom_type bulkio_server_read_fom_type = {
	.ft_ops = &bulkio_server_read_fom_type_ops,
};
static struct c2_fom_type bulkio_stob_create_fom_type = {
	.ft_ops = &bulkio_stob_create_fom_type_ops,
};

/*
 * Intercepting FOM to test I/O FOM functions for different phases.
 */
static struct c2_fom_type ut_io_fom_cob_rw_type_mopt = {
	.ft_ops = &ut_io_fom_cob_rw_type_ops,
};

static void bulkio_stob_fom_fini(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rw   *fom_obj = NULL;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        c2_stob_put(fom_obj->fcrw_stob);
	c2_fom_fini(fom);
	c2_free(fom);
}

static void bulkio_fom_fini(struct c2_fom *fom)
{
	c2_fom_fini(fom);
	c2_free(fom);
}

struct c2_net_buffer_pool * ut_get_buffer_pool(struct c2_fom *fom)
{
        struct c2_reqh_io_service    *serv_obj;
        struct c2_rios_buffer_pool   *bpdesc = NULL;
        struct c2_net_domain         *fop_ndom = NULL;
        struct c2_fop                *fop = NULL;

        fop = fom->fo_fop;
        serv_obj = container_of(fom->fo_service,
                                struct c2_reqh_io_service, rios_gen);

        /* Get network buffer pool for network domain */
        fop_ndom
        = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
        c2_tlist_for(&bufferpools_tl, &serv_obj->rios_buffer_pools,
                     bpdesc) {
                if (bpdesc->rios_ndom == fop_ndom) {
                        return &bpdesc->rios_bp;
                }
        } c2_tlist_endfor;

        return NULL;
}

/* This function is used to bypass request handler while testing.*/
static void ut_fom_wait_dummy(struct c2_fom *fom)
{
        struct c2_fom_locality *loc;

        C2_PRE(fom->fo_state == FOS_RUNNING);

        loc = fom->fo_loc;
        C2_ASSERT(c2_mutex_is_locked(&loc->fl_lock));
        c2_list_add_tail(&loc->fl_wail, &fom->fo_linkage);
        C2_CNT_INC(loc->fl_wail_nr);
}

/* This function is used to bypass request handler while testing.*/
static bool ut_fom_cb_dummy(struct c2_clink *clink)
{
        struct c2_fom_locality  *loc;
        struct c2_fom           *fom;

        C2_PRE(clink != NULL);

        fom = container_of(clink, struct c2_fom, fo_clink);
        loc = fom->fo_loc;
        c2_mutex_lock(&loc->fl_lock);
        C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_linkage));
        c2_list_del(&fom->fo_linkage);
        C2_CNT_DEC(loc->fl_wail_nr);
        c2_mutex_unlock(&loc->fl_lock);
        return true;
}

/*
 * - This is positive test case to test c2_io_fom_cob_rw_state(fom).
 * - This function test next phase after every defined phase for Write FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 */
static int bulkio_server_write_fom_state(struct c2_fom *fom)
{
	int rc;
	switch(fom->fo_phase) {
	case FOPH_IO_FOM_BUFFER_ACQUIRE :
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT ||
                fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);
		break;
	case FOPH_IO_ZERO_COPY_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);
		break;
	case FOPH_IO_ZERO_COPY_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_INIT);
		break;
	case FOPH_IO_STOB_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_WAIT);
		break;
	case FOPH_IO_STOB_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);
		break;
	case FOPH_IO_BUFFER_RELEASE:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase == FOPH_SUCCESS ||
                fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE);
		break;
	default :
		rc = c2_io_fom_cob_rw_state(fom);
	}
	return rc;
}

/*
 * - This is positive test case to test c2_io_fom_cob_rw_state(fom).
 * - This function test next phase after every defined phase for Read FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 */
static int bulkio_server_read_fom_state(struct c2_fom *fom)
{
	int rc;

	switch(fom->fo_phase) {
	case FOPH_IO_FOM_BUFFER_ACQUIRE :
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT ||
                fom->fo_phase == FOPH_IO_STOB_INIT);
		break;
	case FOPH_IO_ZERO_COPY_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);
		break;
	case FOPH_IO_ZERO_COPY_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_BUFFER_RELEASE);
		break;
	case FOPH_IO_STOB_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_STOB_WAIT);
		break;
	case FOPH_IO_STOB_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);
		break;
	case FOPH_IO_BUFFER_RELEASE:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase == FOPH_SUCCESS ||
                fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE);
		break;
	default :
		rc = c2_io_fom_cob_rw_state(fom);
	}
	return rc;
}

/*
 * This function intercepts actual I/O FOM state,
 * for state transition testing.
 *
 * This ut FOM work with real fop send by bulk client.
 * - Client first send write fop
 * - Fops at server side are intercepted by this dummy state function and
     checks all possible state transitions.
 * - It simulates failure environment for particular state and restore
 *   it again after each test.
 * - After reply fop is received by client, client sends a read fop to read
 *   data written by previous write fop.
 * - Further it will checks remaining state transitions.
 * - After reply fop is received by client, at client side received data is
 *   compared with the original data used to send it.
 */
static int ut_io_fom_cob_rw_state(struct c2_fom *fom)
{
        int        rc = 0;

        if (c2_is_read_fop(fom->fo_fop))
                rc = check_read_fom_state_transition(fom);
        else
                rc = check_write_fom_state_transition(fom);

        return rc;
}

/*
 * - This function test next phase after every defined phase for Write FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 * - This test covers all positive as well as negative cases.
 * Note : For each test case it does following things,
 *      - simulates the environment,
 *      - run state function for respective I/O FOM,
 *      - check output state & return code,
 *      - restores the FOM to it's clean state by using the saved original data.
 */
static int check_write_fom_state_transition(struct c2_fom *fom)
{
        int                           rc;
        int                           i = 0;
        int                           colour;
        int                           acquired_net_bufs = 0;
        int                           saved_segments_count = 0;
        int                           saved_ndesc = 0;
        struct c2_fop_cob_rw         *rwfop;
        struct c2_net_domain         *netdom = NULL;
        struct c2_fop                *fop;
        struct c2_io_fom_cob_rw      *fom_obj = NULL;
        struct c2_net_buffer         *nb_list[64];
        struct c2_net_buffer_pool    *bp;
        struct c2_fop_file_fid        saved_fid;
        struct c2_fop_file_fid        invalid_fid;
        struct c2_stob_io_desc       *saved_stobio_desc;
        struct c2_stob_domain        *fom_stdom;
        struct c2_fop_file_fid       *ffid;
        struct c2_fid                 fid;
        struct c2_stob_id             stobid;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);
        colour = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_colour;

        /*
         * No need to test generic phases.
         */
        if (fom->fo_phase < FOPH_NR)
        {
                rc = c2_io_fom_cob_rw_state(fom);
                return rc;
        }

        /* Acquire all buffer pool buffer test some of cases. */
        if (fom_obj->fcrw_bp == NULL)
                bp = ut_get_buffer_pool(fom);
        else
                bp = fom_obj->fcrw_bp;
        C2_UT_ASSERT(bp != NULL);

        /* Acquire all buffers from buffer pool to make it empty.*/
        c2_net_buffer_pool_lock(bp);
        nb_list[i] = c2_net_buffer_pool_get(bp, colour);
        while (nb_list[i] != NULL) {
                i++;
                nb_list[i] =
                c2_net_buffer_pool_get(bp, colour);
        }
        c2_net_buffer_pool_unlock(bp);


        /*
         * Case 01 : No network buffer is available with the buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 02 : No network buffer is available with the buffer pool.
         *         Even after getting buffer pool not-empty event, buffers are
         *         not available in pool (which could be used by other FOMs
         *         in the server).
         *         Input phase          : FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & rstore FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 03 : Network buffer is available with the buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: FOPH_IO_ZERO_COPY_INIT
         */
        c2_net_buffer_pool_lock(bp);
        c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(bp);

        fom->fo_phase =  FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);

        /*
         * Cleanup & restore FOM for next test.
         * Since previous case successfully acquired network buffer
         * and now buffer pool not having any network buffer, this buffer need
         * to return back to the buffer pool.
         */
        acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
        while (acquired_net_bufs > 0) {
                struct c2_net_buffer           *nb = NULL;

                nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                netbufs_tlink_del_fini(nb);
                acquired_net_bufs--;
        }
        c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
        fom_obj->fcrw_batch_size = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 04 : Network buffer is available with the buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: FOPH_IO_ZERO_COPY_INIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);

        /*
         * No need to cleanup here, since FOM will be  transitioned to
         * expected phase.
         */

        /*
         * Case 05 : Zero-copy failure
         *         Input phase          : FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: FOPH_FAILURE
         */

        /*
         * Modify segments count in fop (value greater than net domain max),
         * so that zero-copy initialisation fails.
         */
        saved_segments_count =
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr;
        netdom = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        c2_net_domain_get_max_buffer_segments(netdom)+1;

        fom->fo_phase =  FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & restore FOM for next test. */
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        saved_segments_count;
        c2_rpc_bulk_fini(&fom_obj->fcrw_bulk);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 06 : Zero-copy success
         *         Input phase          : FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: FOPH_IO_ZERO_COPY_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &ut_fom_cb_dummy;

        fom->fo_phase =  FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);

        /*
         * Cleanup & make clean FOM for next test.
         * Since this fom will not go actual wait queue,
         * need to unlock locality.
         */
        ut_fom_wait_dummy(fom);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        c2_mutex_lock(&fom->fo_loc->fl_lock);
        while(fom->fo_loc->fl_wail_nr > 0) {
                c2_mutex_unlock(&fom->fo_loc->fl_lock);
                sleep(1);
                c2_mutex_lock(&fom->fo_loc->fl_lock);
        }
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        /*
         * Case 07 : Zero-copy failure
         *         Input phase          : FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: FOPH_FAILURE
         */
        fom->fo_phase =  FOPH_IO_ZERO_COPY_WAIT;
        fom_obj->fcrw_bulk.rb_rc  = -1;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_bulk.rb_rc  = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 08 : Zero-copy success from wait state.
         *         Input phase          : FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: FOPH_IO_STOB_INIT
         */
        fom->fo_phase =  FOPH_IO_ZERO_COPY_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_STOB_INIT);

        /*
         * Case 09 : STOB I/O launch failure
         *         Input phase          : FOPH_IO_STOB_INIT
         *         Expected Output phase: FOPH_FAILURE
         */

        /* Save original fid and pass invialid fid to make I/O launch fail.*/
        saved_fid = rwfop->crw_fid;
        invalid_fid.f_seq = 111;
        invalid_fid.f_oid = 222;

        rwfop->crw_fid = invalid_fid;

        fom->fo_phase = FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 && rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_fid = saved_fid;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 10 : STOB I/O launch success
         *         Input phase          : FOPH_IO_STOB_INIT
         *         Expected Output phase: FOPH_IO_STOB_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &ut_fom_cb_dummy;

        fom->fo_phase =  FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == FSO_WAIT  &&
                     fom->fo_phase == FOPH_IO_STOB_WAIT);

        /*
         * Cleanup & make clean FOM for next test.
         * Since this FOM	 will not go actual wait queue,
         * need to unlock locality.
         */
        ut_fom_wait_dummy(fom);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        c2_mutex_lock(&fom->fo_loc->fl_lock);
        while(fom->fo_loc->fl_wail_nr > 0) {
                c2_mutex_unlock(&fom->fo_loc->fl_lock);
                sleep(1);
                c2_mutex_lock(&fom->fo_loc->fl_lock);
        }
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        /*
         * Case 11 : STOB I/O failure from wait state.
         *         Input phase          : FOPH_IO_STOB_WAIT
         *         Expected Output phase: FOPH_FAILURE
         */

	/*
         * To test this case there is a need to invalidate stobio descriptor,
         * since io_fom_cob_rw_io_finish() removes the stobio descriptor
         * from list.
         * There is only one stobio descriptor.
         * Before returning error this phase will do following phases :
         * - free and remove stobio descriptors in list,
         * - put stob object
         * - leave FOM block
         */
        saved_stobio_desc = stobio_tlist_head(&fom_obj->fcrw_stio_list);
        C2_UT_ASSERT(saved_stobio_desc != NULL);
        stobio_tlist_del(saved_stobio_desc);

        fom->fo_rc    = -1;
        fom->fo_phase =  FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 && rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /*
         * Cleanup & make clean FOM for next test.
         * Restore original fom.
         */
        stobio_tlist_add(&fom_obj->fcrw_stio_list, saved_stobio_desc);
        ffid = &rwfop->crw_fid;
        io_fom_cob_rw_fid_wire2mem(ffid, &fid);
        io_fom_cob_rw_fid2stob_map(&fid, &stobid);
        fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

        rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
        C2_UT_ASSERT(rc == 0);

        c2_fom_block_enter(fom);

        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 12 : STOB I/O success
         *         Input phase          : FOPH_IO_STOB_WAIT
         *         Expected Output phase: FOPH_IO_BUFFER_RELEASE
         */
        fom->fo_phase =  FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_BUFFER_RELEASE);

        /*
         * Case 13 : Processing of remaining buffer descriptors.
         *         Input phase          : FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_ACQUIRE
         */
        fom->fo_phase = FOPH_IO_BUFFER_RELEASE;

        saved_ndesc = fom_obj->fcrw_ndesc;
        fom_obj->fcrw_ndesc = 2;
        rwfop->crw_desc.id_nr = 2;
        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_ndesc = saved_ndesc;
        rwfop->crw_desc.id_nr = saved_ndesc;

        /*
         * Case 14 : All buffer descriptors are processed.
         *         Input phase          : FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: FOPH_SUCCESS
         */
        fom->fo_phase = FOPH_IO_BUFFER_RELEASE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_SUCCESS);

        c2_net_buffer_pool_lock(bp);
        while (i > 0) {
                c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        }
        c2_net_buffer_pool_unlock(bp);

	return rc;
}

/*
 * - This function test next phase after every defined phase for Read FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 * - This test cover positive as well as negative cases.
 * Note : For each test case it does following things
 *      - simulate environemnt,
 *      - run state function for respective I/O FOM,
 *      - check output state & return code,
 *      - restores the FOM to it's clean state by using the saved original data.
 */
static int check_read_fom_state_transition(struct c2_fom *fom)
{
        int                           rc;
        int                           i = 0;
        int                           colour;
        int                           acquired_net_bufs = 0;
        int                           saved_segments_count = 0;
        int                           saved_ndesc = 0;
        struct c2_fop_cob_rw         *rwfop;
        struct c2_net_domain         *netdom = NULL;
        struct c2_fop                *fop;
        struct c2_io_fom_cob_rw      *fom_obj = NULL;
        struct c2_net_buffer         *nb_list[64];
        struct c2_net_buffer_pool    *bp;
        struct c2_fop_file_fid        saved_fid;
        struct c2_fop_file_fid        invalid_fid;
        struct c2_stob_io_desc       *saved_stobio_desc;
        struct c2_stob_domain        *fom_stdom;
        struct c2_fop_file_fid       *ffid;
        struct c2_fid                 fid;
        struct c2_stob_id             stobid;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);
        colour = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_colour;

        /*
         * No need to test generic phases.
         */
        if (fom->fo_phase < FOPH_NR)
        {
                rc = c2_io_fom_cob_rw_state(fom);
                return rc;
        }

        /* Acquire all buffer pool buffer test some of cases. */
        if (fom_obj->fcrw_bp == NULL)
                bp = ut_get_buffer_pool(fom);
        else
                bp = fom_obj->fcrw_bp;
        C2_UT_ASSERT(bp != NULL);

        /* Acquires all buffers from the buffer pool to make it empty.*/
        i = 0;
        c2_net_buffer_pool_lock(bp);
        nb_list[i] = c2_net_buffer_pool_get(bp, colour);
        while (nb_list[i] != NULL) {
                i++;
                nb_list[i] =
                c2_net_buffer_pool_get(bp, colour);
        }
        c2_net_buffer_pool_unlock(bp);


        /*
         * Case 01 : No network buffer is available with buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == FSO_WAIT  &&
                     fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 02 : No network buffer is available with buffer pool.
         *         Even after getting buffer pool not-empty event, buffers are
         *         not available in pool (which could be used by other FOMs
         *         in the server).
         *         Input phase          : FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase ==  FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 03 : Network buffer is available with the buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: FOPH_IO_STOB_INIT
         */
        c2_net_buffer_pool_lock(bp);
        c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(bp);

        fom->fo_phase =  FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_STOB_INIT);

        /*
         * Cleanup & make clean FOM for next test.
         * Since previous this case successfully acquired network buffer
         * and now buffer pool not having network buffer, this buffer need
         * to return back to the buffer pool.
         */
        acquired_net_bufs = netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
        c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
        while (acquired_net_bufs > 0) {
                struct c2_net_buffer           *nb = NULL;

                nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                netbufs_tlink_del_fini(nb);
                acquired_net_bufs--;
        }
        c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
        fom_obj->fcrw_batch_size = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 04 : Network buffer available with buffer pool.
         *         Input phase          : FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: FOPH_IO_STOB_INIT
         */
        fom->fo_phase =  FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_STOB_INIT);
        /* No need to cleanup here, since FOM will transitioned to expected
         *  phase.
	 */

        /*
         * Case 05 : STOB I/O launch failure
         *         Input phase          : FOPH_IO_STOB_INIT
         *         Expected Output phase: FOPH_FAILURE
         */

        /* Save original fid and pass invalid fid to make I/O launch fail.*/
        saved_fid = rwfop->crw_fid;
        invalid_fid.f_seq = 111;
        invalid_fid.f_oid = 222;

        rwfop->crw_fid = invalid_fid;

        fom->fo_phase = FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_fid = saved_fid;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 06 : STOB I/O launch success
         *         Input phase          : FOPH_IO_STOB_INIT
         *         Expected Output phase: FOPH_IO_STOB_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &ut_fom_cb_dummy;

        fom->fo_phase =  FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase == FOPH_IO_STOB_WAIT);

        /*
         * Cleanup & restore FOM for next test.
         * Since this fom will not go the actual wait queue,
         * need to unlock locality.
         */
        ut_fom_wait_dummy(fom);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        c2_mutex_lock(&fom->fo_loc->fl_lock);
        while(fom->fo_loc->fl_wail_nr > 0) {
                c2_mutex_unlock(&fom->fo_loc->fl_lock);
                sleep(1);
                c2_mutex_lock(&fom->fo_loc->fl_lock);
        }
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);
        /*
         * Case 07 : STOB I/O failure
         *         Input phase          : FOPH_IO_STOB_WAIT
         *         Expected Output phase: FOPH_FAILURE
         */
        /*
         * To test this case there is a need to invalidate stobio descriptor,
         * since io_fom_cob_rw_io_finish() remove stobio descriptor
         * from list.
         * There is only one stobio descriptor.
         * Before returning error this phase will do following phases :
         * - free and remove stobio descriptors in list,
         * - put stob object
         * - leave FOM block
         */
        saved_stobio_desc = stobio_tlist_head(&fom_obj->fcrw_stio_list);
        C2_UT_ASSERT(saved_stobio_desc != NULL);
        stobio_tlist_del(saved_stobio_desc);

        fom->fo_rc = -1;
        fom->fo_phase =  FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /*
         * Cleanup & make clean FOM for next test.
         * Restore original fom.
         */
        stobio_tlist_add(&fom_obj->fcrw_stio_list, saved_stobio_desc);
        ffid = &rwfop->crw_fid;
        io_fom_cob_rw_fid_wire2mem(ffid, &fid);
        io_fom_cob_rw_fid2stob_map(&fid, &stobid);
        fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

        rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
        C2_UT_ASSERT(rc == 0);

        c2_fom_block_enter(fom);

        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 08 : STOB I/O success
         *         Input phase          : FOPH_IO_STOB_WAIT
         *         Expected Output phase: FOPH_IO_ZERO_COPY_INIT
         */
        fom->fo_phase =  FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_ZERO_COPY_INIT);

        /*
         * Case 09 : Zero-copy failure
         *         Input phase          : FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: FOPH_FAILURE
         */

        /*
         * Modify segments count in fop (value greater than net domain max),
         * so that zero-copy initialisation fails.
         */
        saved_segments_count =
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr;
        netdom = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm.ntm_dom;
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        c2_net_domain_get_max_buffer_segments(netdom)+1;

        fom->fo_phase =  FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        saved_segments_count;
        c2_rpc_bulk_fini(&fom_obj->fcrw_bulk);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 10 : Zero-copy success
         *         Input phase          : FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: FOPH_IO_ZERO_COPY_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &ut_fom_cb_dummy;

        fom->fo_phase =  FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_WAIT  &&
                     fom->fo_phase == FOPH_IO_ZERO_COPY_WAIT);

        /*
         * Cleanup & restore FOM for next test.
         * Since this FOM will not go actual wait queue,
         * need to unlock locality.
         */
        ut_fom_wait_dummy(fom);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        c2_mutex_lock(&fom->fo_loc->fl_lock);
        while(fom->fo_loc->fl_wail_nr > 0) {
                c2_mutex_unlock(&fom->fo_loc->fl_lock);
                sleep(1);
                c2_mutex_lock(&fom->fo_loc->fl_lock);
        }
        c2_clink_del(&fom->fo_clink);
        c2_mutex_unlock(&fom->fo_loc->fl_lock);

        /*
         * Case 11 : Zero-copy failure
         *         Input phase          : FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: FOPH_FAILURE
         */
        fom->fo_phase =  FOPH_IO_ZERO_COPY_WAIT;
        fom_obj->fcrw_bulk.rb_rc  = -1;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_bulk.rb_rc  = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 12 : Zero-copy success
         *         Input phase          : FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: FOPH_IO_BUFFER_RELEASE
         */
        fom->fo_phase =  FOPH_IO_ZERO_COPY_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_BUFFER_RELEASE);


        /*
         * Case 13 : Processing of remaining buffer descriptors.
         *         Input phase          : FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: FOPH_IO_FOM_BUFFER_ACQUIRE
         */
        fom->fo_phase = FOPH_IO_BUFFER_RELEASE;

        saved_ndesc = fom_obj->fcrw_ndesc;
        fom_obj->fcrw_ndesc = 2;
        rwfop->crw_desc.id_nr = 2;
        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_IO_FOM_BUFFER_ACQUIRE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_ndesc = saved_ndesc;
        rwfop->crw_desc.id_nr = saved_ndesc;

        /*
         * Case 14 : All buffer descriptors are processed.
         *         Input phase          : FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: FOPH_SUCCESS
         */
        fom->fo_phase = FOPH_IO_BUFFER_RELEASE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == FSO_AGAIN  &&
                     fom->fo_phase == FOPH_SUCCESS);

        c2_net_buffer_pool_lock(bp);
        while (i > 0) {
                c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        }
        c2_net_buffer_pool_unlock(bp);

	return rc;
}

/* It is used to create the stob specified in the fid of each fop. */
static int bulkio_stob_create_fom_state(struct c2_fom *fom)
{
        struct c2_fop_cob_rw            *rwfop;
        struct c2_stob_domain           *fom_stdom;
        struct c2_fop_file_fid          *ffid;
        struct c2_fid                    fid;
        struct c2_stob_id                stobid;
        int				 rc;
	struct c2_fop			*fop;
	struct c2_fop_cob_writev_rep	*wrep;

        struct c2_io_fom_cob_rw  *fom_obj;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        rwfop = io_rw_get(fom->fo_fop);

	C2_UT_ASSERT(rwfop->crw_desc.id_nr == rwfop->crw_ivecs.cis_nr);
        ffid = &rwfop->crw_fid;
        io_fom_cob_rw_fid_wire2mem(ffid, &fid);
        io_fom_cob_rw_fid2stob_map(&fid, &stobid);
        fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

        rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(fom_obj->fcrw_stob->so_state == CSS_UNKNOWN);

        rc = c2_stob_create(fom_obj->fcrw_stob, &fom->fo_tx);
        C2_UT_ASSERT(rc == 0);

	fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
	wrep = c2_fop_data(fop);
	wrep->c_rep.rwr_rc = 0;
	wrep->c_rep.rwr_count = rwfop->crw_ivecs.cis_nr;
	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_UT_ASSERT(rc == 0);
	fom->fo_phase = FOPH_FINISH;
	return rc;
}

/* It is the dummy fom state used by the bulk client to check the
 *  functionality.
 */
static int bulkio_fom_state(struct c2_fom *fom)
{
	int				 rc;
	uint32_t			 i;
	uint32_t			 j;
	uint32_t			 k;
	c2_bcount_t			 tc;
        struct c2_fop                   *fop;
	struct c2_clink			 clink;
	struct c2_net_buffer		**netbufs;
	struct c2_fop_cob_rw		*rw;
	struct c2_io_indexvec		*ivec;
	struct c2_rpc_bulk		*rbulk;
	struct c2_rpc_bulk_buf		*rbuf;
	struct c2_rpc_conn		*conn;
	struct c2_fop_cob_writev_rep	*wrep;
	struct c2_fop_cob_readv_rep	*rrep;

	conn = fom->fo_fop->f_item.ri_session->s_conn;
	rw = io_rw_get(fom->fo_fop);
	C2_UT_ASSERT(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);

	C2_ALLOC_ARR(netbufs, rw->crw_desc.id_nr);
	C2_UT_ASSERT(netbufs != NULL);

	C2_ALLOC_PTR(rbulk);
	C2_UT_ASSERT(rbulk != NULL);
	c2_rpc_bulk_init(rbulk);
	C2_UT_ASSERT(rw->crw_desc.id_nr != 0);
	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i)
		for (j = 0; j < rw->crw_ivecs.cis_ivecs[i].ci_nr; ++j)
			C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs[i].ci_iosegs[j].
			     ci_count == C2_0VEC_ALIGN);

	for (tc = 0, i = 0; i < rw->crw_desc.id_nr; ++i) {
		ivec = &rw->crw_ivecs.cis_ivecs[i];

		C2_ALLOC_PTR(netbufs[i]);
		C2_UT_ASSERT(netbufs[i] != NULL);

		vec_alloc(&netbufs[i]->nb_buffer, ivec->ci_nr,
			  ivec->ci_iosegs[0].ci_count);

		rc = c2_rpc_bulk_buf_add(rbulk, ivec->ci_nr,
					 conn->c_rpcmachine->cr_tm.ntm_dom,
					 netbufs[i], &rbuf);

		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(rbuf != NULL);

		rbuf->bb_nbuf->nb_qtype = c2_is_write_fop(fom->fo_fop) ?
					 C2_NET_QT_ACTIVE_BULK_RECV :
					 C2_NET_QT_ACTIVE_BULK_SEND;

		for (k = 0; k < ivec->ci_nr; ++k)
			tc += ivec->ci_iosegs[k].ci_count;

		if (c2_is_read_fop(fom->fo_fop)) {
			for (j = 0; j < ivec->ci_nr; ++j)
				/*
				 * Sets a pattern in data buffer so that
				 * it can be verified at other side.
				 */
				memset(netbufs[i]->nb_buffer.ov_buf[j], 'b',
				       ivec->ci_iosegs[j].ci_count);
		}
	}
	c2_clink_init(&clink, NULL);
	c2_clink_add(&rbulk->rb_chan, &clink);
	rc = c2_rpc_bulk_load(rbulk, conn, rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);
	c2_chan_wait(&clink);

	/* Makes sure that list of buffers in c2_rpc_bulk is empty. */
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	rc = rbulk->rb_rc;
	C2_UT_ASSERT(rc == 0);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	/* Checks if the write io bulk data is received as is. */
	for (i = 0; i < rw->crw_desc.id_nr &&
                c2_is_write_fop(fom->fo_fop); ++i) {
		for (j = 0; j < netbufs[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bp->bp_writebuf,
				    netbufs[i]->nb_buffer.ov_buf[j],
				    netbufs[i]->nb_buffer.ov_vec.v_count[j]);
			C2_UT_ASSERT(rc == 0);
		}
	}

	if (c2_is_write_fop(fom->fo_fop)) {
		fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
		wrep = c2_fop_data(fop);
		wrep->c_rep.rwr_rc = rbulk->rb_rc;
		wrep->c_rep.rwr_count = tc;
	} else {
		fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
		rrep = c2_fop_data(fop);
		rrep->c_rep.rwr_rc = rbulk->rb_rc;
		rrep->c_rep.rwr_count = tc;
	}
	C2_UT_ASSERT(fop != NULL);

	fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fop->f_item);
	C2_UT_ASSERT(rc == 0);

	fom->fo_phase = FOPH_FINISH;
	/* Deallocates net buffers and c2_bufvec structures. */
	for (i = 0; i < rw->crw_desc.id_nr; ++i) {
		c2_bufvec_free(&netbufs[i]->nb_buffer);
		c2_free(netbufs[i]);
	}
	c2_free(netbufs);
	c2_rpc_bulk_fini(rbulk);
	c2_free(rbulk);

	return rc;

}

static size_t bulkio_fom_locality(const struct c2_fom *fom)
{
	return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static struct c2_fom_ops bulkio_stob_create_fom_ops = {
	.fo_fini = bulkio_stob_fom_fini,
	.fo_state = bulkio_stob_create_fom_state,
	.fo_home_locality = bulkio_fom_locality,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops bulkio_server_write_fom_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_state = bulkio_server_write_fom_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops ut_io_fom_cob_rw_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_state = ut_io_fom_cob_rw_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops bulkio_server_read_fom_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_state = bulkio_server_read_fom_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
        .fo_service_name = c2_io_fom_cob_rw_service_name,
};

static struct c2_fom_ops bulkio_fom_ops = {
	.fo_fini = bulkio_fom_fini,
	.fo_state = bulkio_fom_state,
	.fo_home_locality = bulkio_fom_locality,
};

static int io_fop_stob_create_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	 rc = c2_io_fom_cob_rw_create(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type = bulkio_stob_create_fom_type;
	fom->fo_ops = &bulkio_stob_create_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static int io_fop_server_write_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	 rc = c2_io_fom_cob_rw_create(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type = bulkio_server_write_fom_type;
	fom->fo_ops = &bulkio_server_write_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

/*
 * This creates FOM for ut.
 */
static int ut_io_fom_cob_rw_create(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
        /*
         * Case : This tests the I/O FOM create api.
         *        It use real I/O FOP
         */
	rc = c2_io_fom_cob_rw_create(fop, &fom);
        C2_UT_ASSERT(rc == 0 &&
                     fom != NULL &&
                     fom->fo_rep_fop != NULL &&
                     fom->fo_fop != NULL &&
                     fom->fo_type != NULL &&
                     fom->fo_ops != NULL);

	fop->f_type->ft_fom_type = ut_io_fom_cob_rw_type_mopt;
	fom->fo_ops = &ut_io_fom_cob_rw_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static int io_fop_server_read_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	rc = c2_io_fom_cob_rw_create(fop, &fom);
        C2_UT_ASSERT(rc == 0);
	fop->f_type->ft_fom_type = bulkio_server_read_fom_type;
	fom->fo_ops = &bulkio_server_read_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static int io_fop_dummy_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom *fom;

        C2_ALLOC_PTR(fom);
	C2_UT_ASSERT(fom != NULL);

	fom->fo_fop = fop;
	c2_fom_init(fom);
	fop->f_type->ft_fom_type = bulkio_dummy_fom_type;
	fom->fo_ops = &bulkio_fom_ops;
        fom->fo_type = &bulkio_dummy_fom_type;

	*m = fom;
	return 0;

}

static void io_fids_init(void)
{
	int i;

	/* Populates fids. */
	for (i = 0; i < IO_FIDS_NR; ++i) {
		bp->bp_fids[i].f_seq = i;
		bp->bp_fids[i].f_oid = i;
	}
}

/*
 * Zero vector needs buffers aligned on 4k boundary.
 * Hence c2_bufvec_alloc can not be used.
 */
static void vec_alloc(struct c2_bufvec *bvec, uint32_t segs_nr,
		      c2_bcount_t seg_size)
{
	uint32_t i;

	bvec->ov_vec.v_nr = segs_nr;
	C2_ALLOC_ARR(bvec->ov_vec.v_count, segs_nr);
	C2_UT_ASSERT(bvec->ov_vec.v_count != NULL);
	C2_ALLOC_ARR(bvec->ov_buf, segs_nr);
	C2_UT_ASSERT(bvec->ov_buf != NULL);

	for (i = 0; i < segs_nr; ++i) {
		bvec->ov_buf[i] = c2_alloc_aligned(C2_0VEC_ALIGN,
						   C2_0VEC_SHIFT);
		C2_UT_ASSERT(bvec->ov_buf[i] != NULL);
		bvec->ov_vec.v_count[i] = C2_0VEC_ALIGN;
	}
}

static void io_buffers_allocate(void)
{
	int i;

	/* Initialized the standard buffer with a data pattern for read IO. */
	memset(bp->bp_readbuf, 'b', C2_0VEC_ALIGN);
	memset(bp->bp_writebuf, 'a', C2_0VEC_ALIGN);

	for (i = 0; i < IO_FOPS_NR; ++i)
		vec_alloc(&bp->bp_iobuf[i]->nb_buffer, IO_SEGS_NR,
			  C2_0VEC_ALIGN);
}

static void io_buffers_deallocate(void)
{
	int i;

	for (i = 0; i < IO_FOPS_NR; ++i)
		c2_bufvec_free(&bp->bp_iobuf[i]->nb_buffer);
}

static void io_fop_populate(int index, uint64_t off_index,
			    struct c2_io_fop **io_fops, int segs_nr)
{
	int			 i;
	int			 rc;
	struct c2_io_fop	*iofop;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*rw;

	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;

	/*
	 * Adds a c2_rpc_bulk_buf structure to list of such structures
	 * in c2_rpc_bulk.
	 */
	rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, &bp->bp_cnetdom, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	rw = io_rw_get(&iofop->if_fop);
	rw->crw_fid = bp->bp_fids[off_index];

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < segs_nr; ++i) {
		rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				bp->bp_iobuf[index]->nb_buffer.ov_buf[i],
				bp->bp_iobuf[index]->nb_buffer.ov_vec.
				v_count[i],
				bp->bp_offsets[off_index], &bp->bp_cnetdom);
		C2_UT_ASSERT(rc == 0);

		bp->bp_offsets[off_index] +=
			bp->bp_iobuf[index]->nb_buffer.ov_vec.v_count[i];
	}

	/*
	 * Allocates memory for array of net buf descriptors and array of
	 * index vectors from io fop.
	 */
	rc = c2_io_fop_prepare(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rw->crw_desc.id_nr ==
		     c2_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist));
	C2_UT_ASSERT(rw->crw_desc.id_descs != NULL);

	/*
	 * Stores the net buf desc/s after adding the corresponding
	 * net buffers to transfer machine to io fop wire format.
	 */
	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

}

static void io_fops_create(enum C2_RPC_OPCODES op, int fids_nr, int fops_nr,
			   int segs_nr)
{
	int			  i;
	int			  rc;
	uint64_t		  seed;
	uint64_t		  rnd;
	struct c2_fop_type	 *fopt;
	struct c2_io_fop	**io_fops;

	seed = 0;
	for (i = 0; i < fids_nr; ++i)
		bp->bp_offsets[i] = IO_SEG_START_OFFSET;

	if (op == C2_IOSERVICE_WRITEV_OPCODE) {
		C2_UT_ASSERT(bp->bp_wfops == NULL);
		C2_ALLOC_ARR(bp->bp_wfops, fops_nr);
		fopt = &c2_fop_cob_writev_fopt;
		io_fops = bp->bp_wfops;
	} else {
		C2_UT_ASSERT(bp->bp_rfops == NULL);
		C2_ALLOC_ARR(bp->bp_rfops, fops_nr);
		fopt = &c2_fop_cob_readv_fopt;
		io_fops = bp->bp_rfops;
	}
	C2_UT_ASSERT(io_fops != NULL);

	/* Allocates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		C2_ALLOC_PTR(io_fops[i]);
		C2_UT_ASSERT(io_fops[i] != NULL);
		rc = c2_io_fop_init(io_fops[i], fopt);
		C2_UT_ASSERT(rc == 0);
		/* removed this actual integration */
	}

	/* Populates io fops. */
	for (i = 0; i < fops_nr; ++i) {
		if (fids_nr < fops_nr) {
			rnd = c2_rnd(fids_nr, &seed);
			C2_UT_ASSERT(rnd < fids_nr);
		}
		else rnd = i;

		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
			   bp->bp_rfops;
		io_fop_populate(i, rnd, io_fops, segs_nr);
	}
}

static void io_fops_destroy(void)
{
	c2_free(bp->bp_rfops);
	c2_free(bp->bp_wfops);
	bp->bp_rfops = NULL;
	bp->bp_wfops = NULL;
}

static void io_fops_rpc_submit(struct thrd_arg *t)
{
	int			  i;
	int			  j;
	int			  rc;
	c2_time_t		  timeout;
	struct c2_clink		  clink;
	struct c2_rpc_item	 *item;
	struct c2_rpc_bulk	 *rbulk;
	struct c2_io_fop	**io_fops;

	i = t->ta_index;
	io_fops = (t->ta_op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
		  bp->bp_rfops;
	rbulk = c2_fop_to_rpcbulk(&io_fops[i]->if_fop);
	item = &io_fops[i]->if_fop.f_item;
	item->ri_session = &c_rctx.rcx_session;
	c2_time_set(&timeout, IO_RPC_ITEM_TIMEOUT, 0);

	/*
	 * Initializes and adds a clink to rpc item channel to wait for
	 * reply.
	 */
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	timeout = c2_time_add(timeout, c2_time_now());
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	/* Posts the rpc item and waits until reply is received. */
	rc = c2_rpc_post(item);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_reply_timedwait(&clink, timeout);
	C2_UT_ASSERT(rc == 0);

	if (rc == 0 && c2_is_read_fop(&io_fops[i]->if_fop)) {
		for (j = 0; j < bp->bp_iobuf[i]->nb_buffer.ov_vec.v_nr; ++j) {
			rc = memcmp(bp->bp_iobuf[i]->nb_buffer.ov_buf[j],
				    bp->bp_readbuf,
				    bp->bp_iobuf[i]->nb_buffer.ov_vec.
				    	v_count[j]);
			C2_UT_ASSERT(rc == 0);
			memset(bp->bp_iobuf[i]->nb_buffer.ov_buf[j], 'a',
			       C2_0VEC_ALIGN);
		}
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_UT_ASSERT(rbulk->rb_rc == 0);
		c2_mutex_unlock(&rbulk->rb_mutex);
	}
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}

void bulkio_stob_create(void)
{
	struct c2_fop_cob_rw	*rw;
	enum C2_RPC_OPCODES	 op;
	struct thrd_arg		 targ[IO_FIDS_NR];
	int			 i;
	int			 rc;

	op = C2_IOSERVICE_WRITEV_OPCODE;
	C2_ALLOC_ARR(bp->bp_wfops, IO_FIDS_NR);
	for (i = 0; i < IO_FIDS_NR; ++i) {
		C2_ALLOC_PTR(bp->bp_wfops[i]);
                rc = c2_io_fop_init(bp->bp_wfops[i], &c2_fop_cob_writev_fopt);
                bp->bp_wfops[i]->if_fop.f_type->ft_fom_type =
                bulkio_stob_create_fom_type;

		rw = io_rw_get(&bp->bp_wfops[i]->if_fop);
		bp->bp_wfops[i]->if_fop.f_type->ft_ops = &bulkio_stob_create_ops;
		rw->crw_fid = bp->bp_fids[i];
		targ[i].ta_index = i;
		targ[i].ta_op = op;
		io_fops_rpc_submit(&targ[i]);
	}
	io_fops_destroy();

}

void bulkio_server_single_read_write(void)
{
	int		    j;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type = c2_io_fom_cob_rw_mopt;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type = c2_io_fom_cob_rw_mopt;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
	io_fops_destroy();
}

void bulkio_server_read_write_state_test(void)
{
	int		    j;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type = bulkio_server_write_fom_type;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type = bulkio_server_read_fom_type;
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
	io_fops_destroy();
}

/*
 * This function sends write & read fop to UT FOM to check
 * state transition for I/O FOM.
 */

void bulkio_server_rw_state_transition_test(void)
{
	int		    j;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type = ut_io_fom_cob_rw_type_mopt;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(op, 1, 1, IO_SEGS_NR);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type = ut_io_fom_cob_rw_type_mopt;
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
	io_fops_destroy();
}

void bulkio_server_multiple_read_write(void)
{
	int		     rc;
	int		     i;
	int		     j;
	enum C2_RPC_OPCODES  op;
	struct thrd_arg      targ[IO_FOPS_NR];
	struct c2_io_fop   **io_fops;
	struct c2_bufvec   *buf;

	for (i = 0; i < IO_FOPS_NR; ++i) {
		buf = &bp->bp_iobuf[i]->nb_buffer;
		for (j = 0; j < IO_SEGS_NR; ++j) {
			memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
		}
	}
	for (op = C2_IOSERVICE_WRITEV_OPCODE; op >= C2_IOSERVICE_READV_OPCODE;
	  --op) {
		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(op, IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);
		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops : bp->bp_rfops;
		for (i = 0; i < IO_FOPS_NR; ++i) {
			io_fops[i]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
                        if (op == C2_IOSERVICE_WRITEV_OPCODE)
                                io_fops[i]->if_fop.f_type->ft_fom_type =
				bulkio_server_write_fom_type;
                        else
                                io_fops[i]->if_fop.f_type->ft_fom_type =
				bulkio_server_read_fom_type;

			targ[i].ta_index = i;
			targ[i].ta_op = op;
			C2_SET0(bp->bp_threads[i]);
			rc = C2_THREAD_INIT(bp->bp_threads[i],
					    struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_UT_ASSERT(rc == 0);
		}
		/* Waits till all threads finish their job. */
		for (i = 0; i < IO_FOPS_NR; ++i) {
			c2_thread_join(bp->bp_threads[i]);
			buf = &bp->bp_iobuf[i]->nb_buffer;
			for (j = 0; j < IO_SEGS_NR; ++j) {
				memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
			}
		}
	}
	io_fops_destroy();
}

void fop_create_populate(int index, enum C2_RPC_OPCODES op, int buf_nr)
{
	struct c2_io_fop       **io_fops;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_rpc_bulk	*rbulk;
	struct c2_io_fop	*iofop;
	struct c2_fop_cob_rw	*rw;
	int                      i;
	int			 j;
	int			 rc;

	if (op == C2_IOSERVICE_WRITEV_OPCODE)
		C2_ALLOC_ARR(bp->bp_wfops, IO_FOPS_NR);
	else
		C2_ALLOC_ARR(bp->bp_rfops, IO_FOPS_NR);

	io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops : bp->bp_rfops;
	for (i = 0; i < IO_FOPS_NR; ++i)
		C2_ALLOC_PTR(io_fops[i]);

	if (op == C2_IOSERVICE_WRITEV_OPCODE)
                rc = c2_io_fop_init(io_fops[index], &c2_fop_cob_writev_fopt);
        else
                rc = c2_io_fop_init(io_fops[index], &c2_fop_cob_readv_fopt);
	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;
	rw = io_rw_get(&io_fops[index]->if_fop);

	bp->bp_offsets[0] = IO_SEG_START_OFFSET;

	void add_buffer_bulk(int j) {
	/*
	 * Adds a c2_rpc_bulk_buf structure to list of such structures
	 * in c2_rpc_bulk.
	 */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, &bp->bp_cnetdom, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	/* Adds io buffers to c2_rpc_bulk_buf structure. */
	for (i = 0; i < IO_SEGS_NR; ++i) {
                rc = c2_rpc_bulk_buf_databuf_add(rbuf,
				bp->bp_iobuf[j]->nb_buffer.ov_buf[i],
				bp->bp_iobuf[j]->nb_buffer.ov_vec.v_count[i],
				bp->bp_offsets[0], &bp->bp_cnetdom);
		C2_UT_ASSERT(rc == 0);
		bp->bp_offsets[0] +=
			bp->bp_iobuf[j]->nb_buffer.ov_vec.v_count[i];
	}

	rbuf->bb_nbuf->nb_qtype = (op == C2_IOSERVICE_WRITEV_OPCODE) ?
		C2_NET_QT_PASSIVE_BULK_SEND : C2_NET_QT_PASSIVE_BULK_RECV;
	}

	for (j = 0; j < buf_nr; ++j)
		add_buffer_bulk(j);

	/*
	 * Allocates memory for array of net buf descriptors and array of
	 * index vectors from io fop.
	 */
	rc = c2_io_fop_prepare(&iofop->if_fop);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Stores the net buf desc/s after adding the corresponding
	 * net buffers to transfer machine to io fop wire format.
	 */
	rc = c2_rpc_bulk_store(rbulk, &c_rctx.rcx_connection,
			       rw->crw_desc.id_descs);
	C2_UT_ASSERT(rc == 0);

	for (i = 0; i < IO_FIDS_NR; ++i)
		bp->bp_offsets[i] = IO_SEG_START_OFFSET;
}

void bulkio_server_read_write_multiple_nb(void)
{
	int		    i;
	int		    j;
	int		    buf_nr;
	enum C2_RPC_OPCODES op;
	struct thrd_arg     targ;
	struct c2_bufvec   *buf;

	buf_nr = IO_FOPS_NR;
	for (i = 0; i < buf_nr; ++i) {
		buf = &bp->bp_iobuf[i]->nb_buffer;
		for (j = 0; j < IO_SEGS_NR; ++j) {
			memset(buf->ov_buf[j], 'b', IO_SEG_SIZE);
		}
	}
	op = C2_IOSERVICE_WRITEV_OPCODE;
	fop_create_populate(0, op, buf_nr);
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);

	for (i = 0; i < buf_nr; ++i) {
		buf = &bp->bp_iobuf[i]->nb_buffer;
		for (j = 0; j < IO_SEGS_NR; ++j) {
			memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
		}
	}
	op = C2_IOSERVICE_READV_OPCODE;
	fop_create_populate(0, op, buf_nr);
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	io_fops_rpc_submit(&targ);
	io_fops_destroy();
}

static void bulkio_test(int fids_nr, int fops_nr, int segs_nr)
{
	int		    rc;
	int		    i;
	enum C2_RPC_OPCODES op;
	struct thrd_arg	   *targ;
	struct c2_io_fop  **io_fops;

	C2_ALLOC_ARR(targ, fops_nr);
	C2_UT_ASSERT(targ != NULL);

	for (op = C2_IOSERVICE_READV_OPCODE; op <= C2_IOSERVICE_WRITEV_OPCODE;
	     ++op) {
		C2_UT_ASSERT(op == C2_IOSERVICE_READV_OPCODE ||
			     op == C2_IOSERVICE_WRITEV_OPCODE);

		/*
		 * IO fops are deallocated by an rpc item type op on receiving
		 * the reply fop. See io_item_free().
		 */
		io_fops_create(op, fids_nr, fops_nr, segs_nr);
		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops : bp->bp_rfops;
		for (i = 0; i < fops_nr; ++i) {
			io_fops[i]->if_fop.f_type->ft_ops = &bulkio_fop_ut_ops;
                        io_fops[i]->if_fop.f_type->ft_fom_type =
			bulkio_dummy_fom_type;
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			C2_SET0(bp->bp_threads[i]);
			rc = C2_THREAD_INIT(bp->bp_threads[i],
					    struct thrd_arg *,
					    NULL, &io_fops_rpc_submit,
					    &targ[i], "io_thrd");
			C2_UT_ASSERT(rc == 0);
		}

		/* Waits till all threads finish their job. */
		for (i = 0; i < fops_nr; ++i)
			c2_thread_join(bp->bp_threads[i]);
	}
	c2_free(targ);
	io_fops_destroy();
}

static void bulkio_single_rw(void)
{
	/* Sends only one fop for read and write IO. */
	bulkio_test(IO_FID_SINGLE, IO_FOP_SINGLE, IO_SEGS_NR);
}

static void bulkio_rwv(void)
{
	/* Sends multiple fops with multiple segments and multiple fids. */
	bulkio_test(IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);
}

static void bulkio_params_init(struct bulkio_params *b)
{
	int  i;
	int  rc;
	char addr[]	 = "127.0.0.1:23134:2";
	char cdbname[]	 = "bulk_c_db";
	char sdbfile[]	 = "bulkio_ut.db";
	char slogfile[]  = "bulkio_ut.log";
	char sstobfile[] = "bulkio_ut_stob";

	C2_UT_ASSERT(b != NULL);

	/* Initialize fids and allocate buffers used for bulk transfer. */
	io_fids_init();

	C2_UT_ASSERT(b->bp_iobuf == NULL);
	C2_ALLOC_ARR(b->bp_iobuf, IO_FOPS_NR);
	C2_UT_ASSERT(b->bp_iobuf != NULL);

	C2_UT_ASSERT(b->bp_threads == NULL);
	C2_ALLOC_ARR(b->bp_threads, IO_FOPS_NR);
	C2_UT_ASSERT(b->bp_threads != NULL);
	for (i = 0; i < IO_FOPS_NR; ++i) {
		C2_ALLOC_PTR(b->bp_iobuf[i]);
		C2_UT_ASSERT(b->bp_iobuf[i] != NULL);
		C2_ALLOC_PTR(b->bp_threads[i]);
		C2_UT_ASSERT(b->bp_threads[i] != NULL);
	}

	C2_UT_ASSERT(b->bp_readbuf == NULL);
	C2_ALLOC_ARR(b->bp_readbuf, C2_0VEC_ALIGN);
	C2_UT_ASSERT(b->bp_readbuf != NULL);

	C2_UT_ASSERT(b->bp_writebuf == NULL);
	C2_ALLOC_ARR(b->bp_writebuf, C2_0VEC_ALIGN);
	C2_UT_ASSERT(b->bp_writebuf != NULL);

	io_buffers_allocate();

	b->bp_xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_domain_init(&b->bp_cnetdom, b->bp_xprt);
	C2_UT_ASSERT(rc == 0);

	memcpy(b->bp_sdbfile, sdbfile, sizeof sdbfile);
	memcpy(b->bp_sstobfile, sstobfile, sizeof sstobfile);
	memcpy(b->bp_slogfile, slogfile, sizeof slogfile);
	memcpy(b->bp_cendpaddr, addr, sizeof addr);
	memcpy(b->bp_cdbname, cdbname, sizeof cdbname);

	/* Starts a colibri server. */
	s_rctx.rsx_xprts = &bp->bp_xprt;
	s_rctx.rsx_log_file_name = bp->bp_slogfile;
	b->bp_sctx = &s_rctx;
	rc = c2_rpc_server_start(b->bp_sctx);
	C2_UT_ASSERT(rc == 0);

	/* Starts an rpc client. */
	c_rctx.rcx_net_dom = &bp->bp_cnetdom;
	c_rctx.rcx_local_addr = bp->bp_cendpaddr;
	c_rctx.rcx_db_name = bp->bp_cdbname;
	c_rctx.rcx_dbenv = &bp->bp_cdbenv;
	c_rctx.rcx_cob_dom = &bp->bp_ccbdom;

	b->bp_cctx = &c_rctx;
	rc = c2_rpc_client_init(b->bp_cctx);
	C2_UT_ASSERT(rc == 0);

	for (i = 0; i < IO_FIDS_NR; ++i)
		b->bp_offsets[i] = IO_SEG_START_OFFSET;

	b->bp_rfops = NULL;
	b->bp_wfops = NULL;
}

static void bulkio_params_fini()
{
	int i;
	int rc;

	C2_UT_ASSERT(bp != NULL);

	rc = c2_rpc_client_fini(bp->bp_cctx);
	C2_UT_ASSERT(rc == 0);

	c2_rpc_server_stop(bp->bp_sctx);
	c2_net_domain_fini(&bp->bp_cnetdom);
	C2_UT_ASSERT(bp->bp_iobuf != NULL);
	io_buffers_deallocate();
	for (i = 0; i < IO_FOPS_NR; ++i) {
		c2_free(bp->bp_iobuf[i]);
		c2_free(bp->bp_threads[i]);
	}
	c2_free(bp->bp_iobuf);
	c2_free(bp->bp_threads);

	C2_UT_ASSERT(bp->bp_readbuf != NULL);
	c2_free(bp->bp_readbuf);
	C2_UT_ASSERT(bp->bp_writebuf != NULL);
	c2_free(bp->bp_writebuf);

	C2_UT_ASSERT(bp->bp_rfops == NULL);
	C2_UT_ASSERT(bp->bp_wfops == NULL);
}

static void bulkio_init(void)
{
	struct bulkio_params *bulkp;

	C2_ALLOC_PTR(bulkp);
	C2_UT_ASSERT(bulkp != NULL);

	bp = bulkp;

	bulkio_params_init(bp);

	bulkio_stob_create();
	c2_addb_choose_default_level(AEL_NONE);
}

static void bulkio_fini(void)
{
	bulkio_params_fini();
	c2_free(bp);
}

static void bulkioapi_test(void)
{
	int			 rc;
	char			*sbuf;
	int32_t			 max_segs;
	c2_bcount_t		 max_seg_size;
	c2_bcount_t		 max_buf_size;
	struct c2_io_fop	 iofop;
	struct c2_net_xprt	*xprt;
	struct c2_rpc_bulk	*rbulk;
	struct c2_fop_cob_rw	*rw;
	struct c2_net_domain	 nd;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_rpc_bulk_buf	*rbuf1;

	C2_SET0(&iofop);
	C2_SET0(&nd);

	xprt = &c2_net_bulk_sunrpc_xprt;
	rc = c2_net_domain_init(&nd, xprt);
	C2_UT_ASSERT(rc == 0);

	/* Test : c2_io_fop_init() */
	rc = c2_io_fop_init(&iofop, &c2_fop_cob_writev_fopt);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(iofop.if_magic == C2_IO_FOP_MAGIC);
	C2_UT_ASSERT(iofop.if_fop.f_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_type != NULL);
	C2_UT_ASSERT(iofop.if_fop.f_item.ri_ops  != NULL);

	C2_UT_ASSERT(iofop.if_rbulk.rb_buflist.t_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_magic == C2_RPC_BULK_MAGIC);
	C2_UT_ASSERT(iofop.if_rbulk.rb_bytes == 0);
	C2_UT_ASSERT(iofop.if_rbulk.rb_rc    == 0);

	/* Test : c2_fop_to_rpcbulk() */
	rbulk = c2_fop_to_rpcbulk(&iofop.if_fop);
	C2_UT_ASSERT(rbulk != NULL);
	C2_UT_ASSERT(rbulk == &iofop.if_rbulk);

	/* Test : c2_rpc_bulk_buf_add() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);

	/* Test : c2_rpc_bulk_buf structure. */
	C2_UT_ASSERT(c2_tlink_is_in(&rpcbulk_tl, rbuf));
	C2_UT_ASSERT(rbuf->bb_magic == C2_RPC_BULK_BUF_MAGIC);
	C2_UT_ASSERT(rbuf->bb_rbulk == rbulk);
	C2_UT_ASSERT(rbuf->bb_nbuf  != NULL);

	/*
	 * Since no external net buffer was passed to c2_rpc_bulk_buf_add(),
	 * it should allocate a net buffer internally and c2_rpc_bulk_buf::
	 * bb_flags should be C2_RPC_BULK_NETBUF_ALLOCATED.
	 */
	C2_UT_ASSERT(rbuf->bb_flags == C2_RPC_BULK_NETBUF_ALLOCATED);

	/* Test : c2_rpc_bulk_buf_add() - Error case. */
	max_segs = c2_net_domain_get_max_buffer_segments(&nd);
	rc = c2_rpc_bulk_buf_add(rbulk, max_segs + 1, &nd, NULL, &rbuf1);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buf_databuf_add(). */
	sbuf = c2_alloc_aligned(C2_0VEC_ALIGN, C2_0VEC_SHIFT);
	C2_UT_ASSERT(sbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     C2_0VEC_ALIGN);
	C2_UT_ASSERT(c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) ==
		     c2_vec_count(&rbuf->bb_nbuf->nb_buffer.ov_vec));

	/* Test : c2_rpc_bulk_buf_databuf_add() - Error case. */
	max_seg_size = c2_net_domain_get_max_buffer_segment_size(&nd);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, max_seg_size + 1, 0, &nd);
	/* Segment size bigger than permitted segment size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);

	max_buf_size = c2_net_domain_get_max_buffer_size(&nd);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, max_buf_size + 1, 0, &nd);
	/* Max buffer size greater than permitted max buffer size. */
	C2_UT_ASSERT(rc == -EMSGSIZE);

	/* Test : c2_rpc_bulk_buflist_empty() */
	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Test : c2_rpc_bulk_store() */
	rc = c2_rpc_bulk_buf_add(rbulk, IO_FID_SINGLE, &nd, NULL, &rbuf);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(rbuf != NULL);
	rc = c2_rpc_bulk_buf_databuf_add(rbuf, sbuf, C2_0VEC_ALIGN, 0, &nd);
	C2_UT_ASSERT(rc == 0);

	/*
	 * There is no ACTIVE side to start the bulk transfer and hence the
	 * buffer is guaranteed to stay put in PASSIVE_BULK_SEND queue of TM.
	 */
	C2_UT_ASSERT(rbuf->bb_nbuf != NULL);
	rbuf->bb_nbuf->nb_qtype = C2_NET_QT_PASSIVE_BULK_SEND;
	rc = c2_io_fop_prepare(&iofop.if_fop);
	C2_UT_ASSERT(rc == 0);
	rw = io_rw_get(&iofop.if_fop);
	C2_UT_ASSERT(rw != NULL);

	rw = io_rw_get(&iofop.if_fop);
	c2_io_fop_destroy(&iofop.if_fop);
	C2_UT_ASSERT(rw->crw_desc.id_descs   == NULL);
	C2_UT_ASSERT(rw->crw_desc.id_nr      == 0);
	C2_UT_ASSERT(rw->crw_ivecs.cis_ivecs == NULL);
	C2_UT_ASSERT(rw->crw_ivecs.cis_nr    == 0);

	c2_rpc_bulk_buflist_empty(rbulk);
	C2_UT_ASSERT(c2_tlist_is_empty(&rpcbulk_tl, &rbulk->rb_buflist));

	/* Cleanup. */
	c2_free(sbuf);

	c2_io_fop_fini(&iofop);
	c2_net_domain_fini(&nd);
}

const struct c2_test_suite bulkio_ut = {
	.ts_name = "bulkio-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		/*
		 * Intentionally kept as first test case. It initializes
		 * all necessary data for sending IO fops. Keeping
		 * bulkio_init() as .ts_init requires changing all
		 * C2_UT_ASSERTS to C2_ASSERTS.
		 */
		{ "bulkio_init",	  bulkio_init},
		{ "bulkioapi_test",	  bulkioapi_test},
		{ "bulkio_single_fop_rw", bulkio_single_rw},
		{ "bulkio_vectored_rw",   bulkio_rwv},
		{ "bulkio_server_single_read_write",
		   bulkio_server_single_read_write},
		{ "bulkio_server_read_write_state_test",
		   bulkio_server_read_write_state_test},
		{ "bulkio_server_vectored_read_write",
		   bulkio_server_multiple_read_write},
		{ "bulkio_server_rw_multiple_nb_server",
		   bulkio_server_read_write_multiple_nb},
		{ "bulkio_server_rw_state_transition_test",
		   bulkio_server_rw_state_transition_test},
		{ "bulkio_fini",	  bulkio_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_ut);
