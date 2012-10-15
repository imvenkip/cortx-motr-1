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
 * Original author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 10/08/2012
 */

/*
 * Production code : c2t1fs/linux_kernel/file.c
 * UT code         : c2t1fs/linux_kernel/ut/file.c
 *
 * The production code is added this way to build colibri kernel module.
 * Both production code and UT code are part of Colibri kernel module
 * but production code file is not directly added to Makefile as a file
 * that needs to be compiled.
 * Instead, UT code is added in Makefile for colibri kernel module build
 * which subsequently adds production code file to colibri kernel module.
 */
#include "c2t1fs/linux_kernel/file.c"

#include "lib/ut.h"     /* c2_test_suite */
#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/chan.h"   /* c2_chan */
#include "fid/fid.h"   /* c2_fid */
#include "lib/misc.h"   /* c2_rnd */
#include "lib/time.h"   /* c2_time_nanoseconds */
#include "layout/layout.h" /* c2_layout_domain_init */
#include "layout/linear_enum.h" /* c2_layout_linear_enum */
#include <linux/dcache.h>  /* struct dentry */
#include "c2t1fs/linux_kernel/file_internal.h" /* io_request */

enum {
        IOVEC_NR         = 4,
        IOVEC_BUF_LEN    = 1024,
        FILE_START_INDEX = 21340,

	/* Number of data units. */
	LAY_N		 = 3,

	/* Number of parity units. */
	LAY_K		 = 1,

	/* Unit Size = 12K. */
	UNIT_SIZE	 = 12288,

	/* Data size for parity group = 12K * 3 = 36K. */
	DATA_SIZE        = UNIT_SIZE * LAY_N,

	FID_CONTAINER    = 0,
	FID_KEY		 = 3,
	ATTR_A_CONST	 = 1,
	ATTR_B_CONST	 = 2,
};

static struct c2t1fs_sb              csb;
static struct c2_pdclust_attr        pdattr;
static struct c2_pdclust_layout     *pdlay;
static struct c2_dbenv               testdb;
static struct c2_layout_domain       ldom;
static struct c2_layout_linear_enum *llenum;
static struct c2_layout_linear_attr  llattr;
static struct c2t1fs_inode           ci;
static struct c2_layout_linear_attr  llattr;
static struct file       	     lfile;

static int file_io_ut_init(void)
{
        int		  rc;
	uint64_t	  random;
	struct c2_layout *lay;

        rc = c2_dbenv_init(&testdb, "test.db", 0);
        C2_ASSERT(rc == 0);

        rc = c2_layout_domain_init(&ldom, &testdb);
        C2_ASSERT(rc == 0);

        rc = c2_layout_standard_types_register(&ldom);
        C2_ASSERT(rc == 0);

        C2_SET0(&csb);
        c2_addb_ctx_init(&c2t1fs_addb, &c2t1fs_addb_type, &c2_addb_global_ctx);
        c2_sm_group_init(&csb.csb_iogroup);
        csb.csb_active = true;
        c2_chan_init(&csb.csb_iowait);
        c2_atomic64_set(&csb.csb_pending_io_nr, 0);

        /* Tries to build a layout. */
        llattr = (struct c2_layout_linear_attr) {
                .lla_nr = LAY_N + 2 * LAY_K, 
                .lla_A  = ATTR_A_CONST,
                .lla_B  = ATTR_B_CONST,
        };
        llenum = NULL;
        rc = c2_linear_enum_build(&ldom, &llattr, &llenum);
        C2_ASSERT(rc == 0);

	random = c2_time_nanoseconds(c2_time_now());
        csb.csb_layout_id = c2_rnd(~0ULL >> 16, &random);
        pdattr = (struct c2_pdclust_attr) {
                .pa_N         = LAY_N,
                .pa_K         = LAY_K,
                .pa_P         = LAY_N + 2 * LAY_K,
                .pa_unit_size = UNIT_SIZE,

        };
        c2_uint128_init(&pdattr.pa_seed, "upjumpandpumpim,");
        rc = c2_pdclust_build(&ldom, csb.csb_layout_id, &pdattr,
                              &llenum->lle_base, &pdlay);
        C2_ASSERT(rc == 0);
        C2_ASSERT(pdlay != NULL);

        /* Initializes the c2t1fs inode and build layout instance. */
        C2_SET0(&ci);
        ci.ci_fid = (struct c2_fid) {
                .f_container = FID_CONTAINER,
                .f_key       = FID_KEY,
        };
        ci.ci_layout_id = csb.csb_layout_id;
	lay = c2_layout_find(&ldom, ci.ci_layout_id);
	C2_ASSERT(lay != NULL);

	rc = c2_layout_instance_build(lay, &ci.ci_fid, &ci.ci_layout_instance);
	C2_ASSERT(rc == 0);
	C2_ASSERT(ci.ci_layout_instance != NULL);
	c2_layout_put(lay);

	C2_ALLOC_PTR(lfile.f_dentry);
	C2_ASSERT(lfile.f_dentry != NULL);
	lfile.f_dentry->d_inode = &ci.ci_inode;

	/* Sets the file size in inode. */
	ci.ci_inode.i_size = DATA_SIZE; 

	return 0;
}

