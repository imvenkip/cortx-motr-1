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
 * Original creation date: 07/28/2012
 */

#include <asm/uaccess.h>    /* VERIFY_READ, VERIFY_WRITE */
#include <linux/mm.h>       /* get_user_pages(), get_page(), put_page() */
#include <linux/fs.h>       /* struct file_operations */

#include "lib/memory.h"     /* c2_alloc(), c2_free() */
#include "lib/misc.h"       /* c2_round_{up/down} */
#include "lib/bob.h"        /* c2_bob_type */
#include "lib/ext.h"        /* c2_ext */
#include "lib/arith.h"      /* min_type() */
#include "layout/pdclust.h" /* C2_PUT_*, c2_layout_to_pdl(),
			     * c2_pdclust_instance_map */
#include "lib/bob.h"        /* c2_bob_type */
#include "ioservice/io_fops.h"    /* c2_io_fop */
#include "ioservice/io_fops_ff.h" /* c2_fop_cob_rw */
#include "colibri/magic.h"  /* C2_T1FS_IOREQ_MAGIC */
#include "c2t1fs/linux_kernel/c2t1fs.h" /* c2t1fs_sb */

#include "c2t1fs/linux_kernel/file_internal.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_C2T1FS
#include "lib/trace.h"      /* C2_LOG, C2_ENTRY */


struct io_mem_stats iommstats;

const struct inode_operations c2t1fs_reg_inode_operations;
void iov_iter_advance(struct iov_iter *i, size_t bytes);

/* Imports */
struct c2_net_domain;
extern bool c2t1fs_inode_bob_check(struct c2t1fs_inode *bob);

C2_TL_DESCR_DEFINE(tioreqs, "List of target_ioreq objects", static,
		   struct target_ioreq, ti_link, ti_magic,
		   C2_T1FS_TIOREQ_MAGIC, C2_T1FS_NWREQ_MAGIC);

C2_TL_DESCR_DEFINE(iofops, "List of IO fops", static,
		   struct io_req_fop, irf_link, irf_magic,
		   C2_T1FS_IOFOP_MAGIC, C2_T1FS_TIOREQ_MAGIC);

C2_TL_DEFINE(tioreqs, static, struct target_ioreq);
C2_TL_DEFINE(iofops,  static, struct io_req_fop);

static struct c2_bob_type tioreq_bobtype;
static struct c2_bob_type iofop_bobtype;
static const struct c2_bob_type ioreq_bobtype;
static const struct c2_bob_type pgiomap_bobtype;
static const struct c2_bob_type nwxfer_bobtype;
static const struct c2_bob_type dtbuf_bobtype;

C2_BOB_DEFINE(static, &tioreq_bobtype,	target_ioreq);
C2_BOB_DEFINE(static, &iofop_bobtype,	io_req_fop);
C2_BOB_DEFINE(static, &pgiomap_bobtype, pargrp_iomap);
C2_BOB_DEFINE(static, &ioreq_bobtype,	io_request);
C2_BOB_DEFINE(static, &nwxfer_bobtype,	nw_xfer_request);
C2_BOB_DEFINE(static, &dtbuf_bobtype,	data_buf);

static const struct c2_bob_type ioreq_bobtype = {
	.bt_name	 = "io_request_bobtype",
	.bt_magix_offset = offsetof(struct io_request, ir_magic),
	.bt_magix	 = C2_T1FS_IOREQ_MAGIC,
	.bt_check	 = NULL,
};

static const struct c2_bob_type pgiomap_bobtype = {
	.bt_name	 = "pargrp_iomap_bobtype",
	.bt_magix_offset = offsetof(struct pargrp_iomap, pi_magic),
	.bt_magix	 = C2_T1FS_PGROUP_MAGIC,
	.bt_check	 = NULL,
};

static const struct c2_bob_type nwxfer_bobtype = {
	.bt_name	 = "nw_xfer_request_bobtype",
	.bt_magix_offset = offsetof(struct nw_xfer_request, nxr_magic),
	.bt_magix	 = C2_T1FS_NWREQ_MAGIC,
	.bt_check	 = NULL,
};

static const struct c2_bob_type dtbuf_bobtype = {
	.bt_name	 = "data_buf_bobtype",
	.bt_magix_offset = offsetof(struct data_buf, db_magic),
	.bt_magix	 = C2_T1FS_DTBUF_MAGIC,
	.bt_check	 = NULL,
};

/*
 * These are used as macros since they are used as lvalues which is
 * not possible by using static inline functions.
 */
#define INDEX(ivec, i) ((ivec)->iv_index[(i)])

#define COUNT(ivec, i) ((ivec)->iv_vec.v_count[(i)])

#define SEG_NR(vec)    ((vec)->iv_vec.v_nr)

static inline c2_bcount_t seg_endpos(const struct c2_indexvec *ivec, uint32_t i)
{
	C2_PRE(ivec != NULL);

	return ivec->iv_index[i] + ivec->iv_vec.v_count[i];
}

static inline struct inode *file_to_inode(struct file *file)
{
	return file->f_dentry->d_inode;
}

static inline struct c2t1fs_inode *file_to_c2inode(struct file *file)
{
	return C2T1FS_I(file_to_inode(file));
}

static inline struct c2_fid *file_to_fid(struct file *file)
{
	return &file_to_c2inode(file)->ci_fid;
}

static inline struct c2t1fs_sb *file_to_sb(struct file *file)
{
	return C2T1FS_SB(file_to_inode(file)->i_sb);
}

static inline struct c2_sm_group *file_to_smgroup(struct file *file)
{
	return &file_to_sb(file)->csb_iogroup;
}

static inline uint64_t page_nr(c2_bcount_t size)
{
	return size >> PAGE_CACHE_SHIFT;
}

static inline struct c2_layout_instance *layout_instance(struct io_request *req)
{
	return file_to_c2inode(req->ir_file)->ci_layout_instance;
}

static inline struct c2_pdclust_instance *pdlayout_instance(struct
		c2_layout_instance *li)
{
	return c2_layout_instance_to_pdi(li);
}

static inline struct c2_pdclust_layout *pdlayout_get(struct io_request *req)
{
	return pdlayout_instance(layout_instance(req))->pi_layout;
}

static inline uint32_t layout_n(struct c2_pdclust_layout *play)
{
	return play->pl_attr.pa_N;
}

static inline uint32_t layout_k(struct c2_pdclust_layout *play)
{
	return play->pl_attr.pa_K;
}

static inline uint64_t layout_unit_size(struct c2_pdclust_layout *play)
{
	return play->pl_attr.pa_unit_size;
}

static inline uint64_t parity_units_page_nr(struct c2_pdclust_layout *play)
{
	return page_nr(layout_unit_size(play)) * layout_k(play);
}

static inline uint64_t indexvec_page_nr(struct c2_vec *vec)
{
	return page_nr(c2_vec_count(vec));
}

static inline uint64_t data_size(struct c2_pdclust_layout *play)
{
	return layout_n(play) * layout_unit_size(play);
}

static inline struct c2_parity_math *parity_math(struct io_request *req)
{
	return &pdlayout_instance(layout_instance(req))->pi_math;
}

static inline uint64_t group_id(c2_bindex_t index, c2_bcount_t dtsize)
{
	return index / dtsize;
}

static inline uint64_t target_offset(uint64_t		       frame,
				     struct c2_pdclust_layout *play,
				     c2_bindex_t	       gob_offset)
{
	return frame * layout_unit_size(play) +
	       (gob_offset % layout_unit_size(play));
}

static inline struct c2_fid target_fid(struct io_request	  *req,
				       struct c2_pdclust_tgt_addr *tgt)
{
	return c2t1fs_cob_fid(file_to_c2inode(req->ir_file), tgt->ta_obj);
}

static inline struct c2_rpc_session *target_session(struct io_request *req,
						    struct c2_fid      tfid)
{
	return c2t1fs_container_id_to_session(file_to_sb(req->ir_file),
					      tfid.f_container);
}

static inline uint64_t page_id(c2_bindex_t offset)
{
	return offset >> PAGE_CACHE_SHIFT;
}

static inline uint32_t data_row_nr(struct c2_pdclust_layout *play)
{
	return page_nr(layout_unit_size(play));
}

static inline uint32_t data_col_nr(struct c2_pdclust_layout *play)
{
	return layout_n(play);
}

static inline uint32_t parity_col_nr(struct c2_pdclust_layout *play)
{
	return layout_k(play);
}

static inline uint32_t parity_row_nr(struct c2_pdclust_layout *play)
{
	return data_row_nr(play);
}

static inline uint64_t round_down(uint64_t val, uint64_t size)
{
	C2_PRE(c2_is_po2(size));

	/*
	 * Returns current value if it is already a multiple of size,
	 * else c2_round_down() is invoked.
	 */
	return (val & (size - 1)) == 0 ?
	       val : c2_round_down(val, size);
}

static inline uint64_t round_up(uint64_t val, uint64_t size)
{
	C2_PRE(c2_is_po2(size));

	/*
	 * Returns current value if it is already a multiple of size,
	 * else c2_round_up() is invoked.
	 */
	return (val & (size - 1)) == 0 ?
	       val : c2_round_up(val, size);
}

/* Returns the position of page in matrix of data buffers. */
static void page_pos_get(struct pargrp_iomap *map, c2_bindex_t index,
			 uint32_t *row, uint32_t *col)
{
	uint64_t		  pg_id;
	struct c2_pdclust_layout *play;

	C2_PRE(map != NULL);
	C2_PRE(row != NULL);
	C2_PRE(col != NULL);

	play = pdlayout_get(map->pi_ioreq);

	pg_id = page_id(index - data_size(play) * map->pi_grpid);
	*row  = pg_id % data_row_nr(play);
	*col  = pg_id / data_row_nr(play);
}

/* Invoked during c2t1fs mount. */
void io_bob_tlists_init(void)
{
	c2_bob_type_tlist_init(&tioreq_bobtype, &tioreqs_tl);
	C2_ASSERT(tioreq_bobtype.bt_magix == C2_T1FS_TIOREQ_MAGIC);
	c2_bob_type_tlist_init(&iofop_bobtype, &iofops_tl);
	C2_ASSERT(iofop_bobtype.bt_magix == C2_T1FS_IOFOP_MAGIC);
}

static const struct c2_addb_loc io_addb_loc = {
	.al_name = "c2t1fs_io_path",
};

struct c2_addb_ctx c2t1fs_addb;

const struct c2_addb_ctx_type c2t1fs_addb_type = {
	.act_name = "c2t1fs",
};

C2_ADDB_EV_DEFINE(io_request_failed, "IO request failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

static void io_rpc_item_cb (struct c2_rpc_item *item);
static void io_req_fop_free(struct c2_rpc_item *item);

/*
 * io_rpc_item_cb can not be directly invoked from io fops code since it
 * leads to build dependency of ioservice code over kernel-only code (c2t1fs).
 * Hence, a new c2_rpc_item_ops structure is used for fops dispatched
 * by c2t1fs io requests.
 */
static const struct c2_rpc_item_ops c2t1fs_item_ops = {
	.rio_sent    = NULL,
	.rio_replied = io_rpc_item_cb,
	.rio_free    = io_req_fop_free,
};

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer);

static int  nw_xfer_io_prepare	(struct nw_xfer_request *xfer);
static void nw_xfer_req_complete(struct nw_xfer_request *xfer,
				 bool			 rmw);
static int  nw_xfer_req_dispatch(struct nw_xfer_request *xfer);

static int  nw_xfer_tioreq_map	(struct nw_xfer_request	     *xfer,
				 struct c2_pdclust_src_addr  *src,
				 struct c2_pdclust_tgt_addr  *tgt,
				 struct target_ioreq	    **out);

static int  nw_xfer_tioreq_get	(struct nw_xfer_request *xfer,
				 struct c2_fid		*fid,
				 struct c2_rpc_session	*session,
				 uint64_t		 size,
				 struct target_ioreq   **out);

static const struct nw_xfer_ops xfer_ops = {
	.nxo_prepare	 = nw_xfer_io_prepare,
	.nxo_complete	 = nw_xfer_req_complete,
	.nxo_dispatch	 = nw_xfer_req_dispatch,
	.nxo_tioreq_map	 = nw_xfer_tioreq_map,
};

static int  pargrp_iomap_populate     (struct pargrp_iomap	*map,
				       const struct c2_indexvec *ivec,
				       struct c2_ivec_cursor	*cursor);

static bool pargrp_iomap_spans_seg    (struct pargrp_iomap *map,
				       c2_bindex_t	    index,
				       c2_bcount_t	    count);

static int  pargrp_iomap_readrest     (struct pargrp_iomap *map);


static int  pargrp_iomap_seg_process  (struct pargrp_iomap *map,
				       uint64_t		    seg,
				       bool		    rmw);

static int  pargrp_iomap_parity_recalc(struct pargrp_iomap *map);

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map);

static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map);

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map);

static const struct pargrp_iomap_ops iomap_ops = {
	.pi_populate		 = pargrp_iomap_populate,
	.pi_spans_seg		 = pargrp_iomap_spans_seg,
	.pi_readrest		 = pargrp_iomap_readrest,
	.pi_fullpages_find	 = pargrp_iomap_fullpages_count,
	.pi_seg_process		 = pargrp_iomap_seg_process,
	.pi_readold_auxbuf_alloc = pargrp_iomap_readold_auxbuf_alloc,
	.pi_parity_recalc	 = pargrp_iomap_parity_recalc,
	.pi_paritybufs_alloc	 = pargrp_iomap_paritybufs_alloc,
};

static bool pargrp_iomap_invariant_nr (const struct io_request *req);
static bool target_ioreq_invariant    (const struct target_ioreq *ti);

static void target_ioreq_fini	      (struct target_ioreq *ti);

static int target_ioreq_iofops_prepare(struct target_ioreq *ti,
				       enum page_attr       filter);

