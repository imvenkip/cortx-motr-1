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
 * Production code : m0t1fs/linux_kernel/file.c
 * UT code         : m0t1fs/linux_kernel/ut/file.c
 *
 * The production code is added this way to build mero kernel module.
 * Both production code and UT code are part of Mero kernel module
 * but production code file is not directly added to Makefile as a file
 * that needs to be compiled.
 * Instead, UT code is added in Makefile for mero kernel module build
 * which subsequently adds production code file to mero kernel module.
 */
#include "m0t1fs/linux_kernel/file.c"

#include "ut/ut.h"     /* m0_test_suite */
#include "lib/chan.h"   /* m0_chan */
#include "lib/vec.h"    /* m0_indexvec */
#include "fid/fid.h"    /* m0_fid */
#include "lib/misc.h"   /* m0_rnd */
#include "lib/time.h"   /* m0_time_nanoseconds */
#include "lib/finject.h"
#include "layout/layout.h"      /* m0_layout_domain_init */
#include "layout/linear_enum.h" /* m0_layout_linear_enum */
#include <linux/dcache.h>       /* struct dentry */
#include "m0t1fs/linux_kernel/file_internal.h" /* io_request */
#include "m0t1fs/linux_kernel/m0t1fs.h" /* m0t1fs_sb */

enum {
        IOVEC_NR         = 4,
        IOVEC_BUF_LEN    = 1024,
        FILE_START_INDEX = 21340,

	/* Number of data units. */
	LAY_N		 = 3,

	/* Number of parity units. */
	LAY_K		 = 1,

	/* Number of members in pool. */
	LAY_P            = LAY_N + 2 * LAY_K,

	/* Unit Size = 12K. */
	UNIT_SIZE	 = 3 * PAGE_CACHE_SIZE,
	INDEXPG          = 2000,
	INDEXPG_STEP     = 5000,

	/* Data size for parity group = 12K * 3 = 36K. */
	DATA_SIZE        = UNIT_SIZE * LAY_N,
	FID_KEY		 = 3,
	ATTR_A_CONST	 = 1,
	ATTR_B_CONST	 = 1,
	DGMODE_IOVEC_NR  = 8,
};

static struct super_block            sb;
static struct m0t1fs_sb              csb;
static struct m0_pdclust_attr        pdattr;
static struct m0_pdclust_layout     *pdlay;
static struct m0_layout_linear_enum *llenum;
static struct m0_layout_linear_attr  llattr;
static struct m0t1fs_inode           ci;
static struct m0_layout_linear_attr  llattr;
static struct file                   lfile;
static struct m0t1fs_service_context ctx;
static struct m0_poolmach            poolmach;
static struct m0_rm_remote           creditor;
static struct m0t1fs_service_context msc;

M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);
M0_TL_DECLARE(rpcbulk, M0_INTERNAL, struct m0_rpc_bulk_buf);

int m0t1fs_layout_init(struct m0t1fs_sb *csb);
int m0t1fs_addb_mon_total_io_size_init(struct m0t1fs_sb *csb);
int m0t1fs_rpc_init(struct m0t1fs_sb *csb);
int m0t1fs_net_init(struct m0t1fs_sb *csb);
int m0t1fs_reqh_services_start(struct m0t1fs_sb *csb);

static int file_io_ut_init(void)
{
        int		  rc;
	uint64_t	  random;
	struct m0_layout *lay;

        M0_SET0(&sb);
        M0_SET0(&csb);
	M0_SET0(&creditor);
        m0_sm_group_init(&csb.csb_iogroup);
	sb.s_fs_info          = &csb;
        csb.csb_active        = true;
	csb.csb_nr_containers = LAY_P;
	csb.csb_pool_width    = LAY_P;
	csb.csb_next_key     = FID_KEY;
        m0_chan_init(&csb.csb_iowait, &csb.csb_iogroup.s_lock);
        m0_atomic64_set(&csb.csb_pending_io_nr, 0);
        io_bob_tlists_init();

        rc = m0t1fs_net_init(&csb);
        M0_ASSERT(rc == 0);

        rc = m0t1fs_rpc_init(&csb);
        M0_ASSERT(rc == 0);

        rc = m0t1fs_addb_mon_total_io_size_init(&csb);
        M0_ASSERT(rc == 0);

        rc = m0t1fs_layout_init(&csb);
        M0_ASSERT(rc == 0);
        rc = m0t1fs_reqh_services_start(&csb);
        M0_ASSERT(rc == 0);

        /* Tries to build a layout. */
        llattr = (struct m0_layout_linear_attr) {
                .lla_nr = LAY_N + 2 * LAY_K,
                .lla_A  = ATTR_A_CONST,
                .lla_B  = ATTR_B_CONST,
        };
        llenum = NULL;
        rc = m0_linear_enum_build(&csb.csb_layout_dom, &llattr,
			          &llenum);
        M0_ASSERT(rc == 0);

	random = m0_time_nanoseconds(m0_time_now());
        pdattr = (struct m0_pdclust_attr) {
                .pa_N         = LAY_N,
                .pa_K         = LAY_K,
                .pa_P         = LAY_N + 2 * LAY_K,
                .pa_unit_size = UNIT_SIZE,

        };
        m0_uint128_init(&pdattr.pa_seed, "upjumpandpumpim,");
        rc = m0_pdclust_build(&csb.csb_layout_dom, M0_DEFAULT_LAYOUT_ID,
			      &pdattr, &llenum->lle_base, &pdlay);
        M0_ASSERT(rc == 0);
        M0_ASSERT(pdlay != NULL);

        /* Initializes the m0t1fs inode and build layout instance. */
        M0_SET0(&ci);
        ci.ci_layout_id = M0_DEFAULT_LAYOUT_ID;
	csb.csb_cl_map.rm_ctx = &msc;
	m0t1fs_fid_alloc(&csb, &ci.ci_fid);
	m0t1fs_file_lock_init(&ci, &csb);

	lay = m0_pdl_to_layout(pdlay);
	M0_ASSERT(lay != NULL);

	rc = m0_layout_instance_build(lay, m0t1fs_inode_fid(&ci),
				      &ci.ci_layout_instance);
	M0_ASSERT(rc == 0);
	M0_ASSERT(ci.ci_layout_instance != NULL);

	M0_ALLOC_PTR(lfile.f_dentry);
	M0_ASSERT(lfile.f_dentry != NULL);
	lfile.f_dentry->d_inode = &ci.ci_inode;
	lfile.f_dentry->d_inode->i_sb = &sb;

	/* Sets the file size in inode. */
	ci.ci_inode.i_size = DATA_SIZE;

	M0_SET0(&poolmach);
	csb.csb_pool.po_mach = &poolmach;

	rc = m0_poolmach_init(csb.csb_pool.po_mach, NULL, NULL, NULL,
			      1, LAY_P, 1, LAY_K);
	M0_ASSERT(rc == 0);

	return 0;
}

