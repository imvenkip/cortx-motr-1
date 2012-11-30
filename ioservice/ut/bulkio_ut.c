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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 09/29/2011
 */

#include <sys/stat.h>
#include <sys/types.h>

#include "lib/processor.h"
#include "lib/ut.h"
#include "bulkio_common.h"
#include "net/lnet/lnet.h"
#include "ioservice/io_fops.c"	/* To access static APIs. */
#include "ioservice/io_foms.c"	/* To access static APIs. */
#include "colibri/colibri_setup.h"
#include "lib/finject.h"
#include "fop/fom_generic.c"

static void bulkio_init();
static void bulkio_fini();

struct bulkio_params *bp;

/*
 * This is done in order to bypass the problem of item->ri_ops->rio_free()
 * not getting invoked until rpc session is finalized.
 * An IO fop can not be directly deallocated from rio_free() since it
 * could be statically allocated, it could be an embedded object inside
 * another object and so on.
 * Ergo, IO fop has to be deallocated manually _after_ rio_free() is invoked.
 * But this would not be done until rpc session is finalized.
 * It would be inappropriate to finalize rpc session after each and every
 * fop is sent and its reply is received.
 * Hence this fop array acts as a garbage collector which collects all
 * submitted fops and deallocates them after rpc session is finalized.
 */
static struct c2_io_fop *garb_collect[IO_FOPS_NR * 3];
static int garb_index;

extern void bulkioapi_test(void);
static int io_fop_server_write_fom_create(struct c2_fop *fop,
					  struct c2_fom **m);
static int ut_io_fom_cob_rw_create(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_server_read_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int io_fop_stob_create_fom_create(struct c2_fop *fop, struct c2_fom **m);
static int check_write_fom_tick(struct c2_fom *fom);
static int check_read_fom_tick(struct c2_fom *fom);

struct c2_fop_type_ops bulkio_stob_create_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_write_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct c2_fop_type_ops bulkio_server_read_fop_ut_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

static struct c2_fom_type_ops bulkio_server_write_fomt_ops = {
	.fto_create = io_fop_server_write_fom_create,
};

static struct c2_fom_type_ops bulkio_server_read_fomt_ops = {
	.fto_create = io_fop_server_read_fom_create,
};

static struct c2_fom_type_ops bulkio_stob_create_fomt_ops = {
	.fto_create = io_fop_stob_create_fom_create,
};

static struct c2_fom_type_ops ut_io_fom_cob_rw_type_ops = {
	.fto_create = ut_io_fom_cob_rw_create,
};

static inline struct c2_net_transfer_mc *fop_tm_get(
		const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);

	return &(item_machine(&fop->f_item)->rm_tm);
}

static void bulkio_stob_fom_fini(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rw   *fom_obj;

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
        fop_ndom = fop_tm_get(fop)->ntm_dom;
        c2_tl_for(bufferpools, &serv_obj->rios_buffer_pools,
                     bpdesc) {
                if (bpdesc->rios_ndom == fop_ndom) {
                        return &bpdesc->rios_bp;
                }
        } c2_tl_endfor;

        return NULL;
}


/*
 * - This is positive test case to test c2_io_fom_cob_rw_tick(fom).
 * - This function test next phase after every defined phase for Write FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 */
static int bulkio_server_write_fom_tick(struct c2_fom *fom)
{
	int rc;
	int phase0;

	phase0 = c2_fom_phase(fom);
	rc = c2_io_fom_cob_rw_tick(fom);
	switch (phase0) {
	case C2_FOPH_IO_FOM_BUFFER_ACQUIRE :
		C2_UT_ASSERT(C2_IN(c2_fom_phase(fom),
				   (C2_FOPH_IO_FOM_BUFFER_WAIT,
				    C2_FOPH_IO_ZERO_COPY_INIT)));
		break;
	case C2_FOPH_IO_ZERO_COPY_INIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_WAIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_WAIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_STOB_INIT);
		break;
	case C2_FOPH_IO_STOB_INIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_STOB_WAIT);
		break;
	case C2_FOPH_IO_STOB_WAIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_BUFFER_RELEASE);
		break;
	case C2_FOPH_IO_BUFFER_RELEASE:
                C2_UT_ASSERT(C2_IN(c2_fom_phase(fom),
				   (C2_FOPH_IO_FOM_BUFFER_ACQUIRE,
				    C2_FOPH_SUCCESS)));
		break;
	}
	return rc;
}