static void target_ioreq_seg_add      (struct target_ioreq *ti,
				       uint64_t		    frame,
				       c2_bindex_t	    gob_offset,
				       c2_bindex_t	    par_offset,
				       c2_bcount_t	    count,
				       uint64_t		    unit);

static const struct target_ioreq_ops tioreq_ops = {
	.tio_seg_add	    = target_ioreq_seg_add,
	.tio_iofops_prepare = target_ioreq_iofops_prepare,
};

static struct data_buf *data_buf_alloc_init(enum page_attr pattr);

static void data_buf_dealloc_fini(struct data_buf *buf);

static void io_bottom_half(struct c2_sm_group *grp, struct c2_sm_ast *ast);

static int  ioreq_iomaps_prepare(struct io_request *req);

static void ioreq_iomaps_destroy(struct io_request *req);

static void ioreq_iomap_locate	(struct io_request    *req,
				 uint64_t	       grpid,
				 struct pargrp_iomap **map);

static int ioreq_user_data_copy (struct io_request   *req,
				 enum copy_direction  dir,
				 enum page_attr	      filter);

static int ioreq_parity_recalc	(struct io_request *req);

static int ioreq_iosm_handle	(struct io_request *req);

static const struct io_request_ops ioreq_ops = {
	.iro_iomaps_prepare = ioreq_iomaps_prepare,
	.iro_iomaps_destroy = ioreq_iomaps_destroy,
	.iro_user_data_copy = ioreq_user_data_copy,
	.iro_iomap_locate   = ioreq_iomap_locate,
	.iro_parity_recalc  = ioreq_parity_recalc,
	.iro_iosm_handle    = ioreq_iosm_handle,
};

static inline uint32_t ioreq_sm_state(const struct io_request *req)
{
	return req->ir_sm.sm_state;
}

static void ioreq_sm_failed(struct io_request *req, int rc)
{
	c2_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	c2_sm_fail(&req->ir_sm, IRS_FAILED, rc);
	c2_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
}

static const struct c2_sm_state_descr io_states[] = {
	[IRS_INITIALIZED]    = {
		.sd_flags    = C2_SDF_INITIAL,
		.sd_name     = "IO_initial",
		.sd_allowed  = (1 << IRS_READING) | (1 << IRS_WRITING) |
			       (1 << IRS_FAILED)  | (1 << IRS_REQ_COMPLETE)
	},
	[IRS_READING]	     = {
		.sd_name     = "IO_reading",
		.sd_allowed  = (1 << IRS_READ_COMPLETE) |
			       (1 << IRS_FAILED)
	},
	[IRS_READ_COMPLETE]  = {
		.sd_name     = "IO_read_complete",
		.sd_allowed  = (1 << IRS_WRITING) | (1 << IRS_REQ_COMPLETE) |
			       (1 << IRS_FAILED)
	},
	[IRS_WRITING]        = {
		.sd_name     = "IO_writing",
		.sd_allowed  = (1 << IRS_WRITE_COMPLETE) | (1 << IRS_FAILED)
	},
	[IRS_WRITE_COMPLETE] = {
		.sd_name     = "IO_write_complete",
		.sd_allowed  = (1 << IRS_REQ_COMPLETE) | (1 << IRS_FAILED)
	},
	[IRS_FAILED]         = {
		.sd_flags    = C2_SDF_FAILURE,
		.sd_name     = "IO_req_failed",
		.sd_allowed  = (1 << IRS_REQ_COMPLETE)
	},
	[IRS_REQ_COMPLETE]   = {
		.sd_flags    = C2_SDF_TERMINAL,
		.sd_name     = "IO_req_complete",
	},
};

static const struct c2_sm_conf io_sm_conf = {
	.scf_name      = "IO request state machine configuration",
	.scf_nr_states = ARRAY_SIZE(io_states),
	.scf_state     = io_states,
};

static void ioreq_sm_state_set(struct io_request *req, int state)
{
	C2_LOG(C2_INFO, "IO request %p current state = %s",
	       req, io_states[ioreq_sm_state(req)].sd_name);
	c2_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	c2_sm_state_set(&req->ir_sm, state);
	c2_mutex_unlock(&req->ir_sm.sm_grp->s_lock);
	C2_LOG(C2_INFO, "IO request state changed to %s",
	       io_states[ioreq_sm_state(req)].sd_name);
}

static bool io_request_invariant(const struct io_request *req)
{
	return
	       io_request_bob_check(req) &&
	       req->ir_type   <= IRT_TYPE_NR &&
	       req->ir_iovec  != NULL &&
	       req->ir_ops    != NULL &&
	       c2_fid_is_valid(file_to_fid(req->ir_file)) &&

	       ergo(ioreq_sm_state(req) == IRS_READING,
		    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

	       ergo(ioreq_sm_state(req) == IRS_WRITING,
		    !tioreqs_tlist_is_empty(&req->ir_nwxfer.nxr_tioreqs)) &&

	       ergo(ioreq_sm_state(req) == IRS_WRITE_COMPLETE,
		    req->ir_nwxfer.nxr_iofop_nr == 0) &&

	       c2_vec_count(&req->ir_ivec.iv_vec) > 0 &&

	       c2_forall(i, req->ir_ivec.iv_vec.v_nr - 1,
			 req->ir_ivec.iv_index[i] +
			 req->ir_ivec.iv_vec.v_count[i] <=
			 req->ir_ivec.iv_index[i+1]) &&

	       pargrp_iomap_invariant_nr(req) &&

	       nw_xfer_request_invariant(&req->ir_nwxfer);
}

static bool nw_xfer_request_invariant(const struct nw_xfer_request *xfer)
{
	return
	       nw_xfer_request_bob_check(xfer) &&
	       xfer->nxr_state <= NXS_STATE_NR &&

	       ergo(xfer->nxr_state == NXS_INITIALIZED,
		    (xfer->nxr_rc == xfer->nxr_bytes) ==
		    (xfer->nxr_iofop_nr == 0)) &&

	       ergo(xfer->nxr_state == NXS_INFLIGHT,
		    !tioreqs_tlist_is_empty(&xfer->nxr_tioreqs)) &&

	       ergo(xfer->nxr_state == NXS_COMPLETE,
		    xfer->nxr_iofop_nr == 0) &&

	       c2_tl_forall(tioreqs, tioreq, &xfer->nxr_tioreqs,
			    target_ioreq_invariant(tioreq));
}

static bool data_buf_invariant(const struct data_buf *db)
{
	return
	       db != NULL &&
	       data_buf_bob_check(db) &&
	       ergo(db->db_buf.b_addr != NULL, db->db_buf.b_nob > 0);
}

static bool data_buf_invariant_nr(const struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct c2_pdclust_layout *play;

	play = pdlayout_get(map->pi_ioreq);
	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL &&
			    !data_buf_invariant(map->pi_databufs[row][col]))
				return false;
		}
	}

	if (map->pi_paritybufs != NULL) {
		for (row = 0; row < parity_row_nr(play); ++row) {
			for (col = 0; col < parity_col_nr(play); ++col) {
				if (map->pi_paritybufs[row][col] != NULL &&
				    !data_buf_invariant(map->pi_paritybufs
				    [row][col]))
					return false;
			}
		}
	}
	return true;
}

static bool io_req_fop_invariant(const struct io_req_fop *fop)
{
	return
		io_req_fop_bob_check(fop) &&
		fop->irf_tioreq != NULL &&
		fop->irf_ast.sa_cb != NULL &&
		fop->irf_ast.sa_mach != NULL;
}

static bool target_ioreq_invariant(const struct target_ioreq *ti)
{
	return
		target_ioreq_bob_check(ti) &&
		ti->ti_session	    != NULL &&
		ti->ti_nwxfer	    != NULL &&
		ti->ti_bufvec.ov_buf != NULL &&
		c2_fid_is_valid(&ti->ti_fid) &&
		c2_tl_forall(iofops, iofop, &ti->ti_iofops,
			     io_req_fop_invariant(iofop));
}

static bool pargrp_iomap_invariant(const struct pargrp_iomap *map)
{
	return
	       pargrp_iomap_bob_check(map) &&
	       map->pi_ops	!= NULL &&
	       map->pi_rtype	 < PIR_NR &&
	       map->pi_databufs != NULL &&
	       map->pi_ioreq	!= NULL &&
	       ergo(c2_vec_count(&map->pi_ivec.iv_vec) > 0 &&
		    map->pi_ivec.iv_vec.v_nr >= 2,
		    c2_forall(i, map->pi_ivec.iv_vec.v_nr - 1,
			      map->pi_ivec.iv_index[i] +
			      map->pi_ivec.iv_vec.v_count[i] <=
			      map->pi_ivec.iv_index[i+1])) &&
	       data_buf_invariant_nr(map);
}

static bool pargrp_iomap_invariant_nr(const struct io_request *req)
{
	return c2_forall(i, req->ir_iomap_nr,
			 pargrp_iomap_invariant(req->ir_iomaps[i]));
}

static void nw_xfer_request_init(struct nw_xfer_request *xfer)
{
	C2_ENTRY("nw_xfer_request : %p", xfer);
	C2_PRE(xfer != NULL);

	nw_xfer_request_bob_init(xfer);
	xfer->nxr_rc	= 0;
	xfer->nxr_bytes = xfer->nxr_iofop_nr = 0;
	xfer->nxr_state = NXS_INITIALIZED;
	xfer->nxr_ops	= &xfer_ops;
	tioreqs_tlist_init(&xfer->nxr_tioreqs);

	C2_POST(nw_xfer_request_invariant(xfer));
	C2_LEAVE();
}

static void nw_xfer_request_fini(struct nw_xfer_request *xfer)
{
	C2_ENTRY("nw_xfer_request : %p", xfer);
	C2_PRE(xfer != NULL && xfer->nxr_state == NXS_COMPLETE);
	C2_PRE(nw_xfer_request_invariant(xfer));

	xfer->nxr_ops = NULL;
	nw_xfer_request_bob_fini(xfer);
	tioreqs_tlist_fini(&xfer->nxr_tioreqs);
	C2_LEAVE();
}

static void ioreq_iomap_locate(struct io_request *req, uint64_t grpid,
			       struct pargrp_iomap **iomap)
{
	uint64_t map;

	C2_ENTRY("Locate map with grpid = %llu", grpid);
	C2_PRE(io_request_invariant(req));
	C2_PRE(iomap != NULL);

	for (map = 0; map < req->ir_iomap_nr; ++map) {
		if (req->ir_iomaps[map]->pi_grpid == grpid) {
			*iomap = req->ir_iomaps[map];
			break;
		}
	}
	C2_POST(map < req->ir_iomap_nr);
	C2_LEAVE("Map = %p", *iomap);
}

/* Typically used while copying data to/from user space. */
static bool page_copy_filter(c2_bindex_t start, c2_bindex_t end,
			     enum page_attr filter)
{
	C2_PRE(end - start <= PAGE_CACHE_SIZE);
	C2_PRE(ergo(filter != PA_NONE,
		    filter & (PA_FULLPAGE_MODIFY | PA_PARTPAGE_MODIFY)));

	if (filter & PA_FULLPAGE_MODIFY) {
		return (end - start == PAGE_CACHE_SIZE);
	} else if (filter & PA_PARTPAGE_MODIFY) {
		return (end - start <  PAGE_CACHE_SIZE);
	} else
		return true;
}

static int user_data_copy(struct pargrp_iomap *map,
			  c2_bindex_t	       start,
			  c2_bindex_t	       end,
			  struct iov_iter     *it,
			  enum copy_direction  dir,
			  enum page_attr       filter)
{
	/*
	 * iov_iter should be able to be used with copy_to_user() as well
	 * since it is as good as a vector cursor.
	 * Present kernel 2.6.32 has no support for such requirement.
	 */
	uint64_t		  bytes;
	uint32_t		  row;
	uint32_t		  col;
	struct page		 *page;
	struct c2_pdclust_layout *play;

	C2_ENTRY("Copy %s user-space, start = %llu, end = %llu",
		 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
		 start, end);
	C2_PRE(pargrp_iomap_invariant(map));
	C2_PRE(it != NULL);
	C2_PRE(C2_IN(dir, (CD_COPY_FROM_USER, CD_COPY_TO_USER)));

	/* Finds out the page from pargrp_iomap::pi_databufs. */
	play = pdlayout_get(map->pi_ioreq);
	page_pos_get(map, start, &row, &col);
	C2_ASSERT(map->pi_databufs[row][col] != NULL);
	page = virt_to_page(map->pi_databufs[row][col]->db_buf.b_addr);

	if (dir == CD_COPY_FROM_USER) {
		if (page_copy_filter(start, end, filter)) {
			C2_ASSERT(ergo(filter != PA_NONE,
				       map->pi_databufs[row][col]->db_flags &
				       filter));

			/*
			 * Copies page to auxiliary buffer before it gets
			 * overwritten by user data. This is needed in order
			 * to calculate delta parity in case of read-old
			 * approach.
			 */
			if (map->pi_databufs[row][col]->db_auxbuf.b_addr !=
			    NULL && map->pi_rtype == PIR_READOLD)
				memcpy(map->pi_databufs[row][col]->db_auxbuf.
				       b_addr,
				       map->pi_databufs[row][col]->db_buf.
				       b_addr, PAGE_CACHE_SIZE);

			pagefault_disable();
			/* Copies to appropriate offset within page. */
			bytes = iov_iter_copy_from_user_atomic(page, it,
					start & (PAGE_CACHE_SIZE - 1),
					end - start);
			pagefault_enable();

			C2_LOG(C2_INFO, "%llu bytes copied from user-space\
			       from offset %llu", bytes, start);

			map->pi_ioreq->ir_copied_nr += bytes;

			if (bytes != end - start)
				C2_RETERR(-EFAULT, "Failed to copy_from_user");
		}
	} else {
		bytes = copy_to_user(it->iov->iov_base + it->iov_offset,
				     map->pi_databufs[row][col]->
				     db_buf.b_addr +
				     (start & (PAGE_CACHE_SIZE - 1)),
				     end - start);

		map->pi_ioreq->ir_copied_nr += end - start - bytes;

		C2_LOG(C2_INFO, "%llu bytes copied to user-space from offset\
		       %llu", end - start - bytes, start);

		if (bytes != 0)
			C2_RETERR(-EFAULT, "Failed to copy_to_user");
	}