static void ds_test(void)
{
        int                   rc;
        int                   cnt;
	uint64_t              size;
	struct c2_fid         cfid;
	struct data_buf      *dbuf;
        struct io_request     req;
        struct iovec          iovec_arr[IOVEC_NR];
        struct c2_indexvec    ivec;
	struct pargrp_iomap  *map;
	struct target_ioreq   ti;
	struct io_req_fop    *irfop;
	struct c2_rpc_session session;

        C2_ALLOC_ARR(ivec.iv_vec.v_count, IOVEC_NR);
        C2_UT_ASSERT(ivec.iv_vec.v_count != NULL);
        C2_ALLOC_ARR(ivec.iv_index, IOVEC_NR);
        C2_UT_ASSERT(ivec.iv_index != NULL);

        for (cnt = 0; cnt < IOVEC_NR; ++cnt) {
                iovec_arr[cnt].iov_base = &rc;
                iovec_arr[cnt].iov_len  = IOVEC_BUF_LEN;

                ivec.iv_index[cnt] = FILE_START_INDEX - cnt * IOVEC_BUF_LEN;
                ivec.iv_vec.v_count[cnt] = IOVEC_BUF_LEN;
        }

	/* io_request attributes test. */
        rc = io_request_init(&req, &lfile, iovec_arr, &ivec, IRT_WRITE);
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(req.ir_rc == 0);
        C2_UT_ASSERT(req.ir_file == &lfile);
        C2_UT_ASSERT(req.ir_type == IRT_READ);
        C2_UT_ASSERT(req.ir_iovec == iovec_arr);
        C2_UT_ASSERT(req.ir_ivec.iv_vec.v_nr == IOVEC_NR);
        C2_UT_ASSERT(req.ir_magic == C2_T1FS_IOREQ_MAGIC);
        C2_UT_ASSERT(req.ir_sm.sm_state == IRS_INITIALIZED);
        C2_UT_ASSERT(req.ir_iomaps != NULL);
        C2_UT_ASSERT(req.ir_iomap_nr == 1);
        C2_UT_ASSERT(req.ir_ivec.iv_index != NULL);
        C2_UT_ASSERT(req.ir_ivec.iv_vec.v_count != NULL);

        /* Index array should be sorted in increasing order of file offset. */
        for (cnt = 0; cnt < IOVEC_NR; ++cnt)
                C2_UT_ASSERT(req.ir_ivec.iv_index[cnt] <
                             req.ir_ivec.iv_index[cnt + 1]);

        /* nw_xfer_request attributes test. */
        C2_UT_ASSERT(req.ir_nwxfer.nxr_rc == 0);
        C2_UT_ASSERT(req.ir_nwxfer.nxr_bytes == 0);
        C2_UT_ASSERT(req.ir_nwxfer.nxr_iofop_nr == 0);
        C2_UT_ASSERT(req.ir_nwxfer.nxr_magic == C2_T1FS_NWREQ_MAGIC);
        C2_UT_ASSERT(req.ir_nwxfer.nxr_state == NXS_INITIALIZED);

        /* pargrp_iomap attributes test. */
	rc = ioreq_iomaps_prepare(&req);
	C2_UT_ASSERT(rc == 0);

	C2_UT_ASSERT(req.ir_iomap_nr == 1);
	map = req.ir_iomaps[0];
	C2_UT_ASSERT(map->pi_magic == C2_T1FS_PGROUP_MAGIC);
	C2_UT_ASSERT(map->pi_grpid == 0);
	C2_UT_ASSERT(c2_vec_count(&map->pi_ivec.iv_vec) ==
		     PAGE_CACHE_SIZE * 2);
	C2_UT_ASSERT(map->pi_ivec.iv_index[0] == PAGE_CACHE_SIZE * 4);
	C2_UT_ASSERT(map->pi_ivec.iv_index[1] == PAGE_CACHE_SIZE * 5);
	C2_UT_ASSERT(map->pi_ivec.iv_vec.v_count[0] == PAGE_CACHE_SIZE);
	C2_UT_ASSERT(map->pi_ivec.iv_vec.v_count[1] == PAGE_CACHE_SIZE);
	C2_UT_ASSERT(map->pi_rtype      == PIR_READOLD);
	C2_UT_ASSERT(map->pi_databufs   != NULL);
	C2_UT_ASSERT(map->pi_paritybufs != NULL);
	C2_UT_ASSERT(map->pi_ops        != NULL);
	C2_UT_ASSERT(map->pi_ioreq      == &req);
	C2_UT_ASSERT(map->pi_databufs[1][1]   != NULL);
	C2_UT_ASSERT(map->pi_databufs[1][2]   != NULL);
	C2_UT_ASSERT(map->pi_paritybufs[0][0] != NULL);
	C2_UT_ASSERT(map->pi_paritybufs[1][0] != NULL);
	C2_UT_ASSERT(map->pi_paritybufs[2][0] != NULL);

	cfid.f_container = 0;
	cfid.f_key	 = 5;
	size             = c2_vec_count(&map->pi_ivec.iv_vec) /
			   (LAY_N + 2 * LAY_K);

	/* target_ioreq attributes test. */
	rc = target_ioreq_init(&ti, &req.ir_nwxfer, &cfid, &session, size);
	C2_UT_ASSERT(rc       == 0);
	C2_UT_ASSERT(ti.ti_rc == 0);
	C2_UT_ASSERT(c2_fid_eq(&ti.ti_fid, &cfid));
	C2_UT_ASSERT(ti.ti_bytes   == 0);
	C2_UT_ASSERT(ti.ti_nwxfer  == &req.ir_nwxfer);
	C2_UT_ASSERT(ti.ti_session == &session);
	C2_UT_ASSERT(ti.ti_magic   == C2_T1FS_TIOREQ_MAGIC);
	C2_UT_ASSERT(iofops_tlist_is_empty(&ti.ti_iofops));
	C2_UT_ASSERT(ti.ti_ivec.iv_vec.v_nr    == ti.ti_bufvec.ov_vec.v_nr);
	C2_UT_ASSERT(ti.ti_ivec.iv_index       != NULL);
	C2_UT_ASSERT(ti.ti_ivec.iv_vec.v_count != NULL);
	C2_UT_ASSERT(ti.ti_pageattrs != NULL);
	C2_UT_ASSERT(ti.ti_ops       != NULL);

	/* io_req_fop attributes test. */
	C2_ALLOC_PTR(irfop);
	C2_UT_ASSERT(irfop != NULL);
	rc = io_req_fop_init(irfop, &ti); 
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(irfop->irf_magic == C2_T1FS_IOFOP_MAGIC);
	C2_UT_ASSERT(irfop->irf_tioreq == &ti);
	C2_UT_ASSERT(irfop->irf_ast.sa_cb == io_bottom_half);
	C2_UT_ASSERT(irfop->irf_iofop.if_fop.f_item.ri_ops == &c2t1fs_item_ops);
	C2_UT_ASSERT(irfop->irf_ast.sa_mach == &req.ir_sm);

	/* data_buf attributes test. */
	dbuf = data_buf_alloc_init(0);
	C2_UT_ASSERT(dbuf           != NULL);
	C2_UT_ASSERT(dbuf->db_flags == 0);
	C2_UT_ASSERT(dbuf->db_magic == C2_T1FS_DTBUF_MAGIC);
	C2_UT_ASSERT(dbuf->db_buf.b_addr != NULL);
	C2_UT_ASSERT(dbuf->db_buf.b_nob  == PAGE_CACHE_SIZE);
	C2_UT_ASSERT(dbuf->db_auxbuf.b_addr == NULL);
	C2_UT_ASSERT(dbuf->db_auxbuf.b_nob  == 0);

	data_buf_dealloc_fini(dbuf);
	dbuf = NULL;

	io_req_fop_fini(irfop);
	C2_UT_ASSERT(irfop->irf_magic       == 0);
	C2_UT_ASSERT(irfop->irf_tioreq      == NULL);
	C2_UT_ASSERT(irfop->irf_ast.sa_cb   == NULL);
	C2_UT_ASSERT(irfop->irf_ast.sa_mach == NULL);
	c2_free(irfop);
	irfop = NULL;

	target_ioreq_fini(&ti);
	C2_UT_ASSERT(ti.ti_magic     == 0);
	C2_UT_ASSERT(ti.ti_ops       == NULL);
	C2_UT_ASSERT(ti.ti_session   == NULL);
	C2_UT_ASSERT(ti.ti_nwxfer    == NULL);
	C2_UT_ASSERT(ti.ti_pageattrs == NULL);
	C2_UT_ASSERT(ti.ti_ivec.iv_index         == NULL);
	C2_UT_ASSERT(ti.ti_ivec.iv_vec.v_count   == NULL);
	C2_UT_ASSERT(ti.ti_bufvec.ov_buf         == NULL);
	C2_UT_ASSERT(ti.ti_bufvec.ov_vec.v_count == NULL);

	pargrp_iomap_fini(map);
	C2_UT_ASSERT(map->pi_ops   == NULL);
	C2_UT_ASSERT(map->pi_rtype == PIR_NONE);
	C2_UT_ASSERT(map->pi_magic == 0);
	C2_UT_ASSERT(map->pi_ivec.iv_index       == NULL);
	C2_UT_ASSERT(map->pi_ivec.iv_vec.v_count == NULL);
	C2_UT_ASSERT(map->pi_databufs   == NULL);
	C2_UT_ASSERT(map->pi_paritybufs == NULL);
	C2_UT_ASSERT(map->pi_ioreq      == NULL);

	c2_free(map);
	map = NULL;
	req.ir_iomaps[0] = NULL;
	req.ir_iomap_nr  = 0;

	io_request_fini(&req);
	C2_UT_ASSERT(req.ir_file   == NULL);
	C2_UT_ASSERT(req.ir_iovec  == NULL);
	C2_UT_ASSERT(req.ir_iomaps == NULL);
	C2_UT_ASSERT(req.ir_ops    == NULL);
	C2_UT_ASSERT(req.ir_ivec.iv_index       == NULL);
	C2_UT_ASSERT(req.ir_ivec.iv_vec.v_count == NULL);
	C2_UT_ASSERT(tioreqs_tlist_is_empty(&req.ir_nwxfer.nxr_tioreqs));

	C2_UT_ASSERT(req.ir_nwxfer.nxr_ops == NULL);
	C2_UT_ASSERT(req.ir_nwxfer.nxr_magic == 0);
}

static int file_io_ut_fini(void)
{
	c2_free(lfile.f_dentry);
	c2_layout_instance_fini(ci.ci_layout_instance);

        c2_addb_ctx_fini(&c2t1fs_addb);
        c2_sm_group_fini(&csb.csb_iogroup);
        c2_chan_fini(&csb.csb_iowait);

        /* Finalizes the c2_pdclust_layout type. */
        c2_layout_put(&pdlay->pl_base.sl_base);
        c2_layout_standard_types_unregister(&ldom);
        c2_layout_domain_fini(&ldom);
        c2_dbenv_fini(&testdb);
	return 0;
}

const struct c2_test_suite file_io_ut = {
        .ts_name  = "file-io-ut",
        .ts_init  = file_io_ut_init,
        .ts_fini  = file_io_ut_fini,
        .ts_tests = {
                {"basic_data_structures_test",  ds_test},
                //{"helper_routines_test",        helpers_test},
                //{"parity_group_structure_test", pargrp_iomap_test},
                //{"target_ioreq_test",           target_ioreq_test},
        },
};
C2_EXPORTED(file_io_ut);
