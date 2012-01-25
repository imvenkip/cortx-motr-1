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
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ioservice/io_fops.h"

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "xcode/bufvec_xcode.h" /* c2_xcode_fop_size_get() */
#include "fop/fop_format_def.h"
#include "ioservice/io_fops.ff"
#include "rpc/rpc_base.h"
#include "lib/vec.h"	/* c2_0vec */
#include "rpc/rpc_opcodes.h"

extern const struct c2_rpc_item_ops      rpc_item_iov_ops;
extern const struct c2_rpc_item_type_ops rpc_item_iov_type_ops;

/**
   The IO fops code has been generalized to suit both read and write fops
   as well as the kernel implementation.
   The fop for read and write is same.
   Most of the code deals with IO coalescing and fop type ops.
   Ioservice also registers IO fops. This initialization should be done
   explicitly while using code is user mode while kernel module takes care
   of this initialization by itself.
   Most of the IO coalescing is done from client side. RPC layer, typically
   formation module invokes the IO coalescing code.
 */

/**
   A generic IO segment pointing either to read or write segments. This
   is needed to have generic IO coalescing code. During coalescing, lot
   of new io segments are created which need to be tracked using a list.
   This is where the generic io segment is used.
 */
struct c2_io_ioseg {
	/** IO segment for read or write request fop. */
	struct c2_fop_io_seg	*rw_seg;
        /** Linkage to the list of such structures. */
        struct c2_list_link	 io_linkage;
};

bool is_read(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_fopt;
}

bool is_write(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_fopt;
}

bool is_io(const struct c2_fop *fop)
{
	return is_read(fop) || is_write(fop);
}

bool is_read_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_rep_fopt;
}

bool is_write_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_rep_fopt;
}

bool is_io_rep(const struct c2_fop *fop)
{
	return is_read_rep(fop) || is_write_rep(fop);
}

/**
   Allocates the array of IO segments from IO vector.
   @retval - 0 if succeeded, negative error code otherwise.
 */
static int iosegs_alloc(struct c2_fop_io_vec *iovec, const uint32_t count)
{
	C2_PRE(iovec != NULL);
	C2_PRE(count != 0);

	C2_ALLOC_ARR(iovec->iv_segs, count);
	return iovec->iv_segs == NULL ? 0 : -ENOMEM;
}

struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv  *rfop;
	struct c2_fop_cob_writev *wfop;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	if (is_read(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rwv;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rwv;
	}
}

struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv_rep	*rfop;
	struct c2_fop_cob_writev_rep	*wfop;

	C2_PRE(fop != NULL);
	C2_PRE(is_io_rep(fop));

	if (is_read_rep(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rep;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rep;
	}
}

/**
   Returns IO vector from given IO fop.
 */
static struct c2_fop_io_vec *iovec_get(struct c2_fop *fop)
{
	return &io_rw_get(fop)->crw_iovec;
}

/**
   Deallocates and removes a generic IO segment from aggr_list.
 */
static void ioseg_unlink_free(struct c2_io_ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);

	c2_list_del(&ioseg->io_linkage);
	c2_free(ioseg);
}

/**
   Returns if given 2 fops belong to same type.
 */