	C2_RETURN(0);
}

static int pargrp_iomap_parity_recalc(struct pargrp_iomap *map)
{
	int			  rc;
	uint32_t		  row;
	uint32_t		  col;
	struct c2_buf		 *dbufs;
	struct c2_buf		 *pbufs;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map = %p", map);
	C2_PRE(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);
	C2_ALLOC_ARR_ADDB(dbufs, layout_n(play), &c2t1fs_addb,
			  &io_addb_loc);
	C2_ALLOC_ARR_ADDB(pbufs, layout_k(play), &c2t1fs_addb,
			  &io_addb_loc);

	if (dbufs == NULL || pbufs == NULL) {
		rc = -ENOMEM;
		goto last;
	}

	if ((map->pi_ioreq->ir_type == IRT_WRITE && map->pi_rtype == PIR_NONE)
	    || map->pi_rtype == PIR_READREST) {

		unsigned long zpage;

		zpage = get_zeroed_page(GFP_KERNEL);
		if (zpage == 0) {
			rc = -ENOMEM;
			goto last;
		}

		for (row = 0; row < data_row_nr(play); ++row) {
			for (col = 0; col < data_col_nr(play); ++col)
				if (map->pi_databufs[row][col] != NULL) {
					dbufs[col] = map->pi_databufs
						     [row][col]->db_buf;
				} else {
					dbufs[col].b_addr = (void *)zpage;
					dbufs[col].b_nob  = PAGE_CACHE_SIZE;
				}

			for (col = 0; col < layout_k(play); ++col)
				pbufs[col] = map->pi_paritybufs[row][col]->
					     db_buf;

			c2_parity_math_calculate(parity_math(map->pi_ioreq),
						 dbufs, pbufs);
		}
		rc = 0;
		free_page(zpage);
		C2_LOG(C2_INFO, "Parity recalculated for %s",
		       map->pi_rtype == PIR_READREST ? "read-rest" :
		       "aligned write");

	} else {
		struct c2_buf *old;

		C2_ALLOC_ARR_ADDB(old, layout_n(play), &c2t1fs_addb,
				  &io_addb_loc);

		if (old == NULL) {
			rc = -ENOMEM;
			goto last;
		}

		for (row = 0; row < data_row_nr(play); ++row) {
			for (col = 0; col < data_col_nr(play); ++col) {
				if (map->pi_databufs[row][col] == NULL)
					continue;

				dbufs[col] = map->pi_databufs[row][col]->db_buf;
				old[col]   = map->pi_databufs[row][col]->
					     db_auxbuf;
				pbufs[0]   = map->pi_paritybufs[row][0]->db_buf;

				c2_parity_math_diff(parity_math(map->pi_ioreq),
						    old, dbufs, pbufs, col);
			}
		}
		c2_free(old);
		rc = 0;
	}
last:
	c2_free(dbufs);
	c2_free(pbufs);
	C2_RETURN(rc);
}

static int ioreq_parity_recalc(struct io_request *req)
{
	int	 rc;
	uint64_t map;

	C2_ENTRY("io_request : %p", req);
	C2_PRE(io_request_invariant(req));

	for (map = 0; map < req->ir_iomap_nr; ++map) {
		rc = req->ir_iomaps[map]->pi_ops->pi_parity_recalc(req->
				ir_iomaps[map]);
		if (rc != 0)
			C2_RETERR(rc, "Parity recalc failed for grpid : %llu",
				  req->ir_iomaps[map]->pi_grpid);
	}
	C2_RETURN(0);
}

static int ioreq_user_data_copy(struct io_request   *req,
				enum copy_direction  dir,
				enum page_attr	     filter)
{
	int			  rc;
	uint64_t		  map;
	c2_bindex_t		  grpstart;
	c2_bindex_t		  grpend;
	c2_bindex_t		  pgstart;
	c2_bindex_t		  pgend;
	c2_bcount_t		  count;
	struct iov_iter		  it;
	struct c2_ivec_cursor	  srccur;
	struct c2_pdclust_layout *play;

	C2_ENTRY("io_request : %p, %s filter = %d", req,
		 dir == CD_COPY_FROM_USER ? (char *)"from" : (char *)"to",
		 filter);
	C2_PRE(io_request_invariant(req));
	C2_PRE(dir < CD_NR);

	iov_iter_init(&it, req->ir_iovec, req->ir_ivec.iv_vec.v_nr,
		      c2_vec_count(&req->ir_ivec.iv_vec), 0);
	c2_ivec_cursor_init(&srccur, &req->ir_ivec);
	play = pdlayout_get(req);

	for (map = 0; map < req->ir_iomap_nr; ++map) {

		C2_ASSERT(pargrp_iomap_invariant(req->ir_iomaps[map]));

		count    = 0;
		grpstart = data_size(play) * req->ir_iomaps[map]->pi_grpid;
		grpend	 = grpstart + data_size(play);

		while (!c2_ivec_cursor_move(&srccur, count) &&
		       c2_ivec_cursor_index(&srccur) < grpend) {

			pgstart	 = c2_ivec_cursor_index(&srccur);
			pgend = min64u(c2_round_up(pgstart + 1,
				       PAGE_CACHE_SIZE),
				       pgstart + c2_ivec_cursor_step(&srccur));
			count = pgend - pgstart;

			/*
			 * This takes care of finding correct page from
			 * current pargrp_iomap structure from pgstart
			 * and pgend.
			 */
			rc = user_data_copy(req->ir_iomaps[map], pgstart, pgend,
					    &it, dir, filter);
			if (rc != 0)
				C2_RETERR(rc, "Copy failed");

			iov_iter_advance(&it, count);
		}
	}

	C2_RETURN(0);
}

static void indexvec_sort(struct c2_indexvec *ivec)
{
	uint32_t i;
	uint32_t j;

	C2_ENTRY("indexvec = %p", ivec);
	C2_PRE(ivec != NULL && c2_vec_count(&ivec->iv_vec) > 0);

	/*
	 * TODO Should be replaced by an efficient sorting algorithm,
	 * something like heapsort which is fairly inexpensive in kernel
	 * mode with the least worst case scenario.
	 * Existing heap sort from kernel code can not be used due to
	 * apparent disconnect between index vector and its associated
	 * count vector for same index.
	 */
	for (i = 0; i < SEG_NR(ivec); ++i) {
		for (j = i+1; j < SEG_NR(ivec); ++j) {
			if (INDEX(ivec, i) > INDEX(ivec, j)) {
				C2_SWAP(INDEX(ivec, i), INDEX(ivec, j));
				C2_SWAP(COUNT(ivec, i), COUNT(ivec, j));
			}
		}
	}
	C2_LEAVE();
}

static int pargrp_iomap_init(struct pargrp_iomap *map,
			     struct io_request	 *req,
			     uint64_t		  grpid)
{
	int			  rc;
	int			  pg;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map = %p, ioreq = %p, grpid = %llu", map, req, grpid);
	C2_PRE(map != NULL);
	C2_PRE(req != NULL);

	pargrp_iomap_bob_init(map);
	play		   = pdlayout_get(req);
	map->pi_ops	   = &iomap_ops;
	map->pi_rtype	   = PIR_NONE;
	map->pi_grpid	   = grpid;
	map->pi_ioreq	   = req;
	map->pi_paritybufs = NULL;

	rc = c2_indexvec_alloc(&map->pi_ivec, page_nr(data_size(play)),
			       &c2t1fs_addb, &io_addb_loc);
	if (rc != 0)
		goto fail;

	/*
	 * This number is incremented only when a valid segment
	 * is added to the index vector.
	 */
	map->pi_ivec.iv_vec.v_nr = 0;

	C2_ALLOC_ARR_ADDB(map->pi_databufs, data_row_nr(play),
			  &c2t1fs_addb, &io_addb_loc);
	if (map->pi_databufs == NULL)
		goto fail;

	for (pg = 0; pg < data_row_nr(play); ++pg) {
		C2_ALLOC_ARR_ADDB(map->pi_databufs[pg], layout_n(play),
				  &c2t1fs_addb, &io_addb_loc);
		if (map->pi_databufs[pg] == NULL)
			goto fail;
	}

	if (req->ir_type == IRT_WRITE) {
		C2_ALLOC_ARR_ADDB(map->pi_paritybufs, parity_row_nr(play),
				  &c2t1fs_addb, &io_addb_loc);
		if (map->pi_paritybufs == NULL)
			goto fail;

		for (pg = 0; pg < parity_row_nr(play); ++pg) {
			C2_ALLOC_ARR_ADDB(map->pi_paritybufs[pg],
					  parity_col_nr(play),
					  &c2t1fs_addb, &io_addb_loc);
			if (map->pi_paritybufs[pg] == NULL)
				goto fail;
		}
	}
	C2_POST(pargrp_iomap_invariant(map));
	C2_RETURN(0);

fail:
	c2_indexvec_free(&map->pi_ivec);

	if (map->pi_databufs != NULL) {
		for (pg = 0; pg < data_row_nr(play); ++pg) {
			c2_free(map->pi_databufs[pg]);
			map->pi_databufs[pg] = NULL;
		}
		c2_free(map->pi_databufs);
	}

	if (map->pi_paritybufs != NULL) {
		for (pg = 0; pg < parity_row_nr(play); ++pg) {
			c2_free(map->pi_paritybufs[pg]);
			map->pi_paritybufs[pg] = NULL;
		}
		c2_free(map->pi_paritybufs);
	}
	C2_RETERR(-ENOMEM, "Memory allocation failed");
}

static void pargrp_iomap_fini(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p", map);
	C2_PRE(pargrp_iomap_invariant(map));

	play	     = pdlayout_get(map->pi_ioreq);
	map->pi_ops  = NULL;
	map->pi_rtype = PIR_NONE;

	pargrp_iomap_bob_fini(map);
	c2_indexvec_free(&map->pi_ivec);

	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL) {
				data_buf_dealloc_fini(map->
						pi_databufs[row][col]);
				map->pi_databufs[row][col] = NULL;
			}
		}
		c2_free(map->pi_databufs[row]);
		map->pi_databufs[row] = NULL;
	}

	if (map->pi_ioreq->ir_type == IRT_WRITE) {
		for (row = 0; row < parity_row_nr(play); ++row) {
			for (col = 0; col < parity_col_nr(play); ++col) {
				if (map->pi_paritybufs[row][col] != NULL) {
					data_buf_dealloc_fini(map->
							pi_paritybufs[row][col]);
					map->pi_paritybufs[row][col] = NULL;
				}
			}
			c2_free(map->pi_paritybufs[row]);
			map->pi_paritybufs[row] = NULL;
		}
	}

	c2_free(map->pi_databufs);
	c2_free(map->pi_paritybufs);
	map->pi_ioreq	   = NULL;
	map->pi_databufs   = NULL;
	map->pi_paritybufs = NULL;
	C2_LEAVE();
}

static bool pargrp_iomap_spans_seg(struct pargrp_iomap *map,
				   c2_bindex_t index, c2_bcount_t count)
{
	uint32_t seg;

	C2_PRE(pargrp_iomap_invariant(map));

	for (seg = 0; seg < map->pi_ivec.iv_vec.v_nr; ++seg) {
		if (index >= INDEX(&map->pi_ivec, seg) &&
		    index + count <= seg_endpos(&map->pi_ivec, seg))
			return true;
	}
	return false;
}

static int pargrp_iomap_databuf_alloc(struct pargrp_iomap *map,
				      uint32_t		   row,
				      uint32_t		   col)
{
	C2_PRE(map != NULL);
	C2_PRE(map->pi_databufs[row][col] == NULL);

	map->pi_databufs[row][col] = data_buf_alloc_init(0);

	return map->pi_databufs[row][col] == NULL ?  -ENOMEM : 0;
}

/* Allocates data_buf structures as needed and populates the buffer flags. */
static int pargrp_iomap_seg_process(struct pargrp_iomap *map,
				    uint64_t		 seg,
				    bool		 rmw)
{
	int			  rc;
	bool			  ret;
	uint32_t		  row;
	uint32_t		  col;
	uint64_t		  count = 0;
	c2_bindex_t		  start;
	c2_bindex_t		  end;
	struct inode		 *inode;
	struct data_buf		 *dbuf;
	struct c2_ivec_cursor	  cur;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p, seg %llu, %s", map, seg, rmw ? "rmw" : "aligned");
	play  = pdlayout_get(map->pi_ioreq);
	inode = map->pi_ioreq->ir_file->f_dentry->d_inode;
	c2_ivec_cursor_init(&cur, &map->pi_ivec);
	ret = c2_ivec_cursor_move_to(&cur, map->pi_ivec.iv_index[seg]);
	C2_ASSERT(!ret);

	while (!c2_ivec_cursor_move(&cur, count)) {

		start = c2_ivec_cursor_index(&cur);
		end   = min64u(c2_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + c2_ivec_cursor_step(&cur));
		count = end - start;

		page_pos_get(map, start, &row, &col);

		rc = pargrp_iomap_databuf_alloc(map, row, col);
		if (rc != 0)
			goto err;

		dbuf = map->pi_databufs[row][col];

		if (map->pi_ioreq->ir_type == IRT_WRITE) {
			dbuf->db_flags |= PA_WRITE;

			dbuf->db_flags |= count == PAGE_CACHE_SIZE ?
				PA_FULLPAGE_MODIFY : PA_PARTPAGE_MODIFY;

			/*
			 * Even if PA_PARTPAGE_MODIFY flag is set in
			 * this buffer, the auxiliary buffer can not be
			 * allocated until ::pi_rtype is selected.
			 */
			if (rmw && dbuf->db_flags & PA_PARTPAGE_MODIFY &&
			    (end < inode->i_size ||
			     (inode->i_size > 0 &&
			      page_id(end - 1) == page_id(inode->i_size - 1))))
				dbuf->db_flags |= PA_READ;
		} else
			/*
			 * For read IO requests, file_aio_read() has already
			 * delimited the index vector to EOF boundary.
			 */
			dbuf->db_flags |= PA_READ;
	}

	C2_RETURN(0);
err:
	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {
			if (map->pi_databufs[row][col] != NULL) {
				data_buf_dealloc_fini(map->pi_databufs
						      [row][col]);
				map->pi_databufs[row][col] = NULL;
			}
		}
	}
	C2_RETERR(rc, "databuf_alloc failed");
}

