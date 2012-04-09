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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratec.com>
 * Original creation date: 09/29/2011
 */

#include <sys/stat.h>
#include <sys/types.h>

#include "lib/processor.h"
#include "lib/ut.h"
#include "bulkio_common.h"
#include "ioservice/io_fops.c"	/* To access static APIs. */
#include "ioservice/io_foms.c"	/* To access static APIs. */

static void bulkio_init();
static void bulkio_fini();

struct bulkio_params *bp;
extern void bulkioapi_test(void);
static int io_fop_server_write_fom_create(struct c2_fop *fop,
					  struct c2_fom **m);
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
static void wait_dummy(struct c2_fom *fom)
{
        struct c2_fom_locality *loc;

        loc = fom->fo_loc;

        c2_mutex_lock(&loc->fl_lock);
        c2_list_add_tail(&loc->fl_wail, &fom->fo_linkage);
        C2_CNT_INC(loc->fl_wail_nr);
        c2_mutex_unlock(&loc->fl_lock);
}

/* This function is used to bypass request handler while testing.*/
static bool cb_dummy(struct c2_clink *clink)
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

/** AST callback */
static void cb_dummy2(struct c2_fom_callback *cb)
{
        struct c2_fom_locality  *loc;
        struct c2_fom           *fom;
        struct c2_io_fom_cob_rw  *fom_obj;

        C2_PRE(cb != NULL);

        fom = cb->fc_fom;
        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        --fom_obj->fcrw_num_stobio_launched;
        loc = fom->fo_loc;
        C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_linkage));
        c2_list_del(&fom->fo_linkage);
        C2_CNT_DEC(loc->fl_wail_nr);
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
	case C2_FOPH_IO_FOM_BUFFER_ACQUIRE :
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT ||
                fom->fo_phase == C2_FOPH_IO_ZERO_COPY_INIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_ZERO_COPY_WAIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_STOB_INIT);
		break;
	case C2_FOPH_IO_STOB_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_STOB_WAIT);
		break;
	case C2_FOPH_IO_STOB_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_BUFFER_RELEASE);
		break;
	case C2_FOPH_IO_BUFFER_RELEASE:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase == C2_FOPH_SUCCESS ||
                fom->fo_phase == C2_FOPH_IO_FOM_BUFFER_ACQUIRE);
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
	case C2_FOPH_IO_FOM_BUFFER_ACQUIRE :
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT ||
                fom->fo_phase == C2_FOPH_IO_STOB_INIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_ZERO_COPY_WAIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_BUFFER_RELEASE);
		break;
	case C2_FOPH_IO_STOB_INIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_STOB_WAIT);
		break;
	case C2_FOPH_IO_STOB_WAIT:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(fom->fo_phase == C2_FOPH_IO_ZERO_COPY_INIT);
		break;
	case C2_FOPH_IO_BUFFER_RELEASE:
		rc = c2_io_fom_cob_rw_state(fom);
                C2_UT_ASSERT(
                fom->fo_phase == C2_FOPH_SUCCESS ||
                fom->fo_phase == C2_FOPH_IO_FOM_BUFFER_ACQUIRE);
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
        uint32_t                      colour;
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
        struct c2_net_transfer_mc     tm;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);

        tm = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        colour = c2_net_tm_colour_get(&tm);

        /*
         * No need to test generic phases.
         */
        if (fom->fo_phase < C2_FOPH_NR)
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
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 02 : No network buffer is available with the buffer pool.
         *         Even after getting buffer pool not-empty event, buffers are
         *         not available in pool (which could be used by other FOMs
         *         in the server).
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & rstore FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 03 : Network buffer is available with the buffer pool.
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
         */
        c2_net_buffer_pool_lock(bp);
        c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(bp);

        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_ZERO_COPY_INIT);

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
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_ZERO_COPY_INIT);

        /*
         * No need to cleanup here, since FOM will be  transitioned to
         * expected phase.
         */

        /*
         * Case 05 : Zero-copy failure
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: C2_FOPH_FAILURE
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

        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & restore FOM for next test. */
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        saved_segments_count;
        c2_rpc_bulk_fini(&fom_obj->fcrw_bulk);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 06 : Zero-copy success
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &cb_dummy;
        wait_dummy(fom);

        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase == C2_FOPH_IO_ZERO_COPY_WAIT);

        /*
         * Cleanup & make clean FOM for next test.
         */
        while (fom->fo_loc->fl_wail_nr > 0) {
                sleep(1);
        }
        c2_clink_del(&fom->fo_clink);

        /*
         * Case 07 : Zero-copy failure
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */
        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_WAIT;
        fom_obj->fcrw_bulk.rb_rc  = -1;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_bulk.rb_rc  = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 08 : Zero-copy success from wait state.
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: C2_FOPH_IO_STOB_INIT
         */
        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_STOB_INIT);

        /*
         * Case 09 : STOB I/O launch failure
         *         Input phase          : C2_FOPH_IO_STOB_INIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */

        /* Save original fid and pass invialid fid to make I/O launch fail.*/
        saved_fid = rwfop->crw_fid;
        invalid_fid.f_seq = 111;
        invalid_fid.f_oid = 222;

        rwfop->crw_fid = invalid_fid;

        fom->fo_phase = C2_FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 && rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_fid = saved_fid;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 10 : STOB I/O launch success
         *         Input phase          : C2_FOPH_IO_STOB_INIT
         *         Expected Output phase: C2_FOPH_IO_STOB_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom_obj->fcrw_fc_bottom = &cb_dummy2;
        wait_dummy(fom);

        fom->fo_phase =  C2_FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == C2_FSO_WAIT  &&
                     fom->fo_phase == C2_FOPH_IO_STOB_WAIT);

        /*
         * Cleanup & make clean FOM for next test.
         */
        while (fom->fo_loc->fl_wail_nr > 0) {
                c2_sm_group_unlock(&fom->fo_loc->fl_group);
                sleep(1);
                c2_sm_group_lock(&fom->fo_loc->fl_group);
        }

        /*
         * Case 11 : STOB I/O failure from wait state.
         *         Input phase          : C2_FOPH_IO_STOB_WAIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */

	/*
         * To test this case there is a need to invalidate stobio descriptor,
         * since io_finish() removes the stobio descriptor
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
        fom->fo_phase =  C2_FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 && rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

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

        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 12 : STOB I/O success
         *         Input phase          : C2_FOPH_IO_STOB_WAIT
         *         Expected Output phase: C2_FOPH_IO_BUFFER_RELEASE
         */
        fom->fo_phase =  C2_FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_BUFFER_RELEASE);

        /*
         * Case 13 : Processing of remaining buffer descriptors.
         *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         */
        fom->fo_phase = C2_FOPH_IO_BUFFER_RELEASE;

        saved_ndesc = fom_obj->fcrw_ndesc;
        fom_obj->fcrw_ndesc = 2;
        rwfop->crw_desc.id_nr = 2;
        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_ndesc = saved_ndesc;
        rwfop->crw_desc.id_nr = saved_ndesc;

        /*
         * Case 14 : All buffer descriptors are processed.
         *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: C2_FOPH_SUCCESS
         */
        fom->fo_phase = C2_FOPH_IO_BUFFER_RELEASE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_SUCCESS);

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
        uint32_t                      colour;
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
        struct c2_net_transfer_mc     tm;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);

        tm = fop->f_item.ri_session->s_conn->c_rpcmachine->cr_tm;
        colour = c2_net_tm_colour_get(&tm);

        /*
         * No need to test generic phases.
         */
        if (fom->fo_phase < C2_FOPH_NR)
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
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == C2_FSO_WAIT  &&
                     fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 02 : No network buffer is available with buffer pool.
         *         Even after getting buffer pool not-empty event, buffers are
         *         not available in pool (which could be used by other FOMs
         *         in the server).
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

        /* Cleanup & make clean FOM for next test. */
        c2_clink_del(&fom->fo_clink);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 03 : Network buffer is available with the buffer pool.
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         *         Expected Output phase: C2_FOPH_IO_STOB_INIT
         */
        c2_net_buffer_pool_lock(bp);
        c2_net_buffer_pool_put(bp, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(bp);

        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_ACQUIRE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 && rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_STOB_INIT);

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
         *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
         *         Expected Output phase: C2_FOPH_IO_STOB_INIT
         */
        fom->fo_phase =  C2_FOPH_IO_FOM_BUFFER_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_STOB_INIT);
        /* No need to cleanup here, since FOM will transitioned to expected
         *  phase.
	 */

        /*
         * Case 05 : STOB I/O launch failure
         *         Input phase          : C2_FOPH_IO_STOB_INIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */

        /* Save original fid and pass invalid fid to make I/O launch fail.*/
        saved_fid = rwfop->crw_fid;
        invalid_fid.f_seq = 111;
        invalid_fid.f_oid = 222;

        rwfop->crw_fid = invalid_fid;

        fom->fo_phase = C2_FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_fid = saved_fid;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 06 : STOB I/O launch success
         *         Input phase          : C2_FOPH_IO_STOB_INIT
         *         Expected Output phase: C2_FOPH_IO_STOB_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom_obj->fcrw_fc_bottom = &cb_dummy2;
        wait_dummy(fom);

        fom->fo_phase =  C2_FOPH_IO_STOB_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase == C2_FOPH_IO_STOB_WAIT);

        /*
         * Cleanup & restore FOM for next test.
         */
        while (fom->fo_loc->fl_wail_nr > 0) {
                c2_sm_group_unlock(&fom->fo_loc->fl_group);
                sleep(1);
                c2_sm_group_lock(&fom->fo_loc->fl_group);
        }

        /*
         * Case 07 : STOB I/O failure
         *         Input phase          : C2_FOPH_IO_STOB_WAIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */
        /*
         * To test this case there is a need to invalidate stobio descriptor,
         * since io_finish() remove stobio descriptor
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
        fom->fo_phase =  C2_FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

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

        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 08 : STOB I/O success
         *         Input phase          : C2_FOPH_IO_STOB_WAIT
         *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
         */
        fom->fo_phase =  C2_FOPH_IO_STOB_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_ZERO_COPY_INIT);

        /*
         * Case 09 : Zero-copy failure
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: C2_FOPH_FAILURE
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

        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        rwfop->crw_ivecs.cis_ivecs[fom_obj->fcrw_curr_desc_index].ci_nr =
        saved_segments_count;
        c2_rpc_bulk_fini(&fom_obj->fcrw_bulk);
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 10 : Zero-copy success
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
         *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_WAIT
         */
        /*
         * To bypass request handler need to change FOM callback
         * function which wakeup FOM from wait.
         */
        fom->fo_clink.cl_cb = &cb_dummy;
        wait_dummy(fom);

        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_INIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_WAIT  &&
                     fom->fo_phase == C2_FOPH_IO_ZERO_COPY_WAIT);

        /*
         * Cleanup & restore FOM for next test.
         */
        while (fom->fo_loc->fl_wail_nr > 0) {
                sleep(1);
        }
        c2_clink_del(&fom->fo_clink);

        /*
         * Case 11 : Zero-copy failure
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: C2_FOPH_FAILURE
         */
        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_WAIT;
        fom_obj->fcrw_bulk.rb_rc  = -1;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc != 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_FAILURE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_bulk.rb_rc  = 0;
        rc = 0;
        fom->fo_rc = 0;

        /*
         * Case 12 : Zero-copy success
         *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
         *         Expected Output phase: C2_FOPH_IO_BUFFER_RELEASE
         */
        fom->fo_phase =  C2_FOPH_IO_ZERO_COPY_WAIT;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_BUFFER_RELEASE);


        /*
         * Case 13 : Processing of remaining buffer descriptors.
         *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_ACQUIRE
         */
        fom->fo_phase = C2_FOPH_IO_BUFFER_RELEASE;

        saved_ndesc = fom_obj->fcrw_ndesc;
        fom_obj->fcrw_ndesc = 2;
        rwfop->crw_desc.id_nr = 2;
        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

        /* Cleanup & make clean FOM for next test. */
        fom_obj->fcrw_ndesc = saved_ndesc;
        rwfop->crw_desc.id_nr = saved_ndesc;

        /*
         * Case 14 : All buffer descriptors are processed.
         *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
         *         Expected Output phase: C2_FOPH_SUCCESS
         */
        fom->fo_phase = C2_FOPH_IO_BUFFER_RELEASE;

        rc = c2_io_fom_cob_rw_state(fom);
        C2_UT_ASSERT(fom->fo_rc == 0 &&
                     rc == C2_FSO_AGAIN  &&
                     fom->fo_phase == C2_FOPH_SUCCESS);

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
	struct c2_fop_cob_writev_rep	*wrep;

        struct c2_io_fom_cob_rw  *fom_obj;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        rwfop = io_rw_get(fom->fo_fop);

	C2_UT_ASSERT(rwfop->crw_desc.id_nr == rwfop->crw_ivecs.cis_nr);
        ffid = &rwfop->crw_fid;
        io_fom_cob_rw_fid_wire2mem(ffid, &fid);
        io_fom_cob_rw_fid2stob_map(&fid, &stobid);
        fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	c2_dtx_init(&fom->fo_tx);
	rc = fom_stdom->sd_ops->sdo_tx_make(fom_stdom, &fom->fo_tx);
        C2_UT_ASSERT(rc == 0);

	rc = c2_stob_create_helper(fom_stdom, &fom->fo_tx, &stobid,
				  &fom_obj->fcrw_stob);
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(fom_obj->fcrw_stob != NULL);
        C2_UT_ASSERT(fom_obj->fcrw_stob->so_state == CSS_EXISTS);

	c2_dtx_done(&fom->fo_tx);

	wrep = c2_fop_data(fom->fo_rep_fop);
	wrep->c_rep.rwr_rc = 0;
	wrep->c_rep.rwr_count = rwfop->crw_ivecs.cis_nr;
	fom->fo_rep_fop->f_item.ri_group = NULL;
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
	C2_UT_ASSERT(rc == 0);
	fom->fo_phase = C2_FOPH_FINISH;
	return rc;
}