static bool io_fop_type_equal(const struct c2_fop *fop1,
		const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

/**
   Returns the number of IO fragements (discontiguous buffers)
   for a fop of type read or write.
 */
static uint64_t io_fop_fragments_nr_get(struct c2_fop *fop)
{
	uint32_t	      i;
	uint64_t	      frag_nr = 1;
	struct c2_fop_io_vec *iovec;

	C2_PRE(fop != NULL);
	C2_PRE(is_io(fop));

	iovec = iovec_get(fop);
	for (i = 0; i < iovec->iv_count - 1; ++i)
		if (iovec->iv_segs[i].is_offset +
		    iovec->iv_segs[i].is_buf.ib_count !=
		    iovec->iv_segs[i+1].is_offset)
			frag_nr++;
	return frag_nr;
}

/**
   Allocates a new generic IO segment
 */
static int io_fop_seg_init(struct c2_io_ioseg **ns, struct c2_io_ioseg *cseg)
{
	struct c2_io_ioseg *new_seg;

	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;
	C2_ALLOC_PTR(new_seg->rw_seg);
	if (new_seg->rw_seg == NULL) {
		c2_free(new_seg);
		return -ENOMEM;
	}
	*ns = new_seg;
	*new_seg = *cseg;
	return 0;
}

/**
   Adds a new IO segment to the aggr_list conditionally.
 */
static int io_fop_seg_add_cond(struct c2_io_ioseg *cseg,
		struct c2_io_ioseg *nseg)
{
	int			 rc;
	struct c2_io_ioseg	*new_seg;

	C2_PRE(cseg != NULL);
	C2_PRE(nseg != NULL);

	if (nseg->rw_seg->is_offset < cseg->rw_seg->is_offset) {
		rc = io_fop_seg_init(&new_seg, nseg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&cseg->io_linkage, &new_seg->io_linkage);
	} else
		rc = -EINVAL;

	return rc;
}

/**
   Checks if input IO segment from IO vector can fit with existing set of
   segments in aggr_list.
   If yes, change corresponding segment from aggr_list accordingly.
   The segment is added in a sorted manner of starting offset in aggr_list.
   Else, add a new segment to the aggr_list.
   @note This is a best-case effort or an optimization effort. That is why
   return value is void. If something fails, everything is undone and function
   returns.

   @param aggr_list - list of write segments which gets built during
    this operation.
 */
static void io_fop_seg_coalesce(struct c2_io_ioseg *seg,
		struct c2_list *aggr_list)
{
	int			 rc;
	bool			 added = false;
	struct c2_io_ioseg	*new_seg;
	struct c2_io_ioseg	*ioseg;
	struct c2_io_ioseg	*ioseg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);

	c2_list_for_each_entry_safe(aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		/* If given segment fits before some other segment
		   in increasing order of offsets, add it before
		   current segments from aggr_list. */
		rc = io_fop_seg_add_cond(ioseg, seg);
		if (rc == -ENOMEM)
			return;
		if (rc == 0) {
			added = true;
			break;
		}
	}

	/* Add a new IO segment unconditionally in aggr_list. */
	if (!added) {
		rc = io_fop_seg_init(&new_seg, seg);
		if (rc < 0)
			return;
		c2_list_add_tail(aggr_list, &new_seg->io_linkage);
	}
}

/**
   Coalesces the IO segments from a number of IO fops to create a list
   of IO segments containing merged segments.
   @param aggr_list - list of IO segments which gets populated during
   this operation.
*/
static int io_fop_segments_coalesce(struct c2_fop_io_vec *iovec,
		struct c2_list *aggr_list)
{
	uint32_t		i;
	int			rc = 0;
	uint32_t		segs_nr;
	struct c2_io_ioseg	ioseg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_list != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	segs_nr = iovec->iv_count;
	for (i = 0; i < segs_nr; ++i) {
		ioseg.rw_seg = &iovec->iv_segs[i];
		io_fop_seg_coalesce(&ioseg, aggr_list);
	}

	return rc;
}

/**
   Coalesces the IO vectors of a list of read/write fops into IO vector
   of given resultant fop. At a time, all fops in the list are either
   read fops or write fops. Both fop types can not be present simultaneously.

   @param fop_list - list of fops. These structures contain either read or
   write fops. Both fop types can not be present in the fop_list simultaneously.
   @param res_fop - resultant fop with which the resulting IO vector is
   associated.
   @param bkpfop - A fop used to store the original IO vector of res_fop
   whose IO vector is replaced by the coalesced IO vector.
   from resultant fop and it is restored on receving the reply of this
   coalesced IO request. @see io_fop_iovec_restore.
 */
static int io_fop_coalesce(const struct c2_list *fop_list,
		struct c2_fop *res_fop, struct c2_fop *bkp_fop)
{
	int			 res;
	int			 i = 0;
	uint64_t		 curr_segs;
	struct c2_fop		*fop;
	struct c2_list		 aggr_list;
	struct c2_io_ioseg	*ioseg;
	struct c2_io_ioseg	*ioseg_next;
	struct c2_io_ioseg	 res_ioseg;
	struct c2_fop_type	*fopt;
	struct c2_fop_io_vec	*iovec;
	struct c2_fop_io_vec	*bkp_vec;
	struct c2_fop_io_vec	*res_iovec;