static uint64_t pargrp_iomap_fullpages_count(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	uint64_t		  nr = 0;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p", map);
	C2_PRE(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);

	for (row = 0; row < data_row_nr(play); ++row) {
		for (col = 0; col < data_col_nr(play); ++col) {

			if (map->pi_databufs[row][col] &&
			    map->pi_databufs[row][col]->db_flags &
			    PA_FULLPAGE_MODIFY)
				++nr;
		}
	}
	C2_LEAVE();
	return nr;
}

static uint64_t pargrp_iomap_auxbuf_alloc(struct pargrp_iomap *map,
					  uint32_t	       row,
					  uint32_t	       col)
{
	C2_PRE(pargrp_iomap_invariant(map));
	C2_PRE(map->pi_rtype == PIR_READOLD);

	map->pi_databufs[row][col]->db_auxbuf.b_addr = (void *)
		get_zeroed_page(GFP_KERNEL);

	if (map->pi_databufs[row][col]->db_auxbuf.b_addr == NULL)
		return -ENOMEM;
	++iommstats.a_page_nr;
	map->pi_databufs[row][col]->db_auxbuf.b_nob = PAGE_CACHE_SIZE;

	return 0;
}

/*
 * Allocates auxiliary buffer for data_buf structures in
 * pargrp_iomap structure.
 */
static int pargrp_iomap_readold_auxbuf_alloc(struct pargrp_iomap *map)
{
	int			  rc = 0;
	uint64_t		  start;
	uint64_t		  end;
	uint64_t		  count = 0;
	uint32_t		  row;
	uint32_t		  col;
	struct c2_ivec_cursor	  cur;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p", map);
	C2_PRE(pargrp_iomap_invariant(map));
	C2_PRE(map->pi_rtype == PIR_READOLD);

	play  = pdlayout_get(map->pi_ioreq);
	c2_ivec_cursor_init(&cur, &map->pi_ivec);

	while (!c2_ivec_cursor_move(&cur, count)) {
		start = c2_ivec_cursor_index(&cur);
		end   = min64u(c2_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + c2_ivec_cursor_step(&cur));
		count = end - start;
		page_pos_get(map, start, &row, &col);

		if (map->pi_databufs[row][col] != NULL) {
			rc = pargrp_iomap_auxbuf_alloc(map, row, col);
			if (rc != 0)
				C2_RETERR(rc, "auxbuf_alloc failed");
		}
	}
	C2_RETURN(rc);
}

/*
 * A read request from rmw IO request can lead to either
 *
 * read_old - Read the old data for the extent spanned by current
 * IO request, along with the old parity unit. This approach needs
 * to calculate new parity in _iterative_ manner. This approach is
 * selected only if current IO extent lies within file size.
 *
 * read_rest - Read rest of the parity group, which is _not_ spanned
 * by current IO request, so that data for whole parity group can
 * be availble for parity calculation.
 * This approach reads the extent from start of parity group to the
 * point where a page is completely spanned by incoming IO request.
 *
 * Typically, the approach which leads to least size of data to be
 * read and written from server is selected.
 *
 *   N = 5, P = 1, K = 1, unit_size = 4k
 *   F	=> Fully occupied
 *   P' => Partially occupied
 *   #	=> Parity unit
 *   *	=> Spare unit
 *   x	=> Start of actual file extent.
 *   y	=> End of actual file extent.
 *   a	=> Rounded down value of x.
 *   b	=> Rounded up value of y.
 *
 *  Read-rest approach
 *
 *   a	   x
 *   +---+---+---+---+---+---+---+
 *   |	 | P'| F | F | F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | P'|	 | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *		   y	 b
 *
 *  Read-old approach
 *
 *   a	   x
 *   +---+---+---+---+---+---+---+
 *   |	 |   |	 | P'| F | # | * |  PG#0
 *   +---+---+---+---+---+---+---+
 *   | F | F | F | F | F | # | * |  PG#1
 *   +---+---+---+---+---+---+---+
 *   | F | P'|	 |   |	 | # | * |  PG#2
 *   +---+---+---+---+---+---+---+
 *     N   N   N   N   N   K   P
 *		   y	 b
 *
 */
static int pargrp_iomap_readrest(struct pargrp_iomap *map)
{
	int			  rc;
	uint32_t		  row;
	uint32_t		  col;
	uint32_t		  seg;
	uint32_t		  seg_nr;
	c2_bindex_t		  grpstart;
	c2_bindex_t		  grpend;
	c2_bindex_t		  start;
	c2_bindex_t		  end;
	c2_bcount_t               count = 0;
	struct inode             *inode;
	struct c2_indexvec	 *ivec;
	struct c2_ivec_cursor	  cur;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p", map);
	C2_PRE(pargrp_iomap_invariant(map));

	map->pi_rtype = PIR_READREST;

	play	 = pdlayout_get(map->pi_ioreq);
	ivec	 = &map->pi_ivec;
	seg_nr	 = map->pi_ivec.iv_vec.v_nr;
	grpstart = data_size(play) * map->pi_grpid;
	grpend	 = grpstart + data_size(play);

	/* Extends first segment to align with start of parity group. */
	COUNT(ivec, 0) += (INDEX(ivec, 0) - grpstart);
	INDEX(ivec, 0)	= grpstart;

	/* Extends last segment to align with end of parity group. */
	COUNT(ivec, seg_nr - 1) = grpend - INDEX(ivec, seg_nr - 1);

	/*
	 * All io extents _not_ spanned by pargrp_iomap::pi_ivec
	 * need to be included so that _all_ pages from parity group
	 * are available to do IO.
	 */
	for (seg = 1; seg_nr > 2 && seg <= seg_nr - 2; ++seg) {
		if (seg_endpos(ivec, seg) < INDEX(ivec, seg + 1))
			COUNT(ivec, seg) += INDEX(ivec, seg + 1) -
					    seg_endpos(ivec, seg);
	}

	inode = file_to_inode(map->pi_ioreq->ir_file);
	c2_ivec_cursor_init(&cur, &map->pi_ivec);

	while (!c2_ivec_cursor_move(&cur, count)) {

		start = c2_ivec_cursor_index(&cur);
		end   = min64u(c2_round_up(start + 1, PAGE_CACHE_SIZE),
			       start + c2_ivec_cursor_step(&cur));
		count = end - start;
		page_pos_get(map, start, &row, &col);

		if (map->pi_databufs[row][col] == NULL) {
			rc = pargrp_iomap_databuf_alloc(map, row, col);
			if (rc != 0)
				C2_RETERR(rc, "databuf_alloc failed");

			if (end <= inode->i_size || (inode->i_size > 0 &&
			    page_id(end - 1) == page_id(inode->i_size - 1)))
				map->pi_databufs[row][col]->db_flags |=
					PA_READ;
		}
	}

	C2_RETURN(0);
}

static int pargrp_iomap_paritybufs_alloc(struct pargrp_iomap *map)
{
	uint32_t		  row;
	uint32_t		  col;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p", map);
	C2_PRE(pargrp_iomap_invariant(map));

	play = pdlayout_get(map->pi_ioreq);
	for (row = 0; row < parity_row_nr(play); ++row) {
		for (col = 0; col < parity_col_nr(play); ++col) {

			map->pi_paritybufs[row][col] = data_buf_alloc_init(0);
			if (map->pi_paritybufs[row][col] == NULL)
				goto err;

			map->pi_paritybufs[row][col]->db_flags |= PA_WRITE;

			if (map->pi_rtype == PIR_READOLD &&
			    file_to_inode(map->pi_ioreq->ir_file)->i_size >
			    data_size(play) * map->pi_grpid)
				map->pi_paritybufs[row][col]->db_flags |=
					PA_READ;
		}
	}
	C2_RETURN(0);
err:
	for (row = 0; row < parity_row_nr(play); ++row) {
		for (col = 0; col < parity_col_nr(play); ++col) {
			c2_free(map->pi_paritybufs[row][col]);
			map->pi_paritybufs[row][col] = NULL;
		}
	}
	C2_RETERR(-ENOMEM, "Memory allocation failed for data_buf.");
}

static c2_bcount_t seg_collate(struct pargrp_iomap   *map,
			       struct c2_ivec_cursor *cursor)
{
	uint32_t                  seg;
	uint32_t                  cnt;
	c2_bindex_t               start;
	c2_bindex_t               grpend;
	c2_bcount_t               segcount;
	struct c2_pdclust_layout *play;

	C2_PRE(map    != NULL);
	C2_PRE(cursor != NULL);

	cnt    = 0;
	play   = pdlayout_get(map->pi_ioreq);
	grpend = map->pi_grpid * data_size(play) + data_size(play); 
	start  = c2_ivec_cursor_index(cursor);

	for (seg = cursor->ic_cur.vc_seg; start < grpend &&
	     seg < cursor->ic_cur.vc_vec->v_nr - 1; ++seg) {

		segcount = seg == cursor->ic_cur.vc_seg ?
			 c2_ivec_cursor_step(cursor) :
			 cursor->ic_cur.vc_vec->v_count[seg];

		if (start + segcount ==
		    map->pi_ioreq->ir_ivec.iv_index[seg + 1]) {

			if (start + segcount >= grpend) {
				start = grpend;
				break;
			}
			start += segcount;
		} else
			break;
		++cnt;
	}

	if (cnt == 0)
		return 0;

	/* If this was last segment in vector, add its count too. */
	if (seg == cursor->ic_cur.vc_vec->v_nr - 1)
		start += cursor->ic_cur.vc_vec->v_count[seg];

	return start - c2_ivec_cursor_index(cursor);
}

static int pargrp_iomap_populate(struct pargrp_iomap	  *map,
				 const struct c2_indexvec *ivec,
				 struct c2_ivec_cursor	  *cursor)
{
	int			  rc;
	bool			  rmw;
	uint64_t		  seg;
	uint64_t		  size = 0;
	uint64_t		  grpsize;
	//uint32_t                  rseg;
	c2_bcount_t		  count = 0;
	c2_bindex_t               endpos = 0;
	//c2_bindex_t               reqindex;
	c2_bcount_t               segcount = 0;
	/* Number of pages _completely_ spanned by incoming io vector. */
	uint64_t		  nr = 0;
	/* Number of pages to be read + written for read-old approach. */
	uint64_t		  ro_page_nr;
	/* Number of pages to be read + written for read-rest approach. */
	uint64_t		  rr_page_nr;
	c2_bindex_t		  grpstart;
	c2_bindex_t		  grpend;
	c2_bindex_t		  currindex;
	struct c2_pdclust_layout *play;

	C2_ENTRY("map %p, indexvec %p", map, ivec);
	C2_PRE(map  != NULL);
	C2_PRE(ivec != NULL);

	play	 = pdlayout_get(map->pi_ioreq);
	grpsize	 = data_size(play);
	grpstart = grpsize * map->pi_grpid;
	grpend	 = grpstart + grpsize;
	endpos   = c2_ivec_cursor_index(cursor);

	for (seg = cursor->ic_cur.vc_seg; seg < SEG_NR(ivec) &&
	     INDEX(ivec, seg) < grpend; ++seg) {
		currindex = seg == cursor->ic_cur.vc_seg ?
			    c2_ivec_cursor_index(cursor) : INDEX(ivec, seg);
		size += min64u(seg_endpos(ivec, seg), grpend) - currindex;
	}

	rmw = size < grpsize && map->pi_ioreq->ir_type == IRT_WRITE;
	C2_LOG(C2_INFO, "Group id %llu is %s", map->pi_grpid,
	       rmw ? "rmw" : "aligned");

	size = map->pi_ioreq->ir_file->f_dentry->d_inode->i_size;