static void ds_test(void)
{
        int                   rc;
        int                   cnt;
	struct m0_fid         cfid = M0_FID_INIT(0, 5);
	struct data_buf      *dbuf;
        struct io_request     req;
        struct iovec          iovec_arr[IOVEC_NR];
        struct m0_indexvec    ivec;
	struct pargrp_iomap  *map;
	struct target_ioreq   ti;
	struct io_req_fop    *irfop;
	struct m0_rpc_session session;

	M0_SET0(&req);
	rc = m0_indexvec_alloc(&ivec, ARRAY_SIZE(iovec_arr),
			       &m0t1fs_addb_ctx, 0);
        M0_UT_ASSERT(rc == 0);

        for (cnt = 0; cnt < IOVEC_NR; ++cnt) {
                iovec_arr[cnt].iov_base = &rc;
                iovec_arr[cnt].iov_len  = IOVEC_BUF_LEN;

                INDEX(&ivec, cnt) = FILE_START_INDEX - cnt * IOVEC_BUF_LEN;
                COUNT(&ivec, cnt) = IOVEC_BUF_LEN;
        }

	/* io_request attributes test. */
	rc = io_request_init(&req, &lfile, iovec_arr, &ivec, IRT_WRITE);
        M0_UT_ASSERT(rc == 0);
        M0_UT_ASSERT(req.ir_rc       == 0);
        M0_UT_ASSERT(req.ir_file     == &lfile);
        M0_UT_ASSERT(req.ir_type     == IRT_WRITE);
        M0_UT_ASSERT(req.ir_iovec    == iovec_arr);
        M0_UT_ASSERT(req.ir_magic    == M0_T1FS_IOREQ_MAGIC);
        M0_UT_ASSERT(req.ir_ivec.iv_vec.v_nr    == IOVEC_NR);
        M0_UT_ASSERT(req.ir_sm.sm_state         == IRS_INITIALIZED);
        M0_UT_ASSERT(req.ir_ivec.iv_index       != NULL);
        M0_UT_ASSERT(req.ir_ivec.iv_vec.v_count != NULL);

        /* Index array should be sorted in increasing order of file offset. */
        for (cnt = 0; cnt < IOVEC_NR - 1; ++cnt) {
                M0_UT_ASSERT(req.ir_ivec.iv_index[cnt] <
                             req.ir_ivec.iv_index[cnt + 1]);
	}

        /* nw_xfer_request attributes test. */
        M0_UT_ASSERT(req.ir_nwxfer.nxr_rc == 0);
        M0_UT_ASSERT(req.ir_nwxfer.nxr_bytes == 0);
        M0_UT_ASSERT(req.ir_nwxfer.nxr_iofop_nr == 0);
        M0_UT_ASSERT(req.ir_nwxfer.nxr_magic == M0_T1FS_NWREQ_MAGIC);
        M0_UT_ASSERT(req.ir_nwxfer.nxr_state == NXS_INITIALIZED);

        /* pargrp_iomap attributes test. */
	rc = ioreq_iomaps_prepare(&req);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(req.ir_iomap_nr == 1);
	map = req.ir_iomaps[0];
	M0_UT_ASSERT(map->pi_magic == M0_T1FS_PGROUP_MAGIC);
	M0_UT_ASSERT(map->pi_grpid == 0);

	/*
	 * Input index vector :
	 * {{21340, 1024}, {20316, 1024}, {19292, 1024}, {18268, 1024}}
	 */
	M0_UT_ASSERT(m0_vec_count(&map->pi_ivec.iv_vec) ==
		     PAGE_CACHE_SIZE * 2);

        /*
         * Given input index vector results into 2 pages.
         * {{16384, 4096}, {20480, 4096}}
         * The data matrix in this case is 3 x 3.
         * Since these indices map to page id
         * 5(maps to element [1][1]) and 6(maps to element [2][1])
         * in the data matrix.
         * Rest all pages in data matrix will be NULL.
         */
	M0_UT_ASSERT(map->pi_ivec.iv_index[0] == PAGE_CACHE_SIZE * 4);
	M0_UT_ASSERT(map->pi_ivec.iv_vec.v_count[0] == 2 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map->pi_databufs   != NULL);
	M0_UT_ASSERT(map->pi_paritybufs != NULL);
	M0_UT_ASSERT(map->pi_ops        != NULL);
	M0_UT_ASSERT(map->pi_ioreq      == &req);
	M0_UT_ASSERT(map->pi_databufs[0][0]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[1][0]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[2][0]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[0][1]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[1][1]   != NULL);
	M0_UT_ASSERT(map->pi_databufs[2][1]   != NULL);
	M0_UT_ASSERT(map->pi_databufs[0][2]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[1][2]   == NULL);
	M0_UT_ASSERT(map->pi_databufs[2][2]   == NULL);
	M0_UT_ASSERT(map->pi_paritybufs[0][0] != NULL);
	M0_UT_ASSERT(map->pi_paritybufs[1][0] != NULL);
	M0_UT_ASSERT(map->pi_paritybufs[2][0] != NULL);

        m0_fid_set(&cfid, 0, 5);

	/* target_ioreq attributes test. */
	rc = target_ioreq_init(&ti, &req.ir_nwxfer, &cfid, &session, UNIT_SIZE);
	M0_UT_ASSERT(rc       == 0);
	M0_UT_ASSERT(ti.ti_rc == 0);
	M0_UT_ASSERT(m0_fid_eq(&ti.ti_fid, &cfid));
	M0_UT_ASSERT(ti.ti_parbytes    == 0);
	M0_UT_ASSERT(ti.ti_databytes   == 0);
	M0_UT_ASSERT(ti.ti_nwxfer  == &req.ir_nwxfer);
	M0_UT_ASSERT(ti.ti_session == &session);
	M0_UT_ASSERT(ti.ti_magic   == M0_T1FS_TIOREQ_MAGIC);
	M0_UT_ASSERT(iofops_tlist_is_empty(&ti.ti_iofops));
	M0_UT_ASSERT(ti.ti_ivec.iv_index       != NULL);
	M0_UT_ASSERT(ti.ti_ivec.iv_vec.v_count != NULL);
	M0_UT_ASSERT(ti.ti_pageattrs != NULL);
	M0_UT_ASSERT(ti.ti_ops       != NULL);

	/* io_req_fop attributes test. */
	M0_ALLOC_PTR(irfop);
	M0_UT_ASSERT(irfop != NULL);
	ioreq_sm_state_set(&req, IRS_LOCK_ACQUIRED);
	ioreq_sm_state_set(&req, IRS_READING);
	rc = io_req_fop_init(irfop, &ti, PA_DATA);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(irfop->irf_magic == M0_T1FS_IOFOP_MAGIC);
	M0_UT_ASSERT(irfop->irf_tioreq == &ti);
	M0_UT_ASSERT(irfop->irf_ast.sa_cb == io_bottom_half);
	M0_UT_ASSERT(irfop->irf_iofop.if_fop.f_item.ri_ops == &m0t1fs_item_ops);
	M0_UT_ASSERT(irfop->irf_ast.sa_mach == &req.ir_sm);

	/* data_buf attributes test. */
	dbuf = data_buf_alloc_init(0);
	M0_UT_ASSERT(dbuf           != NULL);
	M0_UT_ASSERT(dbuf->db_flags == 0);
	M0_UT_ASSERT(dbuf->db_magic == M0_T1FS_DTBUF_MAGIC);
	M0_UT_ASSERT(dbuf->db_buf.b_addr != NULL);
	M0_UT_ASSERT(dbuf->db_buf.b_nob  == PAGE_CACHE_SIZE);
	M0_UT_ASSERT(dbuf->db_auxbuf.b_addr == NULL);
	M0_UT_ASSERT(dbuf->db_auxbuf.b_nob  == 0);

	data_buf_dealloc_fini(dbuf);
	dbuf = NULL;

	io_req_fop_fini(irfop);
	M0_UT_ASSERT(irfop->irf_tioreq      == NULL);
	M0_UT_ASSERT(irfop->irf_ast.sa_cb   == NULL);
	M0_UT_ASSERT(irfop->irf_ast.sa_mach == NULL);
	m0_free0(&irfop);

	ti.ti_ivec.iv_vec.v_nr = page_nr(UNIT_SIZE);
	target_ioreq_fini(&ti);
	M0_UT_ASSERT(ti.ti_magic     == 0);
	M0_UT_ASSERT(ti.ti_ops       == NULL);
	M0_UT_ASSERT(ti.ti_session   == NULL);
	M0_UT_ASSERT(ti.ti_nwxfer    == NULL);
	M0_UT_ASSERT(ti.ti_pageattrs == NULL);
	M0_UT_ASSERT(ti.ti_ivec.iv_index         == NULL);
	M0_UT_ASSERT(ti.ti_ivec.iv_vec.v_count   == NULL);
	M0_UT_ASSERT(ti.ti_bufvec.ov_buf         == NULL);
	M0_UT_ASSERT(ti.ti_bufvec.ov_vec.v_count == NULL);

	pargrp_iomap_fini(map);
	M0_UT_ASSERT(map->pi_ops   == NULL);
	M0_UT_ASSERT(map->pi_rtype == PIR_NONE);
	M0_UT_ASSERT(map->pi_magic == 0);
	M0_UT_ASSERT(map->pi_ivec.iv_index       == NULL);
	M0_UT_ASSERT(map->pi_ivec.iv_vec.v_count == NULL);
	M0_UT_ASSERT(map->pi_databufs   == NULL);
	M0_UT_ASSERT(map->pi_paritybufs == NULL);
	M0_UT_ASSERT(map->pi_ioreq      == NULL);

	m0_free0(&map);
	req.ir_iomaps[0] = NULL;
	req.ir_iomap_nr  = 0;

	ioreq_sm_state_set(&req, IRS_READ_COMPLETE);
	ioreq_sm_state_set(&req, IRS_REQ_COMPLETE);
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	M0_UT_ASSERT(tioreqht_htable_is_empty(&req.ir_nwxfer.nxr_tioreqs_hash));
	io_request_fini(&req);
	M0_UT_ASSERT(req.ir_file   == NULL);
	M0_UT_ASSERT(req.ir_iovec  == NULL);
	M0_UT_ASSERT(req.ir_iomaps == NULL);
	M0_UT_ASSERT(req.ir_ops    == NULL);
	M0_UT_ASSERT(req.ir_ivec.iv_index       == NULL);
	M0_UT_ASSERT(req.ir_ivec.iv_vec.v_count == NULL);

	M0_UT_ASSERT(req.ir_nwxfer.nxr_ops == NULL);
	M0_UT_ASSERT(req.ir_nwxfer.nxr_magic == 0);
	m0_indexvec_free(&ivec);
}