static struct c2_fom_ops bulkio_stob_create_fom_ops = {
	.fo_fini = bulkio_stob_fom_fini,
	.fo_state = bulkio_stob_create_fom_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
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
		C2_UT_ASSERT(rc == 0);
                bp->bp_wfops[i]->if_fop.f_type->ft_fom_type =
                bulkio_stob_create_fom_type;

		rw = io_rw_get(&bp->bp_wfops[i]->if_fop);
		bp->bp_wfops[i]->if_fop.f_type->ft_ops =
		&bulkio_stob_create_ops;
		rw->crw_fid = bp->bp_fids[i];
		targ[i].ta_index = i;
		targ[i].ta_op = op;
		targ[i].ta_bp = bp;
		io_fops_rpc_submit(&targ[i]);
	}
	io_fops_destroy(bp);
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
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type = c2_io_fom_cob_rw_mopt;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type = c2_io_fom_cob_rw_mopt;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
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
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type =
	bulkio_server_write_fom_type;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops =
	&bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type =
	bulkio_server_read_fom_type;
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
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
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type =
		ut_io_fom_cob_rw_type_mopt;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops =
		&bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type =
		ut_io_fom_cob_rw_type_mopt;
	bp->bp_rfops[0]->if_fop.f_type->ft_ops =
		&bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
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
		io_fops_create(bp, op, IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);
		io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
							       bp->bp_rfops;
		for (i = 0; i < IO_FOPS_NR; ++i) {
			io_fops[i]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
                        io_fops[i]->if_fop.f_type->ft_fom_type =
			c2_io_fom_cob_rw_mopt;
			targ[i].ta_index = i;
			targ[i].ta_op = op;
			targ[i].ta_bp = bp;
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
	io_fops_destroy(bp);
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

	io_fops = (op == C2_IOSERVICE_WRITEV_OPCODE) ? bp->bp_wfops :
						       bp->bp_rfops;
	C2_ALLOC_PTR(io_fops[index]);

	if (op == C2_IOSERVICE_WRITEV_OPCODE)
                rc = c2_io_fop_init(io_fops[index], &c2_fop_cob_writev_fopt);
        else
                rc = c2_io_fop_init(io_fops[index], &c2_fop_cob_readv_fopt);
	iofop = io_fops[index];
	rbulk = &iofop->if_rbulk;
	rw = io_rw_get(&io_fops[index]->if_fop);

	bp->bp_offsets[0] = IO_SEG_START_OFFSET;

	void add_buffer_bulk(int j)
	{
		/*
		 * Adds a c2_rpc_bulk_buf structure to list of such structures
		 * in c2_rpc_bulk.
		 */
		rc = c2_rpc_bulk_buf_add(rbulk, IO_SEGS_NR, &bp->bp_cnetdom,
					 NULL, &rbuf);
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
			C2_NET_QT_PASSIVE_BULK_SEND :
			C2_NET_QT_PASSIVE_BULK_RECV;
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
	rc = c2_rpc_bulk_store(rbulk, &bp->bp_cctx->rcx_connection,
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

	buf_nr = IO_FOPS_NR / 4;
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
	targ.ta_bp = bp;
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
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
}

static void bulkio_init(void)
{
	int			 rc;
	int			 port;
	const char		*caddr;
	const char		*saddr;

	caddr = saddr = "127.0.0.1";
	port = 23134;
	C2_ALLOC_PTR(bp);
	C2_ASSERT(bp != NULL);
	bulkio_params_init(bp);

	rc = bulkio_server_start(bp, saddr, port);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(bp->bp_sctx != NULL);
	rc = bulkio_client_start(bp, caddr, port, saddr, port);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(bp->bp_cctx != NULL);

	bulkio_stob_create();
}

static void bulkio_fini(void)
{
	bulkio_client_stop(bp->bp_cctx);

	bulkio_server_stop(bp->bp_sctx);
	bulkio_params_fini(bp);
	c2_free(bp);
}

/*
 * Only used for user-space UT.
 */
const struct c2_test_suite bulkio_server_ut = {
	.ts_name = "bulk-server-ut",
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
C2_EXPORTED(bulkio_server_ut);