	for (seg = 0; !c2_ivec_cursor_move(cursor, count) &&
	     c2_ivec_cursor_index(cursor) < grpend;) {
		/*
		 * Skips the current segment if it is completely spanned by
		 * rounding up/down of earlier segment.
		 */
		if (map->pi_ops->pi_spans_seg(map, c2_ivec_cursor_index(cursor),
					      c2_ivec_cursor_step(cursor))) {
			count = c2_ivec_cursor_step(cursor);
			continue;
		}

		INDEX(&map->pi_ivec, seg) = c2_ivec_cursor_index(cursor);
		endpos = min64u(grpend, c2_ivec_cursor_index(cursor) +
				c2_ivec_cursor_step(cursor));

		segcount = seg_collate(map, cursor);
		if (segcount > 0)
			endpos = INDEX(&map->pi_ivec, seg) + segcount;

		COUNT(&map->pi_ivec, seg) = endpos - INDEX(&map->pi_ivec, seg);

		/* For read IO request, IO should not go beyond EOF. */
		if (map->pi_ioreq->ir_type == IRT_READ &&
		    seg_endpos(&map->pi_ivec, seg) > size)
			COUNT(&map->pi_ivec, seg) = size - INDEX(&map->pi_ivec,
								 seg);

		/*
		 * If current segment is _partially_ spanned by previous
		 * segment in pargrp_iomp::pi_ivec, start of segment is
		 * rounded up to move to next page.
		 */
		if (seg > 0 && INDEX(&map->pi_ivec, seg) <
		    seg_endpos(&map->pi_ivec, seg - 1)) {
			c2_bindex_t newindex;

			newindex = c2_round_up(INDEX(&map->pi_ivec, seg) + 1,
					       PAGE_CACHE_SIZE);
			       
			COUNT(&map->pi_ivec, seg) -= (newindex -
					INDEX(&map->pi_ivec, seg));

			INDEX(&map->pi_ivec, seg)  = c2_round_up(
					INDEX(&map->pi_ivec, seg) + 1,
					PAGE_CACHE_SIZE);
		}

		++map->pi_ivec.iv_vec.v_nr;
		rc = map->pi_ops->pi_seg_process(map, seg, rmw);
		if (rc != 0)
			C2_RETERR(rc, "seg_process failed");

		INDEX(&map->pi_ivec, seg) =
			round_down(INDEX(&map->pi_ivec, seg),
				   PAGE_CACHE_SIZE);

		COUNT(&map->pi_ivec, seg) =
			round_up(endpos, PAGE_CACHE_SIZE) -
			INDEX(&map->pi_ivec, seg);

		count = endpos - c2_ivec_cursor_index(cursor);
		++seg;
	}

	/*
	 * Decides whether to undertake read-old approach or read-rest for
	 * an rmw IO request.
	 * By default, the segments in index vector pargrp_iomap::pi_ivec
	 * are suitable for read-old approach.
	 * Hence the index vector is changed only if read-rest approach
	 * is selected.
	 */
	if (rmw) {
		nr = map->pi_ops->pi_fullpages_find(map);

		/*
		 * Can use number of data_buf structures instead of using
		 * indexvec_page_nr().
		 */
		ro_page_nr = /* Number of pages to be read. */
			     indexvec_page_nr(&map->pi_ivec.iv_vec) - nr +
			     parity_units_page_nr(play) +
			     /* Number of pages to be written. */
			     indexvec_page_nr(&map->pi_ivec.iv_vec) +
			     parity_units_page_nr(play);

		rr_page_nr = /* Number of pages to be read. */
			     page_nr(grpend - grpstart) - nr +
			     /* Number of pages to be written. */
			     indexvec_page_nr(&map->pi_ivec.iv_vec) +
			     parity_units_page_nr(play);

		if (rr_page_nr < ro_page_nr) {
			rc = map->pi_ops->pi_readrest(map);
			if (rc != 0)
				C2_RETERR(rc, "readrest failed");
		} else {
			map->pi_rtype = PIR_READOLD;
			rc = map->pi_ops->pi_readold_auxbuf_alloc(map);
		}
	}

	if (map->pi_ioreq->ir_type == IRT_WRITE)
		rc = pargrp_iomap_paritybufs_alloc(map);

	C2_POST(ergo(rc == 0, pargrp_iomap_invariant(map)));

	C2_RETURN(rc);
}

static int ioreq_iomaps_prepare(struct io_request *req)
{
	int			  rc;
	uint64_t		  seg;
	uint64_t		  grp;
	uint64_t		  id;
	uint64_t		  grpstart;
	uint64_t		  grpend;
	uint64_t		 *grparray;
	struct c2_pdclust_layout *play;
	struct c2_ivec_cursor	  cursor;

	C2_ENTRY("io_request %p", req);
	C2_PRE(req != NULL);

	play = pdlayout_get(req);

	C2_ALLOC_ARR_ADDB(grparray, max64u(c2_vec_count(&req->ir_ivec.iv_vec) /
			  data_size(play) + 1, SEG_NR(&req->ir_ivec)),
			  &c2t1fs_addb, &io_addb_loc);
	if (grparray == NULL)
		C2_RETERR(-ENOMEM, "Failed to allocate memory for int array");

	/*
	 * Finds out total number of parity groups spanned by
	 * io_request::ir_ivec.
	 */
	for (seg = 0; seg < SEG_NR(&req->ir_ivec); ++seg) {
		grpstart = group_id(INDEX(&req->ir_ivec, seg), data_size(play));
		grpend	 = group_id(seg_endpos(&req->ir_ivec, seg) - 1,
				    data_size(play));
		for (grp = grpstart; grp <= grpend; ++grp) {
			for (id = 0; id < req->ir_iomap_nr; ++id)
				if (grparray[id] == grp)
					break;
			if (id == req->ir_iomap_nr) {
				grparray[id] = grp;
				++req->ir_iomap_nr;
			}
		}
	}
	c2_free(grparray);

	C2_LOG(C2_DEBUG, "Number of pargrp_iomap structures : %llu",
	       req->ir_iomap_nr);

	/* req->ir_iomaps is zeroed out on allocation. */
	C2_ALLOC_ARR_ADDB(req->ir_iomaps, req->ir_iomap_nr, &c2t1fs_addb,
			  &io_addb_loc);
	if (req->ir_iomaps == NULL) {
		rc = -ENOMEM;
		goto failed;
	}

	c2_ivec_cursor_init(&cursor, &req->ir_ivec);

	/*
	 * cursor is advanced maximum by parity group size in one iteration
	 * of this loop.
	 * This is done by pargrp_iomap::pi_ops::pi_populate().
	 */
	for (id = 0; !c2_ivec_cursor_move(&cursor, 0); ++id) {

		C2_ASSERT(id < req->ir_iomap_nr);
		C2_ASSERT(req->ir_iomaps[id] == NULL);
		C2_ALLOC_PTR_ADDB(req->ir_iomaps[id], &c2t1fs_addb,
				  &io_addb_loc);
		if (req->ir_iomaps[id] == NULL) {
			rc = -ENOMEM;
			goto failed;
		}

		++iommstats.a_pargrp_iomap_nr;
		rc = pargrp_iomap_init(req->ir_iomaps[id], req,
				       group_id(c2_ivec_cursor_index(&cursor),
						data_size(play)));
		if (rc != 0)
			goto failed;

		rc = req->ir_iomaps[id]->pi_ops->pi_populate(req->
				ir_iomaps[id], &req->ir_ivec, &cursor);
		if (rc != 0)
			goto failed;
		C2_LOG(C2_INFO, "pargrp_iomap id : %llu populated",
		       req->ir_iomaps[id]->pi_grpid);
	}

	C2_RETURN(0);
failed:
	req->ir_ops->iro_iomaps_destroy(req);
	C2_RETERR(rc, "iomaps_prepare failed");
}

static void ioreq_iomaps_destroy(struct io_request *req)
{
	uint64_t id;

	C2_ENTRY("io_request %p", req);

	C2_PRE(req != NULL);
	C2_PRE(req->ir_iomaps != NULL);

	for (id = 0; id < req->ir_iomap_nr ; ++id) {
		if (req->ir_iomaps[id] != NULL) {
			pargrp_iomap_fini(req->ir_iomaps[id]);
			c2_free(req->ir_iomaps[id]);
			req->ir_iomaps[id] = NULL;
			++iommstats.d_pargrp_iomap_nr;
		}
	}
	c2_free(req->ir_iomaps);
	req->ir_iomap_nr = 0;
	req->ir_iomaps	 = NULL;
}

/*
 * Creates target_ioreq objects as required and populates
 * target_ioreq::ti_ivec and target_ioreq::ti_bufvec.
 */
static int nw_xfer_io_prepare(struct nw_xfer_request *xfer)
{
	int			    rc;
	uint64_t		    map;
	uint64_t		    unit;
	uint64_t		    unit_size;
	uint64_t		    count;
	uint64_t		    pgstart;
	uint64_t		    pgend;
	/* Extent representing a data unit. */
	struct c2_ext		    u_ext;
	/* Extent representing resultant extent. */
	struct c2_ext		    r_ext;
	/* Extent representing a segment from index vector. */
	struct c2_ext		    v_ext;
	struct io_request	   *req;
	struct target_ioreq	   *ti;
	struct c2_ivec_cursor	    cursor;
	struct c2_pdclust_layout   *play;
	enum c2_pdclust_unit_type   unit_type;
	struct c2_pdclust_src_addr  src;
	struct c2_pdclust_tgt_addr  tgt;

	C2_ENTRY("nw_xfer_request %p", xfer);
	C2_PRE(nw_xfer_request_invariant(xfer));

	req	  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	play	  = pdlayout_get(req);
	unit_size = layout_unit_size(play);

	for (map = 0; map < req->ir_iomap_nr; ++map) {

		count        = 0;
		pgstart	     = data_size(play) * req->ir_iomaps[map]->
			       pi_grpid;
		pgend	     = pgstart + data_size(play);
		src.sa_group = req->ir_iomaps[map]->pi_grpid;

		/* Cursor for pargrp_iomap::pi_ivec. */
		c2_ivec_cursor_init(&cursor, &req->ir_iomaps[map]->pi_ivec);

		while (!c2_ivec_cursor_move(&cursor, count)) {

			unit = (c2_ivec_cursor_index(&cursor) - pgstart) /
			       unit_size;

			u_ext.e_start = pgstart + unit * unit_size;
			u_ext.e_end   = u_ext.e_start + unit_size;

			v_ext.e_start  = c2_ivec_cursor_index(&cursor);
			v_ext.e_end    = v_ext.e_start +
					  c2_ivec_cursor_step(&cursor);

			c2_ext_intersection(&u_ext, &v_ext, &r_ext);
			if (!c2_ext_is_valid(&r_ext))
				continue;

			count	  = c2_ext_length(&r_ext);
			unit_type = c2_pdclust_unit_classify(play, unit);
			if (unit_type == C2_PUT_SPARE ||
			    unit_type == C2_PUT_PARITY)
				continue;

			src.sa_unit = unit;
			rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src, &tgt,
							   &ti);
			if (rc != 0)
				goto err;

			ti->ti_ops->tio_seg_add(ti, tgt.ta_frame, r_ext.e_start,
						0, c2_ext_length(&r_ext),
						src.sa_unit);
		}

		/* Maps parity units only in case of write IO. */
		if (req->ir_type == IRT_WRITE) {

			/*
			 * The loop iterates 2 * layout_k() times since
			 * number of spare units equal number of parity units.
			 * In case of spare units, no IO is done.
			 */
			c2_bindex_t par_offset = 0;

			for (unit = 0; unit < 2 * layout_k(play); ++unit) {

				src.sa_unit = layout_n(play) + unit;
				rc = xfer->nxr_ops->nxo_tioreq_map(xfer, &src,
								   &tgt, &ti);
				if (rc != 0)
					goto err;

				if (c2_pdclust_unit_classify(play,
				    src.sa_unit) == C2_PUT_PARITY)
					ti->ti_ops->tio_seg_add(ti,
							tgt.ta_frame, pgstart,
							0,
							layout_unit_size(play),
							src.sa_unit);
				par_offset += layout_unit_size(play);
			}
		}
	}

	C2_RETURN(0);
err:
	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		tioreqs_tlist_del(ti);
		target_ioreq_fini(ti);
		c2_free(ti);
		ti = NULL;
	} c2_tl_endfor;

	C2_RETERR(rc, "io_prepare failed");
}

static inline int ioreq_sm_timedwait(struct io_request *req,
			             uint64_t           state)
{
	int rc;

	C2_ENTRY("ioreq = %p, current state = %d, waiting for state %llu",
		 req, ioreq_sm_state(req), state);
	C2_PRE(req != NULL);

	c2_mutex_lock(&req->ir_sm.sm_grp->s_lock);
	rc = c2_sm_timedwait(&req->ir_sm, (1 << state), C2_TIME_NEVER);
	c2_mutex_unlock(&req->ir_sm.sm_grp->s_lock);

	C2_RETURN(rc);
}