static int dummy_readrest(struct pargrp_iomap *map)
{
	return 0;
}

static void pargrp_iomap_test(void)
{
	int                     rc;
	int                     cnt;
	uint32_t	        row;
	uint32_t	        col;
	uint64_t	        nr;
	m0_bindex_t             index;
	struct iovec            iovec_arr[LAY_N * UNIT_SIZE / PAGE_CACHE_SIZE];
	struct io_request       req;
	struct m0_indexvec      ivec;
	struct m0_ivec_cursor   cur;
	struct pargrp_iomap     map;
	struct pargrp_iomap_ops piops;

	rc = m0_indexvec_alloc(&ivec, ARRAY_SIZE(iovec_arr),
			       &m0t1fs_addb_ctx, 0);
	M0_UT_ASSERT(rc == 0);

	for (cnt = 0; cnt < ARRAY_SIZE(iovec_arr); ++cnt) {
		iovec_arr[cnt].iov_base  = &rc;
		iovec_arr[cnt].iov_len   = PAGE_CACHE_SIZE;

		INDEX(&ivec, cnt) = (m0_bindex_t)(cnt * PAGE_CACHE_SIZE);
		COUNT(&ivec, cnt) = PAGE_CACHE_SIZE;
	}

	rc = io_request_init(&req, &lfile, iovec_arr, &ivec, IRT_WRITE);
	M0_UT_ASSERT(rc == 0);

	rc = pargrp_iomap_init(&map, &req, 0);
	M0_UT_ASSERT(rc == 0);

	rc = pargrp_iomap_databuf_alloc(&map, 0, 0);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(map.pi_databufs[0][0] != NULL);
	M0_UT_ASSERT(map.pi_databufs[0][0]->db_buf.b_addr != NULL);
	M0_UT_ASSERT(map.pi_databufs[0][0]->db_buf.b_nob  == PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_databufs[0][0]->db_flags == 0);
	data_buf_dealloc_fini(map.pi_databufs[0][0]);
	map.pi_databufs[0][0] = NULL;

	for (cnt = 0; cnt < ARRAY_SIZE(iovec_arr); ++cnt) {
		INDEX(&map.pi_ivec, cnt) = (m0_bindex_t)(cnt * PAGE_CACHE_SIZE);
		COUNT(&map.pi_ivec, cnt) = PAGE_CACHE_SIZE;
		++map.pi_ivec.iv_vec.v_nr;

		rc = pargrp_iomap_seg_process(&map, cnt, true);
		M0_UT_ASSERT(rc == 0);

		page_pos_get(&map, INDEX(&map.pi_ivec, cnt), &row, &col);
		M0_UT_ASSERT(map.pi_databufs[row][col] != NULL);
		M0_UT_ASSERT(map.pi_databufs[row][col]->db_flags & PA_WRITE);
		M0_UT_ASSERT(map.pi_databufs[row][col]->db_flags &
			     PA_FULLPAGE_MODIFY);
		M0_UT_ASSERT(map.pi_databufs[row][col]->db_flags & ~PA_READ);
	}

	/* Checks if given segment falls in pargrp_iomap::pi_ivec. */
	M0_UT_ASSERT(pargrp_iomap_spans_seg (&map, 0,     PAGE_CACHE_SIZE));
	M0_UT_ASSERT(pargrp_iomap_spans_seg (&map, 1234,  10));
	M0_UT_ASSERT(!pargrp_iomap_spans_seg(&map, PAGE_CACHE_SIZE * 10,
				             PAGE_CACHE_SIZE));

	/*
	 * Checks if number of pages completely spanned by index vector
	 * is correct.
	 */
	nr = pargrp_iomap_fullpages_count(&map);
	M0_UT_ASSERT(nr == LAY_N * UNIT_SIZE / PAGE_CACHE_SIZE);

	/* Checks if all parity buffers are allocated properly. */
	map.pi_rtype = PIR_READOLD;
	rc = pargrp_iomap_paritybufs_alloc(&map);
	M0_UT_ASSERT(rc == 0);

	for (row = 0; row < parity_row_nr(pdlay); ++row) {
		for (col = 0; col < parity_col_nr(pdlay); ++col) {
			M0_UT_ASSERT(map.pi_paritybufs[row][col] != NULL);
			M0_UT_ASSERT(map.pi_paritybufs[row][col]->db_flags &
				     PA_WRITE);
			M0_UT_ASSERT(map.pi_paritybufs[row][col]->db_flags &
				     PA_READ);
		}
	}

	/*
	 * Checks if any auxiliary buffers are allocated.
	 * There should be no auxiliary buffers at all.
	 */
	rc = pargrp_iomap_readold_auxbuf_alloc(&map);
	M0_UT_ASSERT(rc == 0);

	for (row = 0; row < data_row_nr(pdlay); ++row) {
		for (col = 0; col < data_col_nr(pdlay); ++col) {
			M0_UT_ASSERT(map.pi_databufs[row][col]->db_flags &
				     ~PA_PARTPAGE_MODIFY);
		}
	}

	/* pargrp_iomap_fini() deallocates all data_buf structures in it. */
	pargrp_iomap_fini(&map);
	m0_indexvec_free(&ivec);
	m0_sm_state_set(&req.ir_sm, IRS_REQ_COMPLETE);
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	io_request_fini(&req);

	rc = m0_indexvec_alloc(&ivec, IOVEC_NR, &m0t1fs_addb_ctx, 0);
	M0_UT_ASSERT(rc == 0);

	index = INDEXPG;
	/*
	 * Segments {2000, 7000}, {9000, 14000}, {16000, 21000}, {23000, 28000}}
	 */
	for (cnt = 0; cnt < IOVEC_NR; ++cnt) {

		iovec_arr[cnt].iov_base  = &rc;
		iovec_arr[cnt].iov_len   = INDEXPG_STEP;

		INDEX(&ivec, cnt) = index;
		COUNT(&ivec, cnt) = INDEXPG_STEP;
		index += COUNT(&ivec, cnt) + INDEXPG;
	}

	rc = io_request_init(&req, &lfile, iovec_arr, &ivec, IRT_WRITE);
	M0_UT_ASSERT(rc == 0);

	rc = pargrp_iomap_init(&map, &req, 0);
	M0_UT_ASSERT(rc == 0);

	piops = (struct pargrp_iomap_ops) {
		.pi_populate             = pargrp_iomap_populate,
		.pi_spans_seg            = pargrp_iomap_spans_seg,
		/* Dummy UT function. */
		.pi_readrest             = dummy_readrest,
		.pi_fullpages_find       = pargrp_iomap_fullpages_count,
		.pi_seg_process          = pargrp_iomap_seg_process,
		.pi_readold_auxbuf_alloc = pargrp_iomap_readold_auxbuf_alloc,
		.pi_parity_recalc        = pargrp_iomap_parity_recalc,
		.pi_paritybufs_alloc     = pargrp_iomap_paritybufs_alloc,
	};
	m0_ivec_cursor_init(&cur, &req.ir_ivec);
	map.pi_ops = &piops;
	rc = pargrp_iomap_populate(&map, &req.ir_ivec, &cur);
	M0_UT_ASSERT(map.pi_databufs != NULL);
	M0_UT_ASSERT(m0_vec_count(&map.pi_ivec.iv_vec) > 0);
	M0_UT_ASSERT(map.pi_grpid == 0);
	M0_UT_ASSERT(map.pi_ivec.iv_vec.v_nr == 4);

	M0_UT_ASSERT(map.pi_ivec.iv_index[0] == 0);
	M0_UT_ASSERT(map.pi_ivec.iv_vec.v_count[0] == 2 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_databufs[0][0] != NULL);
	M0_UT_ASSERT(map.pi_databufs[1][0] != NULL);

	M0_UT_ASSERT(map.pi_ivec.iv_index[1] == 2 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_ivec.iv_vec.v_count[1] == 2 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_databufs[2][0] != NULL);
	M0_UT_ASSERT(map.pi_databufs[0][1] != NULL);

	M0_UT_ASSERT(map.pi_ivec.iv_index[2] == 4 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_ivec.iv_vec.v_count[2] == 2 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_databufs[1][1] != NULL);
	M0_UT_ASSERT(map.pi_databufs[2][1] != NULL);

	M0_UT_ASSERT(map.pi_ivec.iv_index[3] == 6 * PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_ivec.iv_vec.v_count[3] == PAGE_CACHE_SIZE);
	M0_UT_ASSERT(map.pi_databufs[0][2] != NULL);

	rc = pargrp_iomap_readrest(&map);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(map.pi_ivec.iv_index[3] + map.pi_ivec.iv_vec.v_count[3] ==
		     data_size(pdlay));
	M0_UT_ASSERT(map.pi_databufs[1][2] != NULL);
	M0_UT_ASSERT(map.pi_databufs[1][2]->db_flags & PA_READ);
	M0_UT_ASSERT(map.pi_databufs[2][2] != NULL);
	M0_UT_ASSERT(map.pi_databufs[2][2]->db_flags & PA_READ);

	req.ir_sm.sm_state      = IRS_REQ_COMPLETE;
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	io_request_fini(&req);
}