/*
 * - This is positive test case to test c2_io_fom_cob_rw_tick(fom).
 * - This function test next phase after every defined phase for Read FOM.
 * - Validation of next phase is done as per state transition in detail design.
 *   @see DLD-bulk-server-lspec-state
 */
static int bulkio_server_read_fom_tick(struct c2_fom *fom)
{
	int rc;
	int phase0;

	phase0 = c2_fom_phase(fom);
	rc = c2_io_fom_cob_rw_tick(fom);

	switch (phase0) {
	case C2_FOPH_IO_FOM_BUFFER_ACQUIRE :
                C2_UT_ASSERT(C2_IN(c2_fom_phase(fom),
				   (C2_FOPH_IO_FOM_BUFFER_WAIT,
				    C2_FOPH_IO_STOB_INIT)));
		break;
	case C2_FOPH_IO_ZERO_COPY_INIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_WAIT);
		break;
	case C2_FOPH_IO_ZERO_COPY_WAIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_BUFFER_RELEASE);
		break;
	case C2_FOPH_IO_STOB_INIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_STOB_WAIT);
		break;
	case C2_FOPH_IO_STOB_WAIT:
                C2_UT_ASSERT(c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_INIT);
		break;
	case C2_FOPH_IO_BUFFER_RELEASE:
                C2_UT_ASSERT(C2_IN(c2_fom_phase(fom),
				   (C2_FOPH_IO_FOM_BUFFER_ACQUIRE,
				    C2_FOPH_SUCCESS)));
		break;
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
        return c2_is_read_fop(fom->fo_fop) ?
		check_read_fom_tick(fom) : check_write_fom_tick(fom);
}

enum fom_state_transition_tests {
        TEST01 = C2_FOPH_IO_FOM_BUFFER_ACQUIRE,
        TEST02,
        TEST03,
        TEST07,
        TEST10,
        TEST11,
};

static int                    i = 0;
static struct c2_net_buffer  *nb_list[64];
static struct c2_net_buffer_pool *buf_pool;
static int next_test = TEST01;

static void empty_buffers_pool(uint32_t colour)
{
        i--;
        c2_net_buffer_pool_lock(buf_pool);
        do nb_list[++i] = c2_net_buffer_pool_get(buf_pool, colour);
        while (nb_list[i] != NULL);
        c2_net_buffer_pool_unlock(buf_pool);
}