static int ioreq_iosm_handle(struct io_request *req)
{
	int		     rc;
	int		     res;
	bool		     rmw;
	uint64_t	     map;
	struct inode	    *inode;
	struct target_ioreq *ti;

	C2_ENTRY("io_request %p", req);
	C2_PRE(io_request_invariant(req));

	for (map = 0; map < req->ir_iomap_nr; ++map) {
		if (C2_IN(req->ir_iomaps[map]->pi_rtype,
			  (PIR_READOLD, PIR_READREST)))
			break;
	}

	/*
	 * Since c2_sm is part of io_request, for any parity group
	 * which is partial, read-modify-write state transition is followed
	 * for all parity groups.
	 */
	if (map == req->ir_iomap_nr) {
		enum io_req_state state;

		rmw = false;
		state = req->ir_type == IRT_READ ? IRS_READING :
						   IRS_WRITING;
		if (state == IRS_WRITING) {
			rc = req->ir_ops->iro_user_data_copy(req,
					CD_COPY_FROM_USER, 0);
			if (rc != 0)
				goto fail;

			rc = req->ir_ops->iro_parity_recalc(req);
			if (rc != 0)
				goto fail;
		}
		ioreq_sm_state_set(req, state);
		rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail;

		state = req->ir_type == IRT_READ ? IRS_READ_COMPLETE:
						   IRS_WRITE_COMPLETE;
		rc    = ioreq_sm_timedwait(req, state);
		if (rc != 0)
			ioreq_sm_failed(req, rc);

		C2_ASSERT(ioreq_sm_state(req) == state);

		if (state == IRS_READ_COMPLETE) {
			rc = req->ir_ops->iro_user_data_copy(req,
					CD_COPY_TO_USER, 0);
			if (rc != 0)
				ioreq_sm_failed(req, rc);
		}
	} else {
		uint32_t    seg;
		c2_bcount_t read_pages = 0;

		rmw = true;
		c2_tl_for (tioreqs, &req->ir_nwxfer.nxr_tioreqs, ti) {
			for (seg = 0; seg < ti->ti_bufvec.ov_vec.v_nr; ++seg)
				if (ti->ti_pageattrs[seg] & PA_READ)
					++read_pages;
		} c2_tl_endfor;

		/* Read IO is issued only if byte count > 0. */
		if (read_pages > 0) {
			ioreq_sm_state_set(req, IRS_READING);
			rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->
								  ir_nwxfer);
			if (rc != 0)
				goto fail;
		}

		/*
		 * If fops dispatch fails, we need to wait till all io fop
		 * callbacks are acked since IO fops have already been
		 * dispatched.
		 */
		res = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER,
						      PA_FULLPAGE_MODIFY);

		/* Waits for read completion if read IO was issued. */
		if (read_pages > 0) {
			rc = ioreq_sm_timedwait(req, IRS_READ_COMPLETE);

			if (res != 0 || rc != 0) {
				rc = res != 0 ? res : rc;
				goto fail;
			}
		}

		rc = req->ir_ops->iro_user_data_copy(req, CD_COPY_FROM_USER,
						     PA_PARTPAGE_MODIFY);
		if (rc != 0)
			goto fail;

		/* Finalizes the old read fops. */
		if (read_pages > 0) {
			req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer,
							     rmw);
			if (req->ir_rc != 0)
				goto fail;
		}

		ioreq_sm_state_set(req, IRS_WRITING);
		rc = req->ir_ops->iro_parity_recalc(req);
		if (rc != 0)
			goto fail;

		rc = req->ir_nwxfer.nxr_ops->nxo_dispatch(&req->ir_nwxfer);
		if (rc != 0)
			goto fail;

		rc = ioreq_sm_timedwait(req, IRS_WRITE_COMPLETE);
		if (rc != 0)
			goto fail;
	}

	/*
	 * Updates file size on successful write IO.
	 * New file size is maximum value between old file size and
	 * valid file position written in current write IO call.
	 */
	inode = req->ir_file->f_dentry->d_inode;
	if (ioreq_sm_state(req) == IRS_WRITE_COMPLETE) {
		inode->i_size = max64u(inode->i_size,
				seg_endpos(&req->ir_ivec,
					req->ir_ivec.iv_vec.v_nr - 1));
		C2_LOG(C2_INFO, "File size set to %llu", inode->i_size);
	}

	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, rmw);

	if (rmw)
		ioreq_sm_state_set(req, IRS_REQ_COMPLETE);

	C2_RETURN(0);
fail:
	ioreq_sm_failed(req, rc);
	req->ir_nwxfer.nxr_ops->nxo_complete(&req->ir_nwxfer, false);
	C2_RETERR(rc, "ioreq_iosm_handle failed");
}

static int io_request_init(struct io_request  *req,
			   struct file	      *file,
			   struct iovec	      *iov,
			   struct c2_indexvec *ivec,
			   enum io_req_type    rw)
{
	int	 rc;
	uint32_t seg;

	C2_ENTRY("io_request %p, rw %d", req, rw);
	C2_PRE(req  != NULL);
	C2_PRE(file != NULL);
	C2_PRE(iov  != NULL);
	C2_PRE(ivec != NULL);
	C2_PRE(C2_IN(rw, (IRT_READ, IRT_WRITE)));

	req->ir_rc	  = 0;
	req->ir_ops	  = &ioreq_ops;
	req->ir_file	  = file;
	req->ir_type	  = rw;
	req->ir_iovec	  = iov;
	req->ir_iomap_nr  = 0;
	req->ir_copied_nr = 0;

	io_request_bob_init(req);
	nw_xfer_request_init(&req->ir_nwxfer);

	c2_sm_init(&req->ir_sm, &io_sm_conf, IRS_INITIALIZED,
		   file_to_smgroup(req->ir_file), &c2t1fs_addb);

	rc = c2_indexvec_alloc(&req->ir_ivec, ivec->iv_vec.v_nr,
			       &c2t1fs_addb, &io_addb_loc);

	if (rc != 0)
		C2_RETERR(-ENOMEM, "Allocation failed for c2_indexvec");

	for (seg = 0; seg < ivec->iv_vec.v_nr; ++seg) {
		req->ir_ivec.iv_index[seg] = ivec->iv_index[seg];
		req->ir_ivec.iv_vec.v_count[seg] = ivec->iv_vec.v_count[seg];
	}

	/* Sorts the index vector in increasing order of file offset. */
	indexvec_sort(&req->ir_ivec);

	C2_POST(ergo(rc == 0, io_request_invariant(req)));
	C2_RETURN(rc);
}

static void io_request_fini(struct io_request *req)
{
	struct target_ioreq *ti;

	C2_ENTRY("io_request %p", req);
	C2_PRE(io_request_invariant(req));

	c2_sm_fini(&req->ir_sm);
	io_request_bob_fini(req);
	req->ir_file   = NULL;
	req->ir_iovec  = NULL;
	req->ir_iomaps = NULL;
	req->ir_ops    = NULL;
	c2_indexvec_free(&req->ir_ivec);

	c2_tl_for (tioreqs, &req->ir_nwxfer.nxr_tioreqs, ti) {
		   tioreqs_tlist_del(ti);
		   /*
		    * All io_req_fop structures in list target_ioreq::ti_iofops
		    * are already finalized in nw_xfer_req_complete().
		    */
		   target_ioreq_fini(ti);
		   c2_free(ti);
		   ++iommstats.d_target_ioreq_nr;
	} c2_tl_endfor;

	nw_xfer_request_fini(&req->ir_nwxfer);
	C2_LEAVE();
}

static void data_buf_init(struct data_buf *buf, void *addr, uint64_t flags)
{
	C2_PRE(buf  != NULL);
	C2_PRE(addr != NULL);

	data_buf_bob_init(buf);
	buf->db_flags = flags;
	c2_buf_init(&buf->db_buf, addr, PAGE_CACHE_SIZE);
}

static void data_buf_fini(struct data_buf *buf)
{
	C2_PRE(buf != NULL);

	data_buf_bob_fini(buf);
	buf->db_flags = PA_NONE;
}

static int nw_xfer_tioreq_map(struct nw_xfer_request	  *xfer,
			      struct c2_pdclust_src_addr  *src,
			      struct c2_pdclust_tgt_addr  *tgt,
			      struct target_ioreq	 **out)
{
	int			  rc;
	struct c2_fid		  tfid;
	struct io_request	 *req;
	struct c2_rpc_session	 *session;
	struct c2_pdclust_layout *play;

	C2_ENTRY("nw_xfer_request %p", xfer);
	C2_PRE(nw_xfer_request_invariant(xfer));
	C2_PRE(src != NULL);
	C2_PRE(tgt != NULL);

	req  = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);
	play = pdlayout_get(req);

	c2_pdclust_instance_map(pdlayout_instance(layout_instance(req)),
				src, tgt);
	tfid	= target_fid(req, tgt);
	session = target_session(req, tfid);

	rc = nw_xfer_tioreq_get(xfer, &tfid, session,
				layout_unit_size(play) * req->ir_iomap_nr,
				out);
	C2_RETURN(rc);
}

static int target_ioreq_init(struct target_ioreq    *ti,
			     struct nw_xfer_request *xfer,
			     const struct c2_fid    *cobfid,
			     struct c2_rpc_session  *session,
			     uint64_t		     size)
{
	int rc;

	C2_ENTRY("target_ioreq %p, nw_xfer_request %p, fid %p",
		 ti, xfer, cobfid);
	C2_PRE(ti      != NULL);
	C2_PRE(xfer    != NULL);
	C2_PRE(cobfid  != NULL);
	C2_PRE(session != NULL);
	C2_PRE(size    >  0);

	ti->ti_rc        = 0;
	ti->ti_ops       = &tioreq_ops;
	ti->ti_fid       = *cobfid;
	ti->ti_nwxfer    = xfer;
	ti->ti_session   = session;
	ti->ti_parbytes  = 0;
	ti->ti_databytes = 0;

	iofops_tlist_init(&ti->ti_iofops);
	tioreqs_tlink_init(ti);
	target_ioreq_bob_init(ti);

	ti->ti_bufvec.ov_vec.v_nr = page_nr(size);

	rc = c2_indexvec_alloc(&ti->ti_ivec, page_nr(size),
			       &c2t1fs_addb, &io_addb_loc);
	if (rc != 0)
		goto fail;
	/*
	 * This value is incremented when new segments are added to the
	 * index vector.
	 */
	ti->ti_ivec.iv_vec.v_nr = 0;

	C2_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_vec.v_count,
			  ti->ti_bufvec.ov_vec.v_nr, &c2t1fs_addb,
			  &io_addb_loc);
	if (ti->ti_bufvec.ov_vec.v_count == NULL)
		goto fail;

	C2_ALLOC_ARR_ADDB(ti->ti_bufvec.ov_buf, ti->ti_bufvec.ov_vec.v_nr,
			  &c2t1fs_addb, &io_addb_loc);
	if (ti->ti_bufvec.ov_buf == NULL)
		goto fail;

	C2_ALLOC_ARR_ADDB(ti->ti_pageattrs, ti->ti_bufvec.ov_vec.v_nr,
			  &c2t1fs_addb, &io_addb_loc);
	if (ti->ti_pageattrs == NULL)
		goto fail;

	C2_POST(target_ioreq_invariant(ti));
	C2_RETURN(0);
fail:
	c2_indexvec_free(&ti->ti_ivec);
	c2_free(ti->ti_bufvec.ov_vec.v_count);
	c2_free(ti->ti_bufvec.ov_buf);

	C2_RETERR(-ENOMEM, "Failed to allocate memory in target_ioreq_init");
}

static void target_ioreq_fini(struct target_ioreq *ti)
{
	C2_ENTRY("target_ioreq %p", ti);
	C2_PRE(target_ioreq_invariant(ti));

	target_ioreq_bob_fini(ti);
	tioreqs_tlink_fini(ti);
	iofops_tlist_fini(&ti->ti_iofops);
	ti->ti_ops     = NULL;
	ti->ti_session = NULL;
	ti->ti_nwxfer  = NULL;

	/* Resets the number of segments in vector. */
	if (ti->ti_ivec.iv_vec.v_nr == 0)
		ti->ti_ivec.iv_vec.v_nr = ti->ti_bufvec.ov_vec.v_nr;

	c2_indexvec_free(&ti->ti_ivec);
	c2_free(ti->ti_bufvec.ov_buf);
	c2_free(ti->ti_bufvec.ov_vec.v_count);
	c2_free(ti->ti_pageattrs);

	ti->ti_bufvec.ov_buf	     = NULL;
	ti->ti_bufvec.ov_vec.v_count = NULL;
	ti->ti_pageattrs	     = NULL;
	C2_LEAVE();
}

static struct target_ioreq *target_ioreq_locate(struct nw_xfer_request *xfer,
						struct c2_fid	       *fid)
{
	struct target_ioreq *ti;

	C2_ENTRY("nw_xfer_request %p, fid %p", xfer, fid);
	C2_PRE(nw_xfer_request_invariant(xfer));
	C2_PRE(fid != NULL);

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		if (c2_fid_eq(&ti->ti_fid, fid))
			break;
	} c2_tl_endfor;

	C2_LEAVE();
	return ti;
}

static int nw_xfer_tioreq_get(struct nw_xfer_request *xfer,
			      struct c2_fid	     *fid,
			      struct c2_rpc_session  *session,
			      uint64_t		      size,
			      struct target_ioreq   **out)
{
	int		     rc = 0;
	struct target_ioreq *ti;

	C2_ENTRY("nw_xfer_request %p, fid %p", xfer, fid);
	C2_PRE(nw_xfer_request_invariant(xfer));
	C2_PRE(fid     != NULL);
	C2_PRE(session != NULL);
	C2_PRE(out     != NULL);

	ti = target_ioreq_locate(xfer, fid);
	if (ti == NULL) {
		C2_ALLOC_PTR_ADDB(ti, &c2t1fs_addb, &io_addb_loc);
		if (ti == NULL)
			C2_RETERR(-ENOMEM, "Failed to allocate memory"
				  "for target_ioreq");

		++iommstats.a_target_ioreq_nr;
		rc = target_ioreq_init(ti, xfer, fid, session, size);
		if (rc == 0) {
			tioreqs_tlist_add(&xfer->nxr_tioreqs, ti);
			C2_LOG(C2_INFO, "New target_ioreq added for fid\
					%llu:%llu", fid->f_container,
					fid->f_key);
		}
		else
			c2_free(ti);
	}
	*out = ti;
	C2_RETURN(rc);
}

static struct data_buf *data_buf_alloc_init(enum page_attr pattr)
{
	struct data_buf *buf;
	unsigned long	 addr;

	C2_ENTRY();
	addr = get_zeroed_page(GFP_KERNEL);
	if (addr == 0) {
		C2_LOG(C2_ERROR, "Failed to get free page");
		return NULL;
	}

	++iommstats.a_page_nr;
	C2_ALLOC_PTR_ADDB(buf, &c2t1fs_addb, &io_addb_loc);
	if (buf == NULL) {
		free_page(addr);
		C2_LOG(C2_ERROR, "Failed to allocate data_buf");
		return NULL;
	}

	++iommstats.a_data_buf_nr;
	data_buf_init(buf, (void *)addr, pattr);
	C2_POST(data_buf_invariant(buf));
	C2_LEAVE();
	return buf;
}

static void buf_page_free(struct c2_buf *buf)
{
	C2_PRE(buf != NULL);

	free_page((unsigned long)buf->b_addr);
	++iommstats.d_page_nr;
	buf->b_addr = NULL;
	buf->b_nob  = 0;
}

static void data_buf_dealloc_fini(struct data_buf *buf)
{
	C2_ENTRY("data_buf %p", buf);
	C2_PRE(data_buf_invariant(buf));

	if (buf->db_buf.b_addr != NULL)
		buf_page_free(&buf->db_buf);

	if (buf->db_auxbuf.b_addr != NULL)
		buf_page_free(&buf->db_auxbuf);

	data_buf_fini(buf);
	c2_free(buf);
	++iommstats.d_data_buf_nr;
	C2_LEAVE();
}