static void helpers_test(void)
{
	uint32_t            i;
	uint32_t            j;
	uint32_t            row;
	uint32_t            col;
	m0_bindex_t         start = 0;
	struct pargrp_iomap map;
	struct io_request   req;

	M0_UT_ASSERT(parity_units_page_nr(pdlay) ==
			(UNIT_SIZE >> PAGE_CACHE_SHIFT) * LAY_K);

	M0_UT_ASSERT(data_row_nr(pdlay)   == UNIT_SIZE >> PAGE_CACHE_SHIFT);
	M0_UT_ASSERT(data_col_nr(pdlay)   == LAY_N);
	M0_UT_ASSERT(parity_col_nr(pdlay) == LAY_K);

	M0_UT_ASSERT(round_down(1000, 1024) == 0);
	M0_UT_ASSERT(round_up(2000, 1024)   == 2048);
	M0_UT_ASSERT(round_up(0, 2)         == 0);
	M0_UT_ASSERT(round_down(1023, 1024) == 0);
	M0_UT_ASSERT(round_up(1025, 1024)   == 2048);
	M0_UT_ASSERT(round_down(1024, 1024) == round_up(1024, 1024));

	req.ir_file  = &lfile;
	map = (struct pargrp_iomap) {
		.pi_ioreq = &req,
			.pi_grpid = 0,
	};

	for (i = 0; i < UNIT_SIZE / PAGE_CACHE_SIZE; ++i) {
		for (j = 0; j < LAY_N; ++j) {
			page_pos_get(&map, start, &row, &col);
			M0_UT_ASSERT(row == j && col == i);
			start += PAGE_CACHE_SIZE;
		}
	}
}