static void release_one_buffer(uint32_t colour)
{
        c2_net_buffer_pool_lock(buf_pool);
        c2_net_buffer_pool_put(buf_pool, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(buf_pool);
}

static void fill_buffers_pool(uint32_t colour)
{
        c2_net_buffer_pool_lock(buf_pool);
        while (i > 0)
                c2_net_buffer_pool_put(buf_pool, nb_list[--i], colour);
        c2_net_buffer_pool_unlock(buf_pool);
}

void fom_phase_set(struct c2_fom *fom, int phase)
{
	if (c2_fom_phase(fom) == C2_FOPH_FAILURE) {
		const struct fom_phase_desc *fpd_phase;
		while (c2_fom_phase(fom) != C2_FOPH_FINISH) {
			fpd_phase = &fpd_table[c2_fom_phase(fom)];
			c2_fom_phase_set(fom, fpd_phase->fpd_nextphase);
		}

		c2_sm_fini(&fom->fo_sm_phase);
		c2_sm_init(&fom->fo_sm_phase, fom->fo_type->ft_conf,
			   C2_FOM_PHASE_INIT, &fom->fo_loc->fl_group,
			   &fom->fo_loc->fl_dom->fd_addb_ctx);

		while (c2_fom_phase(fom) != C2_FOPH_TYPE_SPECIFIC) {
			fpd_phase = &fpd_table[c2_fom_phase(fom)];
			c2_fom_phase_set(fom, fpd_phase->fpd_nextphase);
		}
	}

	while (phase != c2_fom_phase(fom)) {
		struct c2_io_fom_cob_rw_state_transition  st;

		st = c2_is_read_fop(fom->fo_fop) ?
			io_fom_read_st[c2_fom_phase(fom)] :
			io_fom_write_st[c2_fom_phase(fom)];

		if (C2_IN(phase, (st.fcrw_st_next_phase_again,
				  st.fcrw_st_next_phase_wait))) {
			c2_fom_phase_set(fom, phase);
			break;
		}

		c2_fom_phase_set(fom, st.fcrw_st_next_phase_again != 0 ?
				      st.fcrw_st_next_phase_again :
				      st.fcrw_st_next_phase_wait);
	}
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
static int check_write_fom_tick(struct c2_fom *fom)
{
        int                           rc;
        uint32_t                      colour;
        int                           acquired_net_bufs;
        int                           saved_segments_count;
        int                           saved_ndesc;
        struct c2_fop_cob_rw         *rwfop;
        struct c2_net_domain         *netdom;
        struct c2_fop                *fop;
        struct c2_io_fom_cob_rw      *fom_obj;
        struct c2_fid                 saved_fid;
        struct c2_fid                 invalid_fid;
        struct c2_stob_io_desc       *saved_stobio_desc;
        struct c2_stob_domain        *fom_stdom;
        struct c2_stob_id             stobid;
        struct c2_net_transfer_mc    *tm;
	struct c2_reqh               *reqh;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);

        tm = fop_tm_get(fop);
        colour = c2_net_tm_colour_get(tm);

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /*
                 * No need to test generic phases.
                 */
                rc = c2_io_fom_cob_rw_tick(fom);
		next_test = c2_fom_phase(fom);
        } else if (next_test == TEST01) {
                /* Acquire all buffer pool buffer test some of cases. */
                if (fom_obj->fcrw_bp == NULL)
                        buf_pool = ut_get_buffer_pool(fom);
                else
                        buf_pool = fom_obj->fcrw_bp;
                C2_UT_ASSERT(buf_pool != NULL);

                empty_buffers_pool(colour);

                /*
                 * Case 01: No network buffer is available with the buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
                 */
                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT &&
                             c2_fom_phase(fom) == C2_FOPH_IO_FOM_BUFFER_WAIT);

                /* Cleanup & make clean FOM for next test. */
                rc = C2_FSO_WAIT;
                fom->fo_sm_phase.sm_rc = 0;

                release_one_buffer(colour);
		next_test = TEST02;
        } else if (next_test == TEST02) {
                /*
                 * Case 02: No network buffer is available with the buffer pool.
                 *         Even after getting buffer pool not-empty event,
                 *         buffers are not available in pool (which could be
                 *         used by other FOMs in the server).
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
                 */

                empty_buffers_pool(colour);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT  &&
                             c2_fom_phase(fom) ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

                /* Cleanup & rstore FOM for next test. */
                rc = C2_FSO_WAIT;

                release_one_buffer(colour);
		next_test = TEST03;
        } else if (next_test == TEST03) {
                int cdi = fom_obj->fcrw_curr_desc_index;
                /*
                 * Case 03 : Network buffer is available with the buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
                 */
		fom_phase_set(fom, C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_INIT);

                /*
                 * Cleanup & restore FOM for next test.
                 * Since previous case successfully acquired network buffer
                 * and now buffer pool not having any network buffer, this
                 * buffer need to return back to the buffer pool.
                 */
                acquired_net_bufs =
                        netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
                c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
                while (acquired_net_bufs > 0) {
                        struct c2_net_buffer *nb;
                        nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                        c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                        netbufs_tlink_del_fini(nb);
                        acquired_net_bufs--;
                }
                c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
                fom_obj->fcrw_batch_size = 0;
                rc = C2_FSO_WAIT;

                /*
                 * Case 04 : Network buffer is available with the buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
                 *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_FOM_BUFFER_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_INIT);

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
                 * Modify segments count in fop (value greater than net domain
                 * max), so that zero-copy initialisation fails.
                 */
                saved_segments_count = rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr;
                netdom = fop_tm_get(fop)->ntm_dom;
                rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr =
                        c2_net_domain_get_max_buffer_segments(netdom) + 1;

                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & restore FOM for next test. */
                rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr =
                saved_segments_count;
                rc = C2_FSO_WAIT;

                /*
                 * Case 06 : Zero-copy success
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
                 *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_WAIT
                 */
                /*
                 * To bypass request handler need to change FOM callback
                 * function which wakeup FOM from wait.
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT &&
                             c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_WAIT);
		next_test = TEST07;
        } else if (next_test == TEST07) {
                /*
                 * Case 07 : Zero-copy failure
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_WAIT);
                fom_obj->fcrw_bulk.rb_rc  = -1;

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & make clean FOM for next test. */
                fom_obj->fcrw_bulk.rb_rc  = 0;
                rc = C2_FSO_WAIT;

                /*
                 * Case 08 : Zero-copy success from wait state.
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
                 *         Expected Output phase: C2_FOPH_IO_STOB_INIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_STOB_INIT);

                /*
                 * Case 09 : STOB I/O launch failure
                 *         Input phase          : C2_FOPH_IO_STOB_INIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */

                /* Save original fid and pass invialid fid
                 * to make I/O launch fail. */
                saved_fid = rwfop->crw_fid;
                c2_fid_set(&invalid_fid, 111, 222);

                rwfop->crw_fid = invalid_fid;
                fom_phase_set(fom, C2_FOPH_IO_STOB_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 && rc == C2_FSO_AGAIN  &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & make clean FOM for next test. */
                rwfop->crw_fid = saved_fid;
                rc = C2_FSO_WAIT;

                /*
                 * Case 10 : STOB I/O launch success
                 *         Input phase          : C2_FOPH_IO_STOB_INIT
                 *         Expected Output phase: C2_FOPH_IO_STOB_WAIT
                 */
                /*
                 * To bypass request handler need to change FOM callback
                 * function which wakeup FOM from wait.
                 */
                fom_phase_set(fom, C2_FOPH_IO_STOB_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 && rc == C2_FSO_WAIT  &&
                             c2_fom_phase(fom) == C2_FOPH_IO_STOB_WAIT);

		next_test = TEST11;
        } else if (next_test == TEST11) {
                /*
                 * Case 11 : STOB I/O failure from wait state.
                 *         Input phase          : C2_FOPH_IO_STOB_WAIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */

                /*
                 * To test this case there is a need to invalidate stobio
                 * descriptor, since io_finish() removes the stobio descriptor
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

		fom_phase_set(fom, C2_FOPH_IO_STOB_WAIT);
		c2_fi_enable_once("io_finish", "fake_error");
                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 && rc == C2_FSO_AGAIN  &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /*
                 * Cleanup & make clean FOM for next test.
                 * Restore original fom.
                 */
                stobio_tlist_add(&fom_obj->fcrw_stio_list, saved_stobio_desc);
                io_fom_cob_rw_fid2stob_map(&rwfop->crw_fid, &stobid);
		reqh = c2_fom_reqh(fom);
                fom_stdom = c2_cs_stob_domain_find(reqh, &stobid);
		C2_UT_ASSERT(fom_stdom != NULL);

                rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
                C2_UT_ASSERT(rc == 0);

                rc = C2_FSO_WAIT;

                /*
                 * Case 12 : STOB I/O success
                 *         Input phase          : C2_FOPH_IO_STOB_WAIT
                 *         Expected Output phase: C2_FOPH_IO_BUFFER_RELEASE
                 */
                fom_phase_set(fom, C2_FOPH_IO_STOB_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN  &&
                             c2_fom_phase(fom) == C2_FOPH_IO_BUFFER_RELEASE);

                /*
                 * Case 13 : Processing of remaining buffer descriptors.
                 *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 */
                fom_phase_set(fom, C2_FOPH_IO_BUFFER_RELEASE);

                saved_ndesc = fom_obj->fcrw_ndesc;
                fom_obj->fcrw_ndesc = 2;
                rwfop->crw_desc.id_nr = 2;
                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN  &&
                             c2_fom_phase(fom) ==
			     C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

                /* Cleanup & make clean FOM for next test. */
                fom_obj->fcrw_ndesc = saved_ndesc;
                rwfop->crw_desc.id_nr = saved_ndesc;

                /*
                 * Case 14 : All buffer descriptors are processed.
                 *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
                 *         Expected Output phase: C2_FOPH_SUCCESS
                 */
                fom_phase_set(fom, C2_FOPH_IO_BUFFER_RELEASE);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_SUCCESS);

                fill_buffers_pool(colour);
        } else {
                C2_UT_ASSERT(0); /* this should not happen */
                rc = C2_FSO_WAIT; /* to avoid compiler warning */
        }

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
static int check_read_fom_tick(struct c2_fom *fom)
{
        int                           rc;
        uint32_t                      colour;
        int                           acquired_net_bufs;
        int                           saved_segments_count;
        int                           saved_ndesc;
        struct c2_fop_cob_rw         *rwfop;
        struct c2_net_domain         *netdom;
        struct c2_fop                *fop;
        struct c2_io_fom_cob_rw      *fom_obj;
        struct c2_fid                 saved_fid;
        struct c2_fid                 invalid_fid;
        struct c2_stob_io_desc       *saved_stobio_desc;
        struct c2_stob_domain        *fom_stdom;
        struct c2_stob_id             stobid;
        struct c2_net_transfer_mc    *tm;
	struct c2_reqh               *reqh;

        fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        fop = fom->fo_fop;
        rwfop = io_rw_get(fop);

        tm = fop_tm_get(fop);
        colour = c2_net_tm_colour_get(tm);

        if (c2_fom_phase(fom) < C2_FOPH_NR) {
                /*
                 * No need to test generic phases.
                 */
                rc = c2_io_fom_cob_rw_tick(fom);
		next_test = c2_fom_phase(fom);
        } else if (next_test == TEST01) {
                /* Acquire all buffer pool buffer test some of cases. */
                if (fom_obj->fcrw_bp == NULL)
                        buf_pool = ut_get_buffer_pool(fom);
                else
                        buf_pool = fom_obj->fcrw_bp;
                C2_UT_ASSERT(buf_pool != NULL);

                /* Acquire all buffers from buffer pool to make it empty */
                empty_buffers_pool(colour);

                /*
                 * Case 01 : No network buffer is available with buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 && rc == C2_FSO_WAIT &&
                             c2_fom_phase(fom) ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

                /* Cleanup & make clean FOM for next test. */
                rc = C2_FSO_WAIT;

                release_one_buffer(colour);
                next_test = TEST02;
        } else if (next_test == TEST02) {
                /*
                 * Case 02 : No network buffer is available with buffer pool.
                 *         Even after getting buffer pool not-empty event,
                 *         buffers are not available in pool (which could be
                 *         used by other FOMs in the server).
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_WAIT
                 */

                empty_buffers_pool(colour);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT  &&
                             c2_fom_phase(fom) ==  C2_FOPH_IO_FOM_BUFFER_WAIT);

                /* Cleanup & make clean FOM for next test. */
                rc = C2_FSO_WAIT;

                release_one_buffer(colour);
		next_test = TEST03;
        } else if (next_test == TEST03) {
                /*
                 * Case 03 : Network buffer is available with the buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 *         Expected Output phase: C2_FOPH_IO_STOB_INIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 && rc == C2_FSO_AGAIN  &&
                             c2_fom_phase(fom) == C2_FOPH_IO_STOB_INIT);

                /*
                 * Cleanup & make clean FOM for next test.
                 * Since previous this case successfully acquired network buffer
                 * and now buffer pool not having network buffer, this buffer
                 * need to return back to the buffer pool.
                 */
                acquired_net_bufs =
                        netbufs_tlist_length(&fom_obj->fcrw_netbuf_list);
                c2_net_buffer_pool_lock(fom_obj->fcrw_bp);
                while (acquired_net_bufs > 0) {
                        struct c2_net_buffer *nb;

                        nb = netbufs_tlist_tail(&fom_obj->fcrw_netbuf_list);
                        c2_net_buffer_pool_put(fom_obj->fcrw_bp, nb, colour);
                        netbufs_tlink_del_fini(nb);
                        acquired_net_bufs--;
                }
                c2_net_buffer_pool_unlock(fom_obj->fcrw_bp);
                fom_obj->fcrw_batch_size = 0;
                rc = C2_FSO_WAIT;

                /*
                 * Case 04 : Network buffer available with buffer pool.
                 *         Input phase          : C2_FOPH_IO_FOM_BUFFER_WAIT
                 *         Expected Output phase: C2_FOPH_IO_STOB_INIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_FOM_BUFFER_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_STOB_INIT);

                /* No need to cleanup here, since FOM will transitioned
                 * to expected phase.
                 */

                /*
                 * Case 05 : STOB I/O launch failure
                 *         Input phase          : C2_FOPH_IO_STOB_INIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */

                /* Save original fid and pass invalid fid to make I/O launch
                 * fail. */
                saved_fid = rwfop->crw_fid;
                c2_fid_set(&invalid_fid, 111, 222);

                rwfop->crw_fid = invalid_fid;

                fom_phase_set(fom, C2_FOPH_IO_STOB_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & make clean FOM for next test. */
                rwfop->crw_fid = saved_fid;
                rc = C2_FSO_WAIT;

                /*
                 * Case 06 : STOB I/O launch success
                 *         Input phase          : C2_FOPH_IO_STOB_INIT
                 *         Expected Output phase: C2_FOPH_IO_STOB_WAIT
                 */
                /*
                 * To bypass request handler need to change FOM callback
                 * function which wakeup FOM from wait.
                 */
                fom_phase_set(fom, C2_FOPH_IO_STOB_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT &&
                             c2_fom_phase(fom) == C2_FOPH_IO_STOB_WAIT);
		next_test = TEST07;
        } else if (next_test == TEST07) {

                int cdi = fom_obj->fcrw_curr_desc_index;
                /*
                 * Case 07 : STOB I/O failure
                 *         Input phase          : C2_FOPH_IO_STOB_WAIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */

                /*
                 * To test this case there is a need to invalidate stobio
                 * descriptor, since io_finish() remove stobio descriptor
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

                fom_phase_set(fom, C2_FOPH_IO_STOB_WAIT);
		c2_fi_enable_once("io_finish", "fake_error");

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /*
                 * Cleanup & make clean FOM for next test.
                 * Restore original fom.
                 */
                stobio_tlist_add(&fom_obj->fcrw_stio_list, saved_stobio_desc);
                io_fom_cob_rw_fid2stob_map(&rwfop->crw_fid, &stobid);
		reqh = c2_fom_reqh(fom);
                fom_stdom = c2_cs_stob_domain_find(reqh, &stobid);
		C2_UT_ASSERT(fom_stdom != NULL);

                rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
                C2_UT_ASSERT(rc == 0);

                rc = C2_FSO_WAIT;

                /*
                 * Case 08 : STOB I/O success
                 *         Input phase          : C2_FOPH_IO_STOB_WAIT
                 *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_INIT
                 */
                fom_phase_set(fom, C2_FOPH_IO_STOB_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_INIT);

                /*
                 * Case 09 : Zero-copy failure
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */

                /*
                 * Modify segments count in fop (value greater than net domain
                 * max), so that zero-copy initialisation fails.
                 */
                saved_segments_count = rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr;
                netdom = fop_tm_get(fop)->ntm_dom;
                rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr =
                        c2_net_domain_get_max_buffer_segments(netdom) + 1;

                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & make clean FOM for next test. */
                rwfop->crw_ivecs.cis_ivecs[cdi].ci_nr = saved_segments_count;
                rc = C2_FSO_WAIT;
                fom_phase_set(fom, TEST10);

                /*
                 * Case 10 : Zero-copy success
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_INIT
                 *         Expected Output phase: C2_FOPH_IO_ZERO_COPY_WAIT
                 */
                /*
                 * To bypass request handler need to change FOM callback
                 * function which wakeup FOM from wait.
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_INIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_WAIT &&
                             c2_fom_phase(fom) == C2_FOPH_IO_ZERO_COPY_WAIT);
		next_test = TEST11;
        } else if (next_test == TEST11) {
                /*
                 * Case 11 : Zero-copy failure
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
                 *         Expected Output phase: C2_FOPH_FAILURE
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_WAIT);
                fom_obj->fcrw_bulk.rb_rc  = -1;

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) != 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_FAILURE);

                /* Cleanup & make clean FOM for next test. */
                fom_obj->fcrw_bulk.rb_rc  = 0;
                rc = C2_FSO_WAIT;

                /*
                 * Case 12 : Zero-copy success
                 *         Input phase          : C2_FOPH_IO_ZERO_COPY_WAIT
                 *         Expected Output phase: C2_FOPH_IO_BUFFER_RELEASE
                 */
                fom_phase_set(fom, C2_FOPH_IO_ZERO_COPY_WAIT);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_IO_BUFFER_RELEASE);


                /*
                 * Case 13 : Processing of remaining buffer descriptors.
                 *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
                 *         Expected Output phase: C2_FOPH_IO_FOM_BUFFER_ACQUIRE
                 */
                fom_phase_set(fom, C2_FOPH_IO_BUFFER_RELEASE);

                saved_ndesc = fom_obj->fcrw_ndesc;
                fom_obj->fcrw_ndesc = 2;
                rwfop->crw_desc.id_nr = 2;
                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) ==
			     C2_FOPH_IO_FOM_BUFFER_ACQUIRE);

                /* Cleanup & make clean FOM for next test. */
                fom_obj->fcrw_ndesc = saved_ndesc;
                rwfop->crw_desc.id_nr = saved_ndesc;

                /*
                 * Case 14 : All buffer descriptors are processed.
                 *         Input phase          : C2_FOPH_IO_BUFFER_RELEASE
                 *         Expected Output phase: C2_FOPH_SUCCESS
                 */
                fom_phase_set(fom, C2_FOPH_IO_BUFFER_RELEASE);

                rc = c2_io_fom_cob_rw_tick(fom);
                C2_UT_ASSERT(c2_fom_rc(fom) == 0 &&
                             rc == C2_FSO_AGAIN &&
                             c2_fom_phase(fom) == C2_FOPH_SUCCESS);

                fill_buffers_pool(colour);
        } else {
                C2_UT_ASSERT(0); /* this should not happen */
                rc = C2_FSO_WAIT; /* to avoid compiler warning */
        }

	return rc;
}