static void target_ioreq_seg_add(struct target_ioreq *ti,
				 uint64_t	      frame,
				 c2_bindex_t	      gob_offset,
				 c2_bindex_t	      par_offset,
				 c2_bcount_t	      count,
				 uint64_t	      unit)
{
	uint32_t		   seg;
	c2_bindex_t		   toff;
	c2_bindex_t		   goff;
	c2_bindex_t		   pgstart;
	c2_bindex_t		   pgend;
	struct data_buf		  *buf;
	struct io_request	  *req;
	struct pargrp_iomap	  *map;
	struct c2_pdclust_layout  *play;
	enum c2_pdclust_unit_type  unit_type;

	C2_ENTRY("target_ioreq %p, gob_offset %llu, count %llu",
		 ti, gob_offset, count);
	C2_PRE(target_ioreq_invariant(ti));

	req	= bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
			 &ioreq_bobtype);
	play	= pdlayout_get(req);

	unit_type = c2_pdclust_unit_classify(play, unit);
	C2_ASSERT(C2_IN(unit_type, (C2_PUT_DATA, C2_PUT_PARITY)));

	toff	= target_offset(frame, play, gob_offset);
	req->ir_ops->iro_iomap_locate(req, group_id(gob_offset,
				      data_size(play)), &map);
	C2_ASSERT(map != NULL);
	pgstart = toff;
	goff    = unit_type == C2_PUT_DATA ? gob_offset : par_offset;

	while (pgstart < toff + count) {
		pgend = min64u(pgstart + PAGE_CACHE_SIZE, toff + count);
		seg   = SEG_NR(&ti->ti_ivec);

		INDEX(&ti->ti_ivec, seg) = pgstart;
		COUNT(&ti->ti_ivec, seg) = pgend - pgstart;

		ti->ti_bufvec.ov_vec.v_count[seg] = pgend - pgstart;

		if (unit_type == C2_PUT_DATA) {
			uint32_t row;
			uint32_t col;

			page_pos_get(map, goff, &row, &col);
			buf = map->pi_databufs[row][col];

			ti->ti_pageattrs[seg] |= PA_DATA;
			C2_LOG(C2_INFO, "Data seg added");
		} else {
			buf = map->pi_paritybufs[page_id(goff)]
						[unit % data_col_nr(play)];
			ti->ti_pageattrs[seg] |= PA_PARITY;
			C2_LOG(C2_INFO, "Parity seg added");
		}

		ti->ti_bufvec.ov_buf[seg]  = buf->db_buf.b_addr;
		ti->ti_pageattrs[seg] |= buf->db_flags;

		C2_LOG(C2_INFO, "Seg id %d [%llu, %llu] added to target_ioreq"
		       "with fid %llu:%llu with flags %d", seg,
		       INDEX(&ti->ti_ivec, seg), COUNT(&ti->ti_ivec, seg),
		       ti->ti_fid.f_container, ti->ti_fid.f_key,
		       ti->ti_pageattrs[seg]);
		goff += COUNT(&ti->ti_ivec, seg);
		++ti->ti_ivec.iv_vec.v_nr;
		pgstart = pgend;
	}
	C2_LEAVE();
}

static int io_req_fop_init(struct io_req_fop   *fop,
			   struct target_ioreq *ti,
			   enum page_attr       pattr)
{
	int		   rc;
	struct io_request *req;

	C2_ENTRY("io_req_fop %p, target_ioreq %p", fop, ti);
	C2_PRE(fop != NULL);
	C2_PRE(ti  != NULL);
	C2_PRE(C2_IN(pattr, (PA_DATA, PA_PARITY)));

	io_req_fop_bob_init(fop);
	iofops_tlink_init(fop);
	fop->irf_pattr     = pattr;
	fop->irf_tioreq	   = ti;
	fop->irf_ast.sa_cb = io_bottom_half;

	req = bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
		     &ioreq_bobtype);
	fop->irf_ast.sa_mach = &req->ir_sm;

	rc  = c2_io_fop_init(&fop->irf_iofop, ioreq_sm_state(req) ==
			     IRS_READING ? &c2_fop_cob_readv_fopt :
			     &c2_fop_cob_writev_fopt);
	/*
	 * Changes ri_ops of rpc item so as to execute c2t1fs's own
	 * callback on receiving a reply.
	 */
	fop->irf_iofop.if_fop.f_item.ri_ops = &c2t1fs_item_ops;

	C2_POST(ergo(rc == 0, io_req_fop_invariant(fop)));
	C2_RETURN(rc);
}

static void io_req_fop_fini(struct io_req_fop *fop)
{
	C2_ENTRY("io_req_fop %p", fop);
	C2_PRE(io_req_fop_invariant(fop));

	/*
	 * IO fop is finalized (c2_io_fop_fini()) through rpc sessions code
	 * using c2_rpc_item::c2_rpc_item_ops::rio_free().
	 * see c2_io_item_free().
	 */

	iofops_tlink_fini(fop);

	/*
	 * io_req_bob_fini() is not done here so that struct io_req_fop
	 * can be retrieved from struct c2_rpc_item using bob_of() and
	 * magic numbers can be checked.
	 */

	fop->irf_tioreq = NULL;
	fop->irf_ast.sa_cb = NULL;
	fop->irf_ast.sa_mach = NULL;
	C2_LEAVE();
}

static void irfop_fini(struct io_req_fop *irfop)
{
	C2_ENTRY("io_req_fop %p", irfop);
	C2_PRE(irfop != NULL);

	c2_rpc_bulk_buflist_empty(&irfop->irf_iofop.if_rbulk);
	io_req_fop_fini(irfop);
	c2_free(irfop);
	C2_LEAVE();
}

/*
 * This function can be used by the ioctl which supports fully vectored
 * scatter-gather IO. The caller is supposed to provide an index vector
 * aligned with user buffers in struct iovec array.
 * This function is also used by file->f_op->aio_{read/write} path.
 */
ssize_t c2t1fs_aio(struct kiocb	      *kcb,
		   const struct iovec *iov,
		   struct c2_indexvec *ivec,
		   enum io_req_type    rw)
{
	int		   rc;
	ssize_t		   count;
	struct io_request *req;

	C2_ENTRY("indexvec %p, rw %d", ivec, rw);
	C2_PRE(kcb  != NULL);
	C2_PRE(iov  != NULL);
	C2_PRE(ivec != NULL);
	C2_PRE(C2_IN(rw, (IRT_READ, IRT_WRITE)));

	C2_ALLOC_PTR_ADDB(req, &c2t1fs_addb, &io_addb_loc);
	if (req == NULL)
		C2_RETERR(-ENOMEM, "Failed to allocate memory for io_request");
	++iommstats.a_ioreq_nr;

	rc = io_request_init(req, kcb->ki_filp, (struct iovec *)iov, ivec, rw);
	if (rc != 0) {
		count = 0;
		goto last;
	}

	rc = req->ir_ops->iro_iomaps_prepare(req);
	if (rc != 0) {
		io_request_fini(req);
		count = 0;
		goto last;
	}

	rc = req->ir_nwxfer.nxr_ops->nxo_prepare(&req->ir_nwxfer);
	if (rc != 0) {
		req->ir_ops->iro_iomaps_destroy(req);
		io_request_fini(req);
		count = 0;
		goto last;
	}

	rc    = req->ir_ops->iro_iosm_handle(req);
	C2_LOG(C2_INFO, "nxr_bytes = %llu, copied_nr = %llu",
		req->ir_nwxfer.nxr_bytes, req->ir_copied_nr);
	count = min64u(req->ir_nwxfer.nxr_bytes, req->ir_copied_nr);

	req->ir_ops->iro_iomaps_destroy(req);
	io_request_fini(req);
last:
	c2_free(req);
	++iommstats.d_ioreq_nr;
	C2_LOG(C2_INFO, "io request returned %lu bytes", count);
	C2_LEAVE();

	return count;
}

static struct c2_indexvec *indexvec_create(unsigned long       seg_nr,
					   const struct iovec *iov,
					   loff_t	       pos)
{
	int		    rc;
	uint32_t	    i;
	struct c2_indexvec *ivec;

	/*
	 * Apparently, we need to use a new API to process io request
	 * which can accept c2_indexvec so that it can be reused by
	 * the ioctl which provides fully vectored scatter-gather IO
	 * to cluster library users.
	 * For that, we need to prepare a c2_indexvec and supply it
	 * to this function.
	 */
	C2_ENTRY("seg_nr %lu position %llu", seg_nr, pos);
	C2_ALLOC_PTR_ADDB(ivec, &c2t1fs_addb, &io_addb_loc);
	if (ivec == NULL) {
		C2_LEAVE();
		return NULL;
	}

	rc = c2_indexvec_alloc(ivec, seg_nr, &c2t1fs_addb, &io_addb_loc);
	if (rc != 0) {
		c2_free(ivec);
		return NULL;
	}

	for (i = 0; i < seg_nr; ++i) {
		ivec->iv_index[i] = pos;
		ivec->iv_vec.v_count[i] = iov[i].iov_len;
		pos += iov[i].iov_len;
	}
	C2_POST(c2_vec_count(&ivec->iv_vec) > 0);

	C2_LEAVE();
	return ivec;
}

static ssize_t file_aio_write(struct kiocb	 *kcb,
			      const struct iovec *iov,
			      unsigned long	  seg_nr,
			      loff_t		  pos)
{
	int		    rc;
	size_t		    count = 0;
	size_t		    saved_count;
	struct c2_indexvec *ivec;

	C2_ENTRY("struct iovec %p position %llu", iov, pos);
	C2_PRE(kcb != NULL);
	C2_PRE(iov != NULL);
	C2_PRE(seg_nr > 0);

	if (!file_to_sb(kcb->ki_filp)->csb_active) {
		C2_LEAVE();
		return -EINVAL;
	}

	rc = generic_segment_checks(iov, &seg_nr, &count, VERIFY_READ);
	if (rc != 0) {
		C2_LEAVE();
		return 0;
	}

	saved_count = count;
	rc = generic_write_checks(kcb->ki_filp, &pos, &count, 0);
	if (rc != 0 || count == 0) {
		C2_LEAVE();
		return 0;
	}

	if (count != saved_count)
		seg_nr = iov_shorten((struct iovec *)iov, seg_nr, count);

	ivec = indexvec_create(seg_nr, iov, pos);
	if (ivec == NULL) {
		C2_LEAVE();
		return 0;
	}

	C2_LOG(C2_INFO, "Write vec-count = %llu", c2_vec_count(&ivec->iv_vec));
	count = c2t1fs_aio(kcb, iov, ivec, IRT_WRITE);

	/* Updates file position. */
	kcb->ki_pos = pos + count;

	c2_free(ivec);
	C2_LEAVE();
	return count;
}

static ssize_t file_aio_read(struct kiocb	*kcb,
			     const struct iovec *iov,
			     unsigned long	 seg_nr,
			     loff_t		 pos)
{
	int		    seg;
	ssize_t		    count = 0;
	ssize_t		    res;
	struct inode	   *inode;
	struct c2_indexvec *ivec;

	C2_ENTRY("struct iovec %p position %llu", iov, pos);
	C2_PRE(kcb != NULL);
	C2_PRE(iov != NULL);
	C2_PRE(seg_nr > 0);

	/* Returns if super block is inactive. */
	if (!file_to_sb(kcb->ki_filp)->csb_active) {
		C2_LEAVE();
		return -EINVAL;
	}

	/*
	 * Checks for access privileges and adjusts all segments
	 * for proper count and total number of segments.
	 */
	res = generic_segment_checks(iov, &seg_nr, &count, VERIFY_WRITE);
	if (res != 0) {
		C2_LEAVE();
		return res;
	}

	/* Index vector has to be created before io_request is created. */
	ivec = indexvec_create(seg_nr, iov, pos);
	if (ivec == NULL) {
		C2_LEAVE();
		return 0;
	}

	/*
	 * For read IO, if any segment from index vector goes beyond EOF,
	 * they are dropped and the index vector is truncated to EOF boundary.
	 */
	inode = kcb->ki_filp->f_dentry->d_inode;
	for (seg = 0; seg < SEG_NR(ivec); ++seg) {
		if (INDEX(ivec, seg) > inode->i_size) {
			ivec->iv_vec.v_nr = seg + 1;
			break;
		}
		if (seg_endpos(ivec, seg) > inode->i_size) {
			COUNT(ivec, seg) = inode->i_size - INDEX(ivec, seg);
			ivec->iv_vec.v_nr = seg + 1;
			break;
		}
	}

	if (c2_vec_count(&ivec->iv_vec) == 0) {
		c2_free(ivec);
		return 0;
	}

	C2_LOG(C2_INFO, "Read vec-count = %llu", c2_vec_count(&ivec->iv_vec));
	count = c2t1fs_aio(kcb, iov, ivec, IRT_READ);

	/* Updates file position. */
	kcb->ki_pos = pos + count;

	c2_free(ivec);
	C2_LEAVE();
	return count;
}

const struct file_operations c2t1fs_reg_file_operations = {
	.llseek	   = generic_file_llseek,
	.aio_read  = file_aio_read,
	.aio_write = file_aio_write,
	.read	   = do_sync_read,
	.write	   = do_sync_write,
};

static int io_fops_async_submit(struct c2_io_fop      *iofop,
				struct c2_rpc_session *session)
{
	int		      rc;
	struct c2_fop_cob_rw *rwfop;

	C2_ENTRY("c2_io_fop %p c2_rpc_session %p", iofop, session);
	C2_PRE(iofop   != NULL);
	C2_PRE(session != NULL);