static void nw_xfer_ops_test(void)
{
	int                        cnt;
	int                        rc;
	m0_bindex_t                index;
	struct io_request          req;
	struct iovec               iovec_arr[LAY_N * UNIT_SIZE >>
		                             PAGE_CACHE_SHIFT];
	struct m0_indexvec         ivec;
	struct target_ioreq       *ti;
	struct target_ioreq       *ti1;
	struct m0_pdclust_src_addr src;
	struct m0_pdclust_tgt_addr tgt;

	M0_SET0(&req);
	M0_SET0(&src);
	M0_SET0(&tgt);
	rc = m0_indexvec_alloc(&ivec, ARRAY_SIZE(iovec_arr),
			       &m0t1fs_addb_ctx, 0);
	M0_UT_ASSERT(rc == 0);

	index = 0;
	for (cnt = 0; cnt < LAY_N * UNIT_SIZE >> PAGE_CACHE_SHIFT; ++cnt) {
		iovec_arr[cnt].iov_base  = &rc;
		iovec_arr[cnt].iov_len   = PAGE_CACHE_SIZE;

		ivec.iv_index[cnt]       = index;
		ivec.iv_vec.v_count[cnt] = PAGE_CACHE_SIZE;
		index += PAGE_CACHE_SIZE;
	}

	rc = io_request_init(&req, &lfile, iovec_arr, &ivec, IRT_WRITE);
	M0_UT_ASSERT(rc == 0);

	rc = ioreq_iomaps_prepare(&req);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(req.ir_iomap_nr == 1);
	M0_UT_ASSERT(req.ir_iomaps[0] != NULL);

	for (cnt = 1; cnt <= LAY_P; ++cnt)
		csb.csb_cl_map.ios_map[cnt] = &ctx;

	src.sa_unit = 0;

	/* Test for nw_xfer_tioreq_map. */
	rc = nw_xfer_tioreq_map(&req.ir_nwxfer, &src, &tgt, &ti);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(!tioreqht_htable_is_empty(&req.ir_nwxfer.
				nxr_tioreqs_hash));
	M0_UT_ASSERT(ti->ti_ivec.iv_index != NULL);
	M0_UT_ASSERT(ti->ti_ivec.iv_vec.v_count != NULL);
	M0_UT_ASSERT(ti->ti_bufvec.ov_vec.v_count != NULL);
	M0_UT_ASSERT(ti->ti_bufvec.ov_buf != NULL);
	M0_UT_ASSERT(ti->ti_pageattrs != NULL);

	/* Test for nw_xfer_io_distribute. */
	rc = nw_xfer_io_distribute(&req.ir_nwxfer);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(tioreqht_htable_size(&req.ir_nwxfer.nxr_tioreqs_hash) ==
		     LAY_P - LAY_K);
	m0_htable_for(tioreqht, ti, &req.ir_nwxfer.nxr_tioreqs_hash) {
		M0_UT_ASSERT(ti->ti_nwxfer == &req.ir_nwxfer);
		M0_UT_ASSERT(ti->ti_ops != NULL);

		for (cnt = 0; cnt < ti->ti_ivec.iv_vec.v_nr; ++cnt) {
			M0_UT_ASSERT((ti->ti_ivec.iv_index[cnt] &
				     (PAGE_CACHE_SIZE - 1)) == 0);
			M0_UT_ASSERT(ti->ti_ivec.iv_vec.v_count[cnt] ==
				     PAGE_CACHE_SIZE);
		}
	} m0_htable_endfor;

	m0_htable_for(tioreqht, ti1, &req.ir_nwxfer.nxr_tioreqs_hash) {
		tioreqht_htable_del(&req.ir_nwxfer.nxr_tioreqs_hash, ti1);
	} m0_htable_endfor;

	ioreq_iomaps_destroy(&req);
	req.ir_sm.sm_state      = IRS_REQ_COMPLETE;
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	io_request_fini(&req);
	m0_indexvec_free(&ivec);
}