	C2_PRE(fop_list != NULL);
	C2_PRE(res_fop != NULL);

	fopt = res_fop->f_type;
	C2_PRE(is_io(res_fop));

        /* Make a copy of original IO vector belonging to res_fop and place
           it in input parameter vec which can be used while restoring the
           IO vector. */
	bkp_fop = c2_fop_alloc(fopt, NULL);
	if (bkp_fop == NULL)
		return -ENOMEM;

	c2_list_init(&aggr_list);

	/* Traverse the fop_list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(fop_list, fop, struct c2_fop, f_link) {
		iovec = iovec_get(fop);
		res = io_fop_segments_coalesce(iovec, &aggr_list);
	}

	/* Allocate a new generic IO vector and copy all (merged) IO segments
	   to the new vector and make changes to res_fop accordingly. */
	C2_ALLOC_PTR(res_iovec);
	if (res_iovec == NULL) {
		res = -ENOMEM;
		goto cleanup;
	}

	curr_segs = c2_list_length(&aggr_list);
	res = iosegs_alloc(res_iovec, curr_segs);
	if (res != 0)
		goto cleanup;
	res_iovec->iv_count = curr_segs;

	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		res_ioseg.rw_seg = &res_iovec->iv_segs[i];
		*res_ioseg.rw_seg = *ioseg->rw_seg;
		ioseg_unlink_free(ioseg);
		i++;
	}
	c2_list_fini(&aggr_list);
	res_iovec->iv_count = i;

	iovec = iovec_get(res_fop);
	bkp_vec = iovec_get(bkp_fop);
	*bkp_vec = *iovec;
	*iovec = *res_iovec;
	return res;
cleanup:
	C2_ASSERT(res != 0);
	if (res_iovec != NULL)
		c2_free(res_iovec);
	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
				    struct c2_io_ioseg, io_linkage)
		ioseg_unlink_free(ioseg);
	c2_list_fini(&aggr_list);
	return res;
}

/**
   Restores the original IO vector of parameter fop from the appropriate
   IO vector from parameter bkpfop.
   @param fop - Incoming fop. This fop is same as res_fop parameter from
   the subroutine io_fop_coalesce. @see io_fop_coalesce.
   @param bkpfop - Backup fop with which the original IO vector of
   coalesced fop was stored.
 */
static void io_fop_iovec_restore(struct c2_fop *fop, struct c2_fop *bkpfop)
{
	struct c2_fop_io_vec *vec;

	C2_PRE(fop != NULL);
	C2_PRE(bkpfop != NULL);

	vec = iovec_get(fop);
	c2_free(vec->iv_segs);
	*vec = *(iovec_get(bkpfop));
	c2_fop_free(bkpfop);
}

/**
   Returns the fid of given IO fop.
   @note This method only works for read and write IO fops.
   @retval On-wire fid of given fop.
 */
static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop)
{
	return &(io_rw_get(fop))->crw_fid;
}

/**
   Returns if given 2 fops refer to same fid. The fids mentioned here
   are on-wire fids.
   @retval true if both fops refer to same fid, false otherwise.
 */
static bool io_fop_fid_equal(struct c2_fop *fop1, struct c2_fop *fop2)
{
	struct c2_fop_file_fid *ffid1;
	struct c2_fop_file_fid *ffid2;

	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	ffid1 = io_fop_fid_get(fop1);
	ffid2 = io_fop_fid_get(fop2);

	return (ffid1->f_seq == ffid2->f_seq && ffid1->f_oid == ffid2->f_oid);
}

/**
 * readv FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_op_equal = io_fop_type_equal,
	.fto_fid_equal = io_fop_fid_equal,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
};

/**
 * writev FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fop_replied = NULL,
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_op_equal = io_fop_type_equal,
	.fto_fid_equal = io_fop_fid_equal,
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
};

/**
 * readv and writev reply FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_size_get = c2_xcode_fop_size_get
};

/**
 * FOP definitions for readv and writev operations.
 */