/* It is used to create the stob specified in the fid of each fop. */
static int bulkio_stob_create_fom_tick(struct c2_fom *fom)
{
        struct c2_fop_cob_rw            *rwfop;
        struct c2_stob_domain           *fom_stdom;
        struct c2_stob_id                stobid;
        int				 rc;
	struct c2_fop_cob_writev_rep	*wrep;
	struct c2_reqh                  *reqh;

        struct c2_io_fom_cob_rw  *fom_obj;
	fom_obj = container_of(fom, struct c2_io_fom_cob_rw, fcrw_gen);
        rwfop = io_rw_get(fom->fo_fop);

	C2_UT_ASSERT(rwfop->crw_desc.id_nr == rwfop->crw_ivecs.cis_nr);
        io_fom_cob_rw_fid2stob_map(&rwfop->crw_fid, &stobid);
	reqh = c2_fom_reqh(fom);
        fom_stdom = c2_cs_stob_domain_find(reqh, &stobid);
	C2_UT_ASSERT(fom_stdom != NULL);

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
	rc = c2_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);
	C2_UT_ASSERT(rc == 0);
	c2_fom_phase_set(fom, C2_FOPH_FINISH);
	return C2_FSO_WAIT;
}

static struct c2_fom_ops bulkio_stob_create_fom_ops = {
	.fo_fini = bulkio_stob_fom_fini,
	.fo_tick = bulkio_stob_create_fom_tick,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
};