void m0t1fs_layout_fini(struct m0t1fs_sb *csb);
void m0t1fs_addb_mon_total_io_size_fini(struct m0t1fs_sb *csb);
void m0t1fs_rpc_fini(struct m0t1fs_sb *csb);
void m0t1fs_net_fini(struct m0t1fs_sb *csb);

static int file_io_ut_fini(void)
{
	m0t1fs_file_lock_fini(&ci);
	m0_free(lfile.f_dentry);
	m0_layout_instance_fini(ci.ci_layout_instance);

	m0_addb_ctx_fini(&m0t1fs_addb_ctx);
	m0_chan_fini_lock(&csb.csb_iowait);
	m0_sm_group_fini(&csb.csb_iogroup);

	/* Finalizes the m0_pdclust_layout type. */
	m0_layout_put(&pdlay->pl_base.sl_base);
	m0_poolmach_fini(csb.csb_pool.po_mach);

	m0t1fs_layout_fini(&csb);
	m0t1fs_addb_mon_total_io_size_fini(&csb);
	m0_reqh_services_terminate(&csb.csb_reqh);
	m0t1fs_rpc_fini(&csb);
	m0t1fs_net_fini(&csb);

	return 0;
}

static void target_ioreq_test(void)
{
	struct target_ioreq        ti;
	struct io_request          req;
	uint64_t                   size;
	struct m0_fid              cfid = M0_FID_INIT(0, 5);
	struct m0_rpc_session      session;
	struct m0_rpc_conn         conn;
	struct io_req_fop         *irfop;
	int		           cnt;
	int                        rc;
	void		          *aligned_buf;
	struct iovec               iovec_arr[IOVEC_NR];
	struct m0_indexvec        *ivec;
	struct pargrp_iomap       *map;
	uint32_t                   row;
	uint32_t                   col;
	struct data_buf           *buf;
	struct m0_pdclust_src_addr src;
	struct m0_pdclust_tgt_addr tgt;

	/* Checks working of target_ioreq_iofops_prepare() */

	size = IOVEC_NR * PAGE_CACHE_SIZE;
	req.ir_sm.sm_state = IRS_READING;

	conn.c_rpc_machine = &csb.csb_rpc_machine;
	session.s_conn = &conn;

	aligned_buf = m0_alloc_aligned(M0_0VEC_ALIGN, M0_0VEC_SHIFT);

        io_request_bob_init(&req);
	req.ir_file = &lfile;
        nw_xfer_request_init(&req.ir_nwxfer);

	rc = target_ioreq_init(&ti, &req.ir_nwxfer, &cfid, &session, size);
	M0_UT_ASSERT(rc == 0);

        for (cnt = 0; cnt < IOVEC_NR; ++cnt) {
		iovec_arr[cnt].iov_base  = aligned_buf;
		iovec_arr[cnt].iov_len   = PAGE_CACHE_SIZE;
		ti.ti_bufvec.ov_buf[cnt] = aligned_buf;
		COUNT(&ti.ti_ivec, cnt)  = PAGE_CACHE_SIZE;
		INDEX(&ti.ti_ivec, cnt)  = cnt * PAGE_CACHE_SIZE;
		ti.ti_pageattrs[cnt]     = PA_READ | PA_DATA;
	}
	SEG_NR(&ti.ti_ivec)  = IOVEC_NR;

	rc = target_ioreq_iofops_prepare(&ti, PA_DATA);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(ti.ti_nwxfer->nxr_iofop_nr == 1);

	m0_tl_for(iofops, &ti.ti_iofops, irfop) {
		struct m0_rpc_bulk *rbulk = &irfop->irf_iofop.if_rbulk;
		M0_UT_ASSERT(!m0_tlist_is_empty(&rpcbulk_tl,
						&rbulk->rb_buflist));
		M0_UT_ASSERT(m0_io_fop_size_get(&irfop->irf_iofop.if_fop) <=
			m0_rpc_session_get_max_item_payload_size(&session));
	} m0_tl_endfor;

	m0_tl_teardown(iofops, &ti.ti_iofops, irfop) {
		struct m0_io_fop *iofop = &irfop->irf_iofop;

		req.ir_nwxfer.nxr_rdbulk_nr -=
			rpcbulk_tlist_length(&iofop->if_rbulk.rb_buflist);
                irfop_fini(irfop);
		m0_io_fop_fini(iofop);
		M0_CNT_DEC(req.ir_nwxfer.nxr_iofop_nr);
	}

	/* Checks allocation failure. */

	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 1, 1);
	rc = target_ioreq_iofops_prepare(&ti, PA_DATA);
	M0_UT_ASSERT(rc == -ENOMEM);

	/* Checks allocation failure in m0_rpc_bulk_buf_add(). */

	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 2, 1);
	rc = target_ioreq_iofops_prepare(&ti, PA_DATA);
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_disable("m0_alloc", "fail_allocation");

	/* Finalisation */
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	nw_xfer_request_fini(&req.ir_nwxfer);
        io_request_bob_fini(&req);

	/* Checks working of target_ioreq_seg_add() */
	ivec = &ti.ti_ivec;
	rc = io_request_init(&req, &lfile, iovec_arr, ivec, IRT_WRITE);
	M0_UT_ASSERT(rc == 0);

	rc = ioreq_iomaps_prepare(&req);
	M0_UT_ASSERT(rc == 0);
	map = req.ir_iomaps[0];

	/* Addition of data buffer */
	page_pos_get(map, 0, &row, &col);
	buf = map->pi_databufs[row][col];
	M0_UT_ASSERT(row == 0);
	M0_UT_ASSERT(col == 0);
	SEG_NR(&ti.ti_ivec) = 0;

	for (cnt = 0; cnt < IOVEC_NR; ++cnt)
		ti.ti_pageattrs[cnt] &= ~(PA_DATA | PA_PARITY);

	src.sa_group = 0;
	src.sa_unit  = 0;
	tgt.ta_frame = 0;
	tgt.ta_obj   = 0;

	target_ioreq_seg_add(&ti, &src, &tgt, 0, PAGE_CACHE_SIZE, map);
	M0_UT_ASSERT(1 == SEG_NR(&ti.ti_ivec));
	M0_UT_ASSERT(ti.ti_bufvec.ov_buf[0] == buf->db_buf.b_addr);
	M0_UT_ASSERT(ti.ti_pageattrs[0] & PA_DATA);

	/* Set gob_offset to COUNT(&ti.ti_ivec, 0) */
	page_pos_get(map, COUNT(&ti.ti_ivec, 0), &row, &col);
	buf = map->pi_databufs[row][col];

	target_ioreq_seg_add(&ti, &src, &tgt, COUNT(&ti.ti_ivec, 0),
			     PAGE_CACHE_SIZE, map);
	M0_UT_ASSERT(2 == SEG_NR(&ti.ti_ivec));
	M0_UT_ASSERT(ti.ti_bufvec.ov_buf[1] == buf->db_buf.b_addr);
	M0_UT_ASSERT(ti.ti_pageattrs[1] & PA_DATA);

	/* Addition of parity buffer */
	buf = map->pi_paritybufs[page_id(0)]
		[LAY_N % data_col_nr(pdlay)];

	src.sa_unit  = LAY_N;
	target_ioreq_seg_add(&ti, &src, &tgt, 0, PAGE_CACHE_SIZE, map);
	M0_UT_ASSERT(3 == SEG_NR(&ti.ti_ivec));
	M0_UT_ASSERT(ti.ti_bufvec.ov_buf[2] == buf->db_buf.b_addr);
	M0_UT_ASSERT(ti.ti_pageattrs[2] & PA_PARITY);

	target_ioreq_fini(&ti);
	pargrp_iomap_fini(map);
	m0_free0(&map);
	req.ir_iomaps[0] = NULL;
	req.ir_iomap_nr  = 0;

	m0_sm_state_set(&req.ir_sm, IRS_REQ_COMPLETE);
	req.ir_nwxfer.nxr_state = NXS_COMPLETE;
	req.ir_nwxfer.nxr_bytes = 1;
	io_request_fini(&req);

	m0_free_aligned(aligned_buf, M0_0VEC_ALIGN, M0_0VEC_SHIFT);
}