	rwfop = io_rw_get(&iofop->if_fop);
	C2_ASSERT(rwfop != NULL);

	rc = c2_rpc_bulk_store(&iofop->if_rbulk, session->s_conn,
			       rwfop->crw_desc.id_descs);
	if (rc != 0)
		goto out;

	iofop->if_fop.f_item.ri_session = session;
	rc = c2_rpc_post(&iofop->if_fop.f_item);
	C2_LOG(C2_INFO, "IO fops submitted to rpc, rc = %d", rc);

out:
	C2_RETURN(rc);
}

/*
 * There is no periodical rpc item pruning mechanism in rpc sessions.
 * The only time rpc items are pruned is when the rpc session is finalized.
 *
 * Hence such fops will keep occupying memory until rpc session is finalized,
 * which is done only when c2t1fs instance is unmounted.
 */
static void io_req_fop_free(struct c2_rpc_item *item)
{
	struct c2_fop	  *fop;
	struct c2_io_fop  *iofop;
	struct io_req_fop *reqfop;

	C2_ENTRY("rpc_item %p", item);
	C2_PRE(item != NULL);

	fop    = container_of(item, struct c2_fop, f_item);
	iofop  = container_of(fop, struct c2_io_fop, if_fop);
	reqfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	/* see io_req_fop_fini(). */
	io_req_fop_bob_fini(reqfop);

	c2_io_item_free(item);
	c2_free(reqfop);
	++iommstats.d_io_req_fop_nr;
}

static void io_rpc_item_cb(struct c2_rpc_item *item)
{
	struct c2_fop	  *fop;
	struct c2_io_fop  *iofop;
	struct io_req_fop *reqfop;
	struct io_request *ioreq;

	C2_ENTRY("rpc_item %p", item);
	C2_PRE(item != NULL);

	fop    = container_of(item, struct c2_fop, f_item);
	iofop  = container_of(fop, struct c2_io_fop, if_fop);
	reqfop = bob_of(iofop, struct io_req_fop, irf_iofop, &iofop_bobtype);
	ioreq  = bob_of(reqfop->irf_tioreq->ti_nwxfer, struct io_request,
			ir_nwxfer, &ioreq_bobtype);

	c2_sm_ast_post(ioreq->ir_sm.sm_grp, &reqfop->irf_ast);
	C2_LEAVE();
}

static void io_bottom_half(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct io_req_fop   *irfop;
	struct io_request   *req;
	struct target_ioreq *tioreq;

	C2_ENTRY("sm_group %p sm_ast %p", grp, ast);
	C2_PRE(grp != NULL);
	C2_PRE(ast != NULL);

	irfop  = bob_of(ast, struct io_req_fop, irf_ast, &iofop_bobtype);
	tioreq = irfop->irf_tioreq;
	req    = bob_of(tioreq->ti_nwxfer, struct io_request, ir_nwxfer,
			&ioreq_bobtype);

	C2_ASSERT(C2_IN(irfop->irf_pattr, (PA_DATA, PA_PARITY)));
	C2_ASSERT(C2_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING)));

	if (tioreq->ti_rc == 0)
		tioreq->ti_rc = irfop->irf_iofop.if_rbulk.rb_rc;

	if (irfop->irf_pattr == PA_DATA)
		tioreq->ti_databytes += irfop->irf_iofop.if_rbulk.rb_bytes;
	else
		tioreq->ti_parbytes  += irfop->irf_iofop.if_rbulk.rb_bytes;

	C2_CNT_DEC(tioreq->ti_nwxfer->nxr_iofop_nr);
	c2_atomic64_dec(&file_to_sb(req->ir_file)->csb_pending_io_nr);
	C2_LOG(C2_INFO, "Due io fops = %llu", tioreq->ti_nwxfer->nxr_iofop_nr);

	if (tioreq->ti_nwxfer->nxr_iofop_nr == 0)
		c2_sm_state_set(&req->ir_sm,
				ioreq_sm_state(req) == IRS_READING ?
				IRS_READ_COMPLETE : IRS_WRITE_COMPLETE);

	C2_LEAVE();
}

static int nw_xfer_req_dispatch(struct nw_xfer_request *xfer)
{
	int		     rc = 0;
	struct io_req_fop   *irfop;
	struct io_request   *req;
	struct target_ioreq *ti;

	C2_PRE(xfer != NULL);
	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		rc = ti->ti_ops->tio_iofops_prepare(ti, PA_DATA);
		if (rc != 0)
			C2_RETERR(rc, "data fop failed");

		rc = ti->ti_ops->tio_iofops_prepare(ti, PA_PARITY);
		if (rc != 0)
			C2_RETERR(rc, "parity fop failed");
	} c2_tl_endfor;

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		c2_tl_for(iofops, &ti->ti_iofops, irfop) {

			rc = io_fops_async_submit(&irfop->irf_iofop,
						  ti->ti_session);
			if (rc != 0)
				goto out;
			else
				c2_atomic64_inc(&file_to_sb(req->ir_file)->
						csb_pending_io_nr);
		} c2_tl_endfor;

	} c2_tl_endfor;

out:
	xfer->nxr_state = NXS_INFLIGHT;
	C2_RETURN(rc);
}

static void nw_xfer_req_complete(struct nw_xfer_request *xfer, bool rmw)
{
	struct io_request   *req;
	struct target_ioreq *ti;

	C2_ENTRY("nw_xfer_request %p, rmw %s", xfer,
		 rmw ? (char *)"true" : (char *)"false");
	C2_PRE(xfer != NULL);
	xfer->nxr_state = NXS_COMPLETE;

	req = bob_of(xfer, struct io_request, ir_nwxfer, &ioreq_bobtype);

	c2_tl_for (tioreqs, &xfer->nxr_tioreqs, ti) {
		struct io_req_fop *irfop;

		/* Maintains only the first error encountered. */
		if (xfer->nxr_rc == 0)
			xfer->nxr_rc = ti->ti_rc;

		xfer->nxr_bytes += ti->ti_databytes;
		ti->ti_databytes = 0;

		c2_tl_for(iofops, &ti->ti_iofops, irfop) {
			iofops_tlist_del(irfop);
			io_req_fop_fini(irfop);
			/*
			 * io_req_fop structures are deallocated using a
			 * rpc session method - rio_free().
			 * see io_req_fop_free().
			 */
		} c2_tl_endfor;
	} c2_tl_endfor;

	C2_LOG(C2_INFO, "Number of bytes %s = %llu",
	       ioreq_sm_state(req) == IRS_READ_COMPLETE ? "read" : "written",
	       xfer->nxr_bytes);

	if (!rmw)
		ioreq_sm_state_set(req, IRS_REQ_COMPLETE);
	else if (ioreq_sm_state(req) == IRS_READ_COMPLETE)
		xfer->nxr_bytes = 0;

	req->ir_rc = xfer->nxr_rc;
	C2_LEAVE();
}

/*
 * Used in precomputing io fop size while adding rpc bulk buffer and
 * data buffers.
 */
static inline uint32_t io_desc_size(struct c2_net_domain *ndom)
{
	return
		/* size of variables ci_nr and nbd_len */
		C2_MEMBER_SIZE(struct c2_io_indexvec, ci_nr) +
		C2_MEMBER_SIZE(struct c2_net_buf_desc, nbd_len) +
		/* size of nbd_data */
		c2_net_domain_get_max_buffer_desc_size(ndom);
}

static inline uint32_t io_seg_size(void)
{
	return sizeof(struct c2_ioseg);
}

static int bulk_buffer_add(struct io_req_fop	   *irfop,
			   struct c2_net_domain	   *dom,
			   struct c2_rpc_bulk_buf **rbuf,
			   uint32_t		   *delta,
			   uint32_t		    maxsize)
{
	int		   rc;
	int		   seg_nr;
	struct io_request *req;

	C2_ENTRY("io_req_fop %p net_domain %p delta_size %d",
		 irfop, dom, *delta);
	C2_PRE(irfop  != NULL);
	C2_PRE(dom    != NULL);
	C2_PRE(rbuf   != NULL);
	C2_PRE(delta  != NULL);
	C2_PRE(maxsize > 0);

	req    = bob_of(irfop->irf_tioreq->ti_nwxfer, struct io_request,
			ir_nwxfer, &ioreq_bobtype);
	seg_nr = min32(c2_net_domain_get_max_buffer_segments(dom),
		       SEG_NR(&irfop->irf_tioreq->ti_ivec));

	*delta += io_desc_size(dom);
	if (c2_io_fop_size_get(&irfop->irf_iofop.if_fop) + *delta < maxsize) {

		rc = c2_rpc_bulk_buf_add(&irfop->irf_iofop.if_rbulk, seg_nr,
					 dom, NULL, rbuf);
		if (rc != 0) {
			*delta -= io_desc_size(dom);
			C2_RETERR(rc, "Failed to add rpc_bulk_buffer");
		}
	}

	C2_POST(*rbuf != NULL);
	C2_RETURN(rc);
}

static int target_ioreq_iofops_prepare(struct target_ioreq *ti,
				       enum page_attr       filter)
{
	int			rc = 0;
	uint32_t		buf = 0;
	/* Number of segments in one c2_rpc_bulk_buf structure. */
	uint32_t		bbsegs;
	uint32_t		maxsize;
	uint32_t		delta = 0;
	enum page_attr		rw;
	struct io_request      *req;
	struct io_req_fop      *irfop;
	struct c2_net_domain   *ndom;
	struct c2_rpc_bulk_buf *rbuf;

	C2_ENTRY("target_ioreq %p", ti);
	C2_PRE(target_ioreq_invariant(ti));
	C2_PRE(C2_IN(filter, (PA_DATA, PA_PARITY)));

	req	= bob_of(ti->ti_nwxfer, struct io_request, ir_nwxfer,
			 &ioreq_bobtype);
	C2_ASSERT(C2_IN(ioreq_sm_state(req), (IRS_READING, IRS_WRITING)));

	ndom	= ti->ti_session->s_conn->c_rpc_machine->rm_tm.ntm_dom;
	rw      = ioreq_sm_state(req) == IRS_READING ? PA_READ : PA_WRITE;
	maxsize = c2_max_fop_size(ti->ti_session->s_conn->c_rpc_machine);

	while (buf < SEG_NR(&ti->ti_ivec)) {

		bbsegs = 0;
		if (!(ti->ti_pageattrs[buf] & filter)) {
			++buf;
			continue;
		}

		C2_ALLOC_PTR_ADDB(irfop, &c2t1fs_addb, &io_addb_loc);
		if (irfop == NULL)
			goto err;

		rc = io_req_fop_init(irfop, ti, filter);
		if (rc != 0) {
			c2_free(irfop);
			goto err;
		}
		++iommstats.a_io_req_fop_nr;

		rc = bulk_buffer_add(irfop, ndom, &rbuf, &delta, maxsize);
		if (rc != 0) {
			io_req_fop_fini(irfop);
			c2_free(irfop);
			goto err;
		}
		delta += io_seg_size();

		/*
		 * Adds io segments and io descriptor only if it fits within
		 * permitted size.
		 */
		while (buf < SEG_NR(&ti->ti_ivec) &&
		       c2_io_fop_size_get(&irfop->irf_iofop.if_fop) + delta <
		       maxsize) {

			/*
			 * Adds a page to rpc bulk buffer only if it passes
			 * through the filter.
			 */
			if (ti->ti_pageattrs[buf] & rw &&
			    ti->ti_pageattrs[buf] & filter) {
				delta += io_seg_size();

				rc = c2_rpc_bulk_buf_databuf_add(rbuf,
						ti->ti_bufvec.ov_buf[buf],
						COUNT(&ti->ti_ivec, buf),
						INDEX(&ti->ti_ivec, buf), ndom);

				if (rc == -EMSGSIZE) {

					/*
					 * Fix the number of segments in
					 * current c2_rpc_bulk_buf structure.
					 */
					rbuf->bb_nbuf->nb_buffer.ov_vec.v_nr =
						bbsegs;
					rbuf->bb_zerovec.z_bvec.ov_vec.v_nr =
						bbsegs;
					bbsegs = 0;

					delta -= io_seg_size();
					rc     = bulk_buffer_add(irfop, ndom,
							&rbuf, &delta, maxsize);
					if (rc != 0)
						goto fini_fop;

					/*
					 * Since current bulk buffer is full,
					 * new bulk buffer is added and
					 * existing segment is attempted to
					 * be added to new bulk buffer.
					 */
					continue;
				} else if (rc == 0)
					++bbsegs;
			}
			++buf;
		}

		if (c2_io_fop_byte_count(&irfop->irf_iofop) == 0) {
			irfop_fini(irfop);
			continue;
		}

		rbuf->bb_nbuf->nb_buffer.ov_vec.v_nr = bbsegs;
		rbuf->bb_zerovec.z_bvec.ov_vec.v_nr = bbsegs;

		rc = c2_io_fop_prepare(&irfop->irf_iofop.if_fop);
		if (rc != 0)
			goto fini_fop;

		io_rw_get(&irfop->irf_iofop.if_fop)->crw_fid.f_seq =
			ti->ti_fid.f_container;
		io_rw_get(&irfop->irf_iofop.if_fop)->crw_fid.f_oid =
			ti->ti_fid.f_key;

		C2_CNT_INC(ti->ti_nwxfer->nxr_iofop_nr);
		C2_LOG(C2_INFO, "Number of io fops = %llu",
		       ti->ti_nwxfer->nxr_iofop_nr);
		iofops_tlist_add(&ti->ti_iofops, irfop);
	}

	C2_RETURN(0);
fini_fop:
	irfop_fini(irfop);
err:
	c2_tlist_for (&iofops_tl, &ti->ti_iofops, irfop) {
		iofops_tlist_del(irfop);
		irfop_fini(irfop);
	} c2_tlist_endfor;

	C2_RETERR(rc, "iofops_prepare failed");
}