static struct c2_fom_ops bulkio_server_write_fom_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_tick = bulkio_server_write_fom_tick,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
};

static struct c2_fom_ops ut_io_fom_cob_rw_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_tick = ut_io_fom_cob_rw_state,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
};

static struct c2_fom_ops bulkio_server_read_fom_ops = {
	.fo_fini = c2_io_fom_cob_rw_fini,
	.fo_tick = bulkio_server_read_fom_tick,
	.fo_home_locality = c2_io_fom_cob_rw_locality_get,
};

static int io_fop_stob_create_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
	int rc;
	struct c2_fom *fom;
	rc = c2_io_fom_cob_rw_create(fop, &fom);
        C2_UT_ASSERT(rc == 0);
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
	fom->fo_ops = &bulkio_server_read_fom_ops;
	*m = fom;
        C2_UT_ASSERT(fom->fo_fop != 0);
	return rc;
}

static void garb_add(struct c2_io_fop *iofop)
{
        garb_collect[garb_index] = iofop;
        ++garb_index;
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
                garb_add(bp->bp_wfops[i]);
		/*
		 * We replace the original ->ft_ops amd ->ft_fom_type for
		 * regular io_fops. This is reset later.
		 */
                bp->bp_wfops[i]->if_fop.f_type->ft_fom_type.ft_ops =
		&bulkio_stob_create_fomt_ops;

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

static void garb_clear()
{
        int cnt;

        for (cnt = 0; cnt < garb_index; ++cnt) {
                c2_free(garb_collect[cnt]);
                garb_collect[cnt] = NULL;
        }
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
        garb_add(bp->bp_wfops[0]);
	/*
	 * Here we replace the original ->ft_ops amd ->ft_fom_type as they were
	 * changed during bulkio_stob_create test.
	 */
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type.ft_ops = &io_fom_type_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        garb_add(bp->bp_rfops[0]);
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type.ft_ops = &io_fom_type_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
}

/*
 * Sends regular write and read io fops, although replaces the original FOM
 * types in each io fop type with UT specific FOM types.
 */
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
        garb_add(bp->bp_wfops[0]);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type.ft_ops =
	&bulkio_server_write_fomt_ops;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops =
	&bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        garb_add(bp->bp_rfops[0]);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type.ft_ops =
	&bulkio_server_read_fomt_ops;
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &bulkio_server_read_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
}