static void dgmode_readio_test(void)
{
	int                         rc;
	int                         cnt;
	char                        content[LAY_P - 1] = {'b', 'c', 'd', 'e'};
	char                       *cont;
	uint32_t                    row;
	uint32_t                    col;
	uint64_t                    pgcur = 0;
	uint64_t                    key;
	struct iovec                iovec_arr[DGMODE_IOVEC_NR];
	struct m0_fop              *reply;
	struct io_request          *req;
	struct io_req_fop          *irfop;
	struct m0_indexvec          ivec;
	struct m0_rpc_conn         *conn;
	struct m0_rpc_bulk         *rbulk;
	struct pargrp_iomap        *map;
	struct target_ioreq        *ti;
	struct m0_rpc_session      *session;
	struct m0_layout_enum      *le;
	struct dgmode_rwvec         dgvec_tmp;
	struct m0_rpc_bulk_buf     *rbuf;
	struct m0_pdclust_layout   *play;
	struct m0_pdclust_src_addr  src;
	struct m0_pdclust_tgt_addr  tgt;
	struct m0_fop_cob_rw_reply *rw_rep;

	M0_ALLOC_PTR(req);
	M0_UT_ASSERT(req != NULL);

	rc = m0_indexvec_alloc(&ivec, ARRAY_SIZE(iovec_arr), NULL, 0);
	M0_UT_ASSERT(rc == 0);

	/* 8 segments covering a parity group each. */
	for (cnt = 0; cnt < DGMODE_IOVEC_NR; ++cnt) {
		iovec_arr[cnt].iov_base = &rc;
		iovec_arr[cnt].iov_len  = PAGE_CACHE_SIZE;

		INDEX(&ivec, cnt) = cnt * UNIT_SIZE * LAY_N;
		COUNT(&ivec, cnt) = iovec_arr[cnt].iov_len;
	}

	ci.ci_inode.i_size = 2 * DATA_SIZE * DGMODE_IOVEC_NR;
	rc = io_request_init(req, &lfile, iovec_arr, &ivec, IRT_READ);
	M0_UT_ASSERT(rc == 0);

	/* Creates all pargrp_iomap structures. */
	rc = ioreq_iomaps_prepare(req);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(req->ir_iomap_nr == DGMODE_IOVEC_NR);

	/* Spawns and initialises all target_ioreq objects. */
	rc = req->ir_nwxfer.nxr_ops->nxo_distribute(&req->ir_nwxfer);
	M0_UT_ASSERT(rc == 0);

	ioreq_sm_state_set(req, IRS_LOCK_ACQUIRED);
	ioreq_sm_state_set(req, IRS_READING);
	key = 1;
	ti = tioreqht_htable_lookup(&req->ir_nwxfer.nxr_tioreqs_hash, &key);

	/*
	 * Fake data structure members so that UT passes through
	 * PRE checks unhurt.
	 */
	ti->ti_dgvec = &dgvec_tmp;
	M0_ALLOC_PTR(session);
	M0_UT_ASSERT(session != NULL);
	M0_ALLOC_PTR(conn);
	M0_UT_ASSERT(conn != NULL);
	session->s_conn = conn;
	conn->c_rpc_machine = &csb.csb_rpc_machine;
	conn->c_rpc_machine->rm_tm.ntm_dom = &csb.csb_ndom;
	ti->ti_session = session;

	/* Creates IO fops from pages. */
	rc = target_ioreq_iofops_prepare(ti, PA_DATA);
	M0_UT_ASSERT(rc == 0);

	/* Retrieves an IO fop that will be treated as a failed fop. */
	irfop = iofops_tlist_head(&ti->ti_iofops);
	m0_fop_rpc_machine_set(&irfop->irf_iofop.if_fop,
			       &csb.csb_rpc_machine);
	reply = m0_fop_alloc(&m0_fop_cob_readv_rep_fopt, NULL,
			     &csb.csb_rpc_machine);
	reply->f_item.ri_rmachine = conn->c_rpc_machine;
	irfop->irf_iofop.if_fop.f_item.ri_reply = &reply->f_item;

	/* Increments refcount so that ref release can be verified. */
	reply = m0_fop_get(reply);

	rbulk = &irfop->irf_iofop.if_rbulk;
	M0_UT_ASSERT(m0_tlist_length(&rpcbulk_tl, &rbulk->rb_buflist) == 1);
	rbuf  = m0_tlist_head(&rpcbulk_tl, &rbulk->rb_buflist);

	/*
	 * Starts degraded mode processing for parity groups whose pages
	 * are found in the failed IO fop.
	 */
	ioreq_sm_state_set(req, IRS_READ_COMPLETE);
	ioreq_sm_state_set(req, IRS_DEGRADED_READING);
	rw_rep = io_rw_rep_get(reply);
	rw_rep->rwr_rc = M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH;
	io_req_fop_dgmode_read(irfop);

	play = pdlayout_get(req);
	le   = m0_layout_instance_to_enum(file_to_m0inode(req->ir_file)->
	       ci_layout_instance);

	for (cnt = 0; cnt < req->ir_iomap_nr; ++cnt) {
		if (req->ir_iomaps[cnt]->pi_state != PI_DEGRADED)
			continue;

		rc = req->ir_iomaps[cnt]->pi_ops->pi_dgmode_postprocess(req->
				ir_iomaps[cnt]);
		M0_UT_ASSERT(rc == 0);
	}

	for (cnt = 0; cnt < rbuf->bb_zerovec.z_bvec.ov_vec.v_nr; ++cnt) {

		ioreq_pgiomap_find(req, pargrp_id_find(rbuf->bb_zerovec.
				   z_index[cnt], UNIT_SIZE), &pgcur, &map);
		M0_UT_ASSERT(map != NULL);
		M0_UT_ASSERT(map->pi_state == PI_DEGRADED);

		tgt.ta_frame = rbuf->bb_zerovec.z_index[cnt] /
			       layout_unit_size(play);
		tgt.ta_obj   = m0_layout_enum_find(le,
				file_to_fid(req->ir_file), &ti->ti_fid);

		m0_pdclust_instance_inv(pdlayout_instance(layout_instance(req)),
				        &tgt, &src);
		M0_UT_ASSERT(src.sa_unit < layout_n(play));

		/*
		 * Checks if all pages from given unit_id are marked
		 * as failed.
		 */
		for (row = 0; row < data_row_nr(play); ++row)
			M0_UT_ASSERT(map->pi_databufs[row][src.sa_unit]->
				     db_flags & PA_READ_FAILED);

	}

	for (cnt = 0; cnt < req->ir_iomap_nr; ++cnt) {
		map = req->ir_iomaps[cnt];
		if (map->pi_state != PI_DEGRADED)
			continue;

		/* Traversing unit by unit. */
		for (col = 0; col < data_col_nr(play); ++col) {
			for (row = 0; row < data_row_nr(play); ++row) {
				if (col == src.sa_unit)
					M0_UT_ASSERT(map->pi_databufs
						     [row][col]->db_flags &
						     PA_READ_FAILED);
				else {
					M0_UT_ASSERT(map->pi_databufs
						     [row][col]->db_flags &
						     PA_DGMODE_READ);
					memset(map->pi_databufs[row][col]->
					       db_buf.b_addr, content[col],
					       PAGE_CACHE_SIZE);
					cont = (char *)map->pi_databufs
						[row][col]->db_buf.b_addr;
				}
			}
		}

		/* Parity units are needed for recovery. */
		for (col = 0; col < parity_col_nr(play); ++col) {
			for (row = 0; row < parity_row_nr(play); ++row) {
				M0_UT_ASSERT(map->pi_paritybufs[row][col]->
					     db_flags & PA_DGMODE_READ);
				memset(map->pi_paritybufs[row][col]->db_buf.
				       b_addr, content[LAY_P - 2],
				       PAGE_CACHE_SIZE);
				cont = (char *)map->pi_paritybufs[row][col]->
					db_buf.b_addr;
			}
		}

		/* Recovers lost data unit/s. */
		rc = pargrp_iomap_dgmode_recover(map);
		M0_UT_ASSERT(rc == 0);

		/* Validate the recovered data. */
		for (row = 0; row < data_row_nr(play); ++row) {
			cont = (char *)map->pi_databufs[row][src.sa_unit]->
				db_buf.b_addr;
			for (col = 0; col < PAGE_CACHE_SIZE; ++col, ++cont)
				M0_UT_ASSERT(*cont == content[0]);
		}
	}

	ti->ti_dgvec = NULL;
	rc = dgmode_rwvec_alloc_init(ti);
	M0_UT_ASSERT(ti->ti_dgvec->dr_tioreq == ti);
	M0_UT_ASSERT(ti->ti_dgvec->dr_ivec.iv_index != NULL);
	M0_UT_ASSERT(ti->ti_dgvec->dr_ivec.iv_vec.v_count != NULL);
	M0_UT_ASSERT(ti->ti_dgvec->dr_bufvec.ov_buf != NULL);
	M0_UT_ASSERT(ti->ti_dgvec->dr_bufvec.ov_vec.v_count != NULL);
	M0_UT_ASSERT(ti->ti_dgvec->dr_pageattrs != NULL);

	ti->ti_dgvec->dr_ivec.iv_vec.v_nr = page_nr(layout_unit_size(play) *
			                    layout_k(play));
	dgmode_rwvec_dealloc_fini(ti->ti_dgvec);

	/* Cleanup */
	m0_rpc_bulk_buflist_empty(rbulk);
	m0_fop_put_lock(reply);
	ioreq_sm_state_set(req, IRS_READ_COMPLETE);
	req->ir_nwxfer.nxr_iofop_nr = 0;
	req->ir_nwxfer.nxr_rdbulk_nr = 0;
	ti->ti_dgvec = NULL;

	req->ir_ops->iro_iomaps_destroy(req);
	nw_xfer_req_complete(&req->ir_nwxfer, false);
	io_request_fini(req);
	m0_indexvec_free(&ivec);
	m0_free(req);
}

const struct m0_test_suite file_io_ut = {
        .ts_name  = "file-io-ut",
        .ts_init  = file_io_ut_init,
        .ts_fini  = file_io_ut_fini,
        .ts_tests = {
                {"basic_data_structures_test", ds_test},
                {"helper_routines_test",       helpers_test},
                {"parity_group_ops_test",      pargrp_iomap_test},
		{"nw_xfer_ops_test",           nw_xfer_ops_test},
                {"target_ioreq_ops_test",      target_ioreq_test},
                {"dgmode_readio_test",         dgmode_readio_test},
		{NULL,                         NULL},
        },
};
M0_EXPORTED(file_io_ut);