C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_readv, "Read request", &c2_io_cob_readv_ops,
			C2_IOSERVICE_READV_OPCODE, C2_RPC_ITEM_TYPE_REQUEST,
			&rpc_item_iov_type_ops);
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_writev, "Write request",
			&c2_io_cob_writev_ops,
			C2_IOSERVICE_WRITEV_OPCODE, C2_RPC_ITEM_TYPE_REQUEST,
			&rpc_item_iov_type_ops);

/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_writev_rep, "Write reply",
			&c2_io_rwv_rep_ops, C2_IOSERVICE_WRITEV_REP_OPCODE,
			C2_RPC_ITEM_TYPE_REPLY, &rpc_item_iov_type_ops);
C2_FOP_TYPE_DECLARE_OPS(c2_fop_cob_readv_rep, "Read reply",
			&c2_io_rwv_rep_ops, C2_IOSERVICE_READV_REP_OPCODE,
			C2_RPC_ITEM_TYPE_REPLY,  &rpc_item_iov_type_ops);

static struct c2_fop_type_format *ioservice_fmts[] = {
	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_rw_tfmt,
	&c2_fop_cob_rw_reply_tfmt,
};

static struct c2_fop_type *ioservice_fops[] = {
	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_cob_readv_rep_fopt,
	&c2_fop_cob_writev_rep_fopt,
};

int c2_ioservice_fops_nr(void)
{
	return ARRAY_SIZE(ioservice_fops);
}
C2_EXPORTED(c2_ioservice_fops_nr);

void c2_ioservice_fop_fini(void)
{
	c2_fop_type_fini_nr(ioservice_fops, ARRAY_SIZE(ioservice_fops));
	c2_fop_type_format_fini_nr(ioservice_fmts, ARRAY_SIZE(ioservice_fmts));
}
C2_EXPORTED(c2_ioservice_fop_fini);

int c2_ioservice_fop_init(void)
{
	int rc;

	rc = c2_fop_type_format_parse_nr(ioservice_fmts,
			ARRAY_SIZE(ioservice_fmts));
	if (rc == 0)
		rc = c2_fop_type_build_nr(ioservice_fops,
				ARRAY_SIZE(ioservice_fops));
	if (rc != 0)
		c2_ioservice_fop_fini();
	return rc;
}
C2_EXPORTED(c2_ioservice_fop_init);

struct io_zeroseg *io_zeroseg_alloc(void)
{
	struct io_zeroseg *zseg;

	C2_ALLOC_PTR(zseg);
	if (zseg == NULL)
		return NULL;

	c2_list_link_init(&zseg->is_linkage);
	return zseg;
}

void io_zeroseg_free(struct io_zeroseg *zseg)
{
	C2_PRE(zseg != NULL);

	c2_list_link_fini(&zseg->is_linkage);
	c2_free(zseg);
}

void io_zerovec_seg_get(const struct c2_0vec *zvec, uint32_t seg_index,
			struct io_zeroseg *seg)
{
	C2_PRE(seg != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);

	seg->is_off = zvec->z_index[seg_index];
	seg->is_count = zvec->z_bvec.ov_vec.v_count[seg_index];
	seg->is_buf = zvec->z_bvec.ov_buf[seg_index];
}

void io_zerovec_seg_set(struct c2_0vec *zvec, uint32_t seg_index,
			const struct io_zeroseg *seg)
{
	C2_PRE(seg != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);

	zvec->z_bvec.ov_buf[seg_index] = seg->is_buf;
	zvec->z_index[seg_index] = seg->is_off;
	zvec->z_bvec.ov_vec.v_count[seg_index] = seg->is_count;
}

int io_zerovec_segs_alloc(struct c2_0vec *zvec, uint32_t segs_nr)
{
	C2_PRE(zvec != NULL);
	C2_PRE(segs_nr != 0);

	C2_ALLOC_ARR(zvec->z_bvec.ov_buf, segs_nr);
	return zvec->z_bvec.ov_buf == NULL ? -ENOMEM : 0;
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