/*
 * Sends regular write and read fops although replaces the original FOM types
 * in each io fop type with UT specific FOM types to check state transition for
 * I/O FOM.
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
        garb_add(bp->bp_wfops[0]);
        bp->bp_wfops[0]->if_fop.f_type->ft_fom_type.ft_ops =
	&ut_io_fom_cob_rw_type_ops;
	bp->bp_wfops[0]->if_fop.f_type->ft_ops =
		&bulkio_server_write_fop_ut_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);

	buf = &bp->bp_iobuf[0]->nb_buffer;
	for (j = 0; j < IO_SEGS_NR; ++j) {
		memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
	}
	op = C2_IOSERVICE_READV_OPCODE;
	io_fops_create(bp, op, 1, 1, IO_SEGS_NR);
        garb_add(bp->bp_rfops[0]);
        bp->bp_rfops[0]->if_fop.f_type->ft_fom_type.ft_ops =
		&ut_io_fom_cob_rw_type_ops;
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
                        garb_add(io_fops[i]);
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
                io_fops_destroy(bp);
	}
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
        garb_add(bp->bp_wfops[0]);
	bp->bp_wfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);

	for (i = 0; i < buf_nr; ++i) {
		buf = &bp->bp_iobuf[i]->nb_buffer;
		for (j = 0; j < IO_SEGS_NR; ++j) {
			memset(buf->ov_buf[j], 'a', IO_SEG_SIZE);
		}
	}
	op = C2_IOSERVICE_READV_OPCODE;
	fop_create_populate(0, op, buf_nr);
        garb_add(bp->bp_rfops[0]);
	bp->bp_rfops[0]->if_fop.f_type->ft_ops = &io_fop_rwv_ops;
	targ.ta_index = 0;
	targ.ta_op = op;
	targ.ta_bp = bp;
	io_fops_rpc_submit(&targ);
	io_fops_destroy(bp);
}

void bulkio_server_read_write_fv_mismatch(void)
{
	struct c2_reqh      *reqh;
	struct c2_poolmach  *pm;
	struct c2_pool_event event;
	struct c2_fop       *wfop;
	struct c2_fop       *rfop;
	struct c2_fop_cob_rw_reply *rw_reply;
	int rc;

	event.pe_type  = C2_POOL_DEVICE;
	event.pe_index = 1;
	event.pe_state = C2_PNDS_FAILED;

	reqh = c2_cs_reqh_get(&bp->bp_sctx->rsx_colibri_ctx, "ioservice");
	C2_UT_ASSERT(reqh != NULL);

	pm = c2_ios_poolmach_get(reqh);
	C2_UT_ASSERT(pm != NULL);

	rc = c2_poolmach_state_transit(pm, &event);
	C2_UT_ASSERT(rc == 0);

	/* This is just a test to detect failure vector mismatch on server
	 * side. No need to prepare a full write request, e.g. buffer.
	 */
	wfop = c2_fop_alloc(&c2_fop_cob_writev_fopt, NULL);
	C2_UT_ASSERT(wfop != NULL);

	wfop->f_type->ft_ops = &io_fop_rwv_ops;
        wfop->f_type->ft_fom_type.ft_ops = &io_fom_type_ops;

	rc = c2_rpc_client_call(wfop, &bp->bp_cctx->rcx_session,
				&c2_fop_default_item_ops,
				0 /* deadline */,
				IO_RPC_ITEM_TIMEOUT);
	C2_ASSERT(rc == 0);
	rw_reply = io_rw_rep_get(c2_rpc_item_to_fop(wfop->f_item.ri_reply));
	C2_UT_ASSERT(rw_reply->rwr_rc ==
			C2_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH);

	rfop = c2_fop_alloc(&c2_fop_cob_readv_fopt, NULL);
	C2_UT_ASSERT(rfop != NULL);

	rfop->f_type->ft_ops = &io_fop_rwv_ops;
        rfop->f_type->ft_fom_type.ft_ops = &io_fom_type_ops;

	rc = c2_rpc_client_call(rfop, &bp->bp_cctx->rcx_session,
				&c2_fop_default_item_ops, 0 /* deadline */,
				IO_RPC_ITEM_TIMEOUT);
	C2_ASSERT(rc == 0);
	rw_reply = io_rw_rep_get(c2_rpc_item_to_fop(rfop->f_item.ri_reply));
	C2_UT_ASSERT(rw_reply->rwr_rc ==
			C2_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH);
}


static void bulkio_init(void)
{
	int  rc;
	const char *caddr = "0@lo:12345:34:*";
	const char *saddr = "0@lo:12345:34:1";

	C2_ALLOC_PTR(bp);
	C2_ASSERT(bp != NULL);
	bulkio_params_init(bp);

	rc = bulkio_server_start(bp, saddr);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(bp->bp_sctx != NULL);
	rc = bulkio_client_start(bp, caddr, saddr);
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

        /* Clear the garbage! */
        garb_clear();
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
		{ "bulkio_init", bulkio_init},
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
		{ "bulkio_server_read_write_fv_mismatch",
		   bulkio_server_read_write_fv_mismatch},
		{ "bulkio_fini", bulkio_fini},
		{ NULL, NULL }
	}
};
C2_EXPORTED(bulkio_server_ut);
