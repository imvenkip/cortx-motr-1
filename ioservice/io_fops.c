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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "io_fops.h"
#include "lib/errno.h"

int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
   Reply received callback for a c2_fop_cob_readv fop.
   @pre fop.f_item.ri_type->rit_ops->rio_replied is called.

   @param - Read fop for which reply is received
 */
static void io_fop_cob_readv_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	c2_free(read_fop->frd_ioseg.fs_segs);
	c2_fop_free(fop);
}

/**
   Reply received callback for a c2_fop_cob_writev fop.
   @pre fop.f_item.ri_type->rit_ops->rio_replied is called.

   @param - Write fop for which reply is received
 */
static void io_fop_cob_writev_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	c2_free(write_fop->fwr_iovec.iov_seg);
	c2_fop_free(fop);
}

/**
   Return the size of a fop of type c2_fop_cob_readv.
   @pre fop.f_item.ri_type->rit_ops->rio_item_size is called.

   @param - Read fop for which size is to be calculated
 */
static uint64_t io_fop_cob_readv_getsize(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop;
	uint64_t			 size = 0;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	read_fop = c2_fop_data(fop);
	C2_ASSERT(read_fop != NULL);
	size += read_fop->frd_ioseg.fs_count * sizeof(struct c2_fop_segment);
	return size;
}

/**
   Return the size of a fop of type c2_fop_cob_writev.
   @pre fop.f_item.ri_type->rit_ops->rio_item_size is called.

   @param - Write fop for which size is to be calculated
 */
static uint64_t io_fop_cob_writev_getsize(struct c2_fop *fop)
{
	int				 i;
	uint64_t			 size;
	uint64_t			 vec_count;
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	write_fop = c2_fop_data(fop);
	C2_ASSERT(write_fop != NULL);
	vec_count = write_fop->fwr_iovec.iov_count;
	/* Size of actual user data. */
	for (i = 0; i < vec_count; ++i) {
		size += write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
	}
	/* Size of holding structure. */
	size += vec_count * sizeof(struct c2_fop_io_seg);

	return size;
}

/**
   Return if given 2 fops belong to same type.
   @pre fop.f_item.ri_type->rit_ops->rio_items_equal is called.

   @param - Fop1 to be compared with Fop2
   @param - Fop2 to be compared with Fop1
 */
static bool io_fop_type_equal(const struct c2_fop *fop1,
		const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return (fop1->f_type == fop2->f_type);
}

/**
   Return fid for a fop of type c2_fop_cob_readv.
   @pre fop.f_item.ri_type->rit_ops->rio_io_get_fid is called.

   @param - Read fop for which fid is to be calculated
 */
static struct c2_fop_file_fid *io_fop_get_read_fid(struct c2_fop *fop)
{
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	return &read_fop->frd_fid;
}

/**
   Return fid for a fop of type c2_fop_cob_writev.
   @pre fop.f_item.ri_type->rit_ops->rio_io_get_fid is called.

   @param - Write fop for which fid is to be calculated
 */
static struct c2_fop_file_fid *io_fop_get_write_fid(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	return &write_fop->fwr_fid;
}

/**
   Return status telling if given fop is an IO request or not.
   @pre fop.f_item.ri_type->rit_ops->rio_is_io_req is called.

   @param - fop for which rw status is to be found out
 */
static bool io_fop_is_rw(const struct c2_fop *fop)
{
	int opcode;

	C2_PRE(fop != NULL);

	opcode = fop->f_type->ft_code;
	return opcode == C2_IO_SERVICE_READV_OPCODE ||
			opcode == C2_IO_SERVICE_WRITEV_OPCODE;
}

/**
   Return the number of IO fragements(discontiguous buffers)
   for a fop of type c2_fop_cob_readv.
   @pre fop.f_item.ri_type->rit_ops->rio_get_io_fragment_count is called.

   @param - Read fop for which number of fragments is to be calculated
 */
static uint64_t io_fop_read_get_nfragments(struct c2_fop *fop)
{
	int			 i;
	int			 seg_count;
	uint64_t		 nfragments = 0;
	uint64_t		 s_offset;
	uint64_t		 s_count;
	uint64_t		 next_s_offset;
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	seg_count = read_fop->frd_ioseg.fs_count;
	for (i = 0; i < seg_count - 1; ++i) {
		s_offset = read_fop->frd_ioseg.fs_segs[i].f_offset;
		s_count = read_fop->frd_ioseg.fs_segs[i].f_count;
		next_s_offset = read_fop->frd_ioseg.fs_segs[i+1].f_offset;
		if (s_offset + s_count != next_s_offset)
			nfragments++;
	}
	return nfragments;
}

/**
   Return the number of IO fragements(discontiguous buffers)
   for a fop of type c2_fop_cob_writev.
   @pre fop.f_item.ri_type->rit_ops->rio_get_io_fragment_count is called.

   @param - Write fop for which number of fragments is to be calculated
 */
static uint64_t io_fop_write_get_nfragments(struct c2_fop *fop)
{
	int				 i;
	int				 seg_count;
	uint64_t			 nfragments = 0;
	uint64_t			 s_offset;
	uint64_t			 s_count;
	uint64_t			 next_s_offset;
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	seg_count = write_fop->fwr_iovec.iov_count;
	for (i = 0; i < seg_count - 1; ++i) {
		s_offset = write_fop->fwr_iovec.iov_seg[i].f_offset;
		s_count = write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
		next_s_offset = write_fop->fwr_iovec.iov_seg[i+1].f_offset;
		if (s_offset + s_count != next_s_offset) {
			nfragments++;
		}
	}
	return nfragments;
}

/**
   Add a new read segment to aggr_list unconditionally.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_read_segments_coalesce is called.
   @pre io_fop_read_seg_coalesce is called || io_fop_read_seg_add_cond
   is called.

   @param rs - current read segment from IO vector.
   @param offset - starting offset of current read segment from aggr_list.
   @param count - count of bytes in current read segment from aggr_list.
   @param res_segs - number of resultant segs.
   @param ns - target c2_io_read_segment structure.
 */
static int io_fop_read_seg_add(struct c2_io_read_segment *rs, uint64_t offset,
		uint64_t count, uint64_t *res_segs, struct c2_io_read_segment
		**ns)
{
	int				 rc = 0;
	struct c2_io_read_segment	*new_seg;

	C2_PRE(rs != NULL);
	C2_PRE(res_segs != NULL);
	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;
	new_seg->rs_seg.f_offset = offset;
	new_seg->rs_seg.f_count = count;
	(*res_segs)++;
	*ns = new_seg;
	return rc;
}

/**
   Add a new read segment to the aggr_list if given segment
   did not match any of the existing segments in the list.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_read_segments_coalesce is called.
   @pre io_fop_read_seg_coalesce is called.

   @param rs - current read segment from IO vector.
   @param off1 - starting offset of current read segment from aggr_list.
   @param cnt1 - count of bytes in current read segment from aggr_list.
   @param off2 - starting offset of rs.
   @param cnt2 - count of bytes in rs.
   @param res_segs - number of segments in aggr_list.
 */
static int io_fop_read_seg_add_cond(struct c2_io_read_segment *rs,
		const uint64_t off1, const uint64_t cnt1, const uint64_t off2,
		const uint64_t cnt2, uint64_t *res_segs)
{
	int				 rc = 0;
	struct c2_io_read_segment	*new_seg;

	C2_PRE(rs != NULL);
	C2_PRE(res_segs != NULL);
	C2_PRE(rs->rs_seg.f_offset == off2);
	C2_PRE(rs->rs_seg.f_count == cnt2);

	if (off1 + cnt1 < off2 || off1 < off2) {
		rc = io_fop_read_seg_add(rs, off1, cnt1, res_segs, &new_seg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&rs->rs_linkage, &new_seg->rs_linkage);
	}
	return rc;
}

/**
   If given 2 segments are adjacent, coalesce them and make a single
   segment.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_read_segments_coalesce is called.
   @pre io_fop_read_seg_coalesce is called.

   @param rs - current read segment from IO vector.
   @param off1 - starting offset of current read segment from aggr_list.
   @param cnt1 - count of bytes in current read segment from aggr_list.
   @param off2 - starting offset of rs.
   @param cnt2 - count of bytes in rs.
 */
static bool io_fop_read_segs_adjacent(struct c2_io_read_segment *rs,
		const uint64_t off1, const uint64_t cnt1, const uint64_t off2,
		const uint64_t cnt2)
{
	bool ret = false;

	C2_PRE(rs != NULL);
	C2_PRE(rs->rs_seg.f_offset == off2);
	C2_PRE(rs->rs_seg.f_count == cnt2);

	/* If off1 + count1 == off2,
	   OR off2 + count2 == off1, merge 2 segments. */
	if (off1 + cnt1 == off2) {
		rs->rs_seg.f_offset = off1;
		rs->rs_seg.f_count += cnt1;
		ret = true;
	} else if (off2 + cnt2 == off1) {
		rs->rs_seg.f_count += cnt1;
		ret = true;
	}
	return ret;
}

/**
   Given a read segment from read IO vector, see if it can be fit with
   existing set of segments in aggr_list.
   If yes, change corresponding segment from aggr_list accordingly.
   Else, add a new segment to the aggr_list.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_read_segments_coalesce is called.

   @param seg - current read segment from IO vector.
   @param aggr_list - list of read segments which gets built during
    this operation.
   @param res_segs - number of segments in aggr_list.
 */
static void io_fop_read_seg_coalesce(const struct c2_fop_segment *seg,
		struct c2_list *aggr_list, uint64_t *res_segs)
{
	int				 rc = 0;
	bool				 res;
	uint64_t			 off1;
	uint64_t			 cnt1;
	uint64_t			 off2;
	uint64_t			 cnt2;
	struct c2_io_read_segment	*new_seg;
	struct c2_io_read_segment	*read_seg;
	struct c2_io_read_segment	*read_seg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	c2_list_for_each_entry_safe(aggr_list, read_seg, read_seg_next,
			struct c2_io_read_segment, rs_linkage) {
		/* off1 and cnt1 are offset and count of incoming
		   read segment. */
		off1 = seg->f_offset;
		cnt1 = seg->f_count;

		/* off2 and cnt2 are offset and count of current segment
		   from the aggr_list. */
		off2 = read_seg->rs_seg.f_offset;
		cnt2 = read_seg->rs_seg.f_count;

		/* Check if given segments can be merged. */
		res = io_fop_read_segs_adjacent(read_seg, off1, cnt1,
				off2, cnt2);
		if (!res) {
			/* If given segment fits before some other segment
			   in increasing order of offsets, add it before
			   current segments from aggr_list. */
			io_fop_read_seg_add_cond(read_seg, off1, cnt1,
					off2, cnt2, res_segs);
		}
	}

	/* The above loop either 
	   a) Runs till end
	   b) Does not run at all (due to list being empty)
	   In either case, add the new segment to the list. */
	if (c2_list_is_empty(aggr_list)
			|| c2_list_link_is_last(&read_seg->rs_linkage,
				aggr_list)) {
		/* Add a new read segment unconditionally in aggr_list. */
		rc = io_fop_read_seg_add(read_seg, off1, cnt1,
				res_segs, &new_seg);
		if (rc < 0)
			return;
		c2_list_add_tail(aggr_list, &new_seg->rs_linkage);
	}
}

/**
   Contract the formed list of read IO segments further by merging
   adjacent segments.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_read_segments_coalesce is called.

   @param aggr_list - list of read IO segments.
   @param res_segs - number of read segments in aggr_list.
 */
static void io_fop_read_segments_contract(const struct c2_list *aggr_list,
		uint64_t *res_segs)
{
	struct c2_io_read_segment	*new_seg;
	struct c2_io_read_segment	*new_seg_next;

	c2_list_for_each_entry_safe(aggr_list, new_seg, new_seg_next,
			struct c2_io_read_segment, rs_linkage) {
                if (new_seg->rs_seg.f_offset + new_seg->rs_seg.f_count
                                == new_seg_next->rs_seg.f_offset) {
                        new_seg_next->rs_seg.f_offset = new_seg->
                                rs_seg.f_offset;
                        new_seg_next->rs_seg.f_count += new_seg->
                                rs_seg.f_count;
                        c2_list_del(&new_seg->rs_linkage);
                        c2_free(new_seg);
                        (*res_segs)--;
                }
	}
}

/**
   Coalesce the IO segments from a number of read fops to create a list
   of IO segments containing merged segments.
   @param iovec - vector of read segments.
   @param aggr_list - list of read segments which gets populated during
   this operation.
   @param res_segs - number of segments in aggr_list.

   @pre fop->f_type->ft_ops->fto_io_coalesce is called.

   @param - target iovec
   @param - aggr_list is list of read segments that could be merged
   @param - number of resultant merged segments
*/
static int io_fop_read_segments_coalesce(struct c2_fop_segment_seq *iovec,
		struct c2_list *aggr_list, uint64_t *res_segs)
{
	int	i;
	int	rc = 0;

	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < iovec->fs_count; ++i) {
		io_fop_read_seg_coalesce(&iovec->fs_segs[i], aggr_list,
				res_segs);
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
	io_fop_read_segments_contract(aggr_list, res_segs);

	return rc;
}

/**
   Coalese the IO vectors of number of read fops and put the
   collated IO vector into given resultant fop.
   @pre fop.f_item.ri_type->rit_ops->rio_io_coalesce is called.

   @param - list of read fops
   @param - resultant fop
 */
static int io_fop_read_coalesce(const struct c2_list *list,
		struct c2_fop *b_fop, union c2_io_iovec *vec)
{
	int				 res;
	uint64_t			 curr_segs = 0;
	struct c2_list			 aggr_list;
	struct c2_io_fop_member		*fop_member;
	struct c2_fop			*fop;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_segment_seq	*read_vec;
	struct c2_io_read_segment	*read_seg;
	struct c2_io_read_segment	*read_seg_next;

	C2_PRE(list != NULL);
	C2_PRE(b_fop != NULL);

	c2_list_init(&aggr_list);

	/* Traverse the list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(list, fop_member, struct c2_io_fop_member,
			fop_linkage) {
		fop = fop_member->fop;
		read_fop = c2_fop_data(fop);
		res = io_fop_read_segments_coalesce(&read_fop->frd_ioseg,
				&aggr_list, &curr_segs);
	}

	/* Allocate a new vector, copy all segments(merged) to the new
	   vector and update the read fop accordingly. */
	C2_ALLOC_PTR(read_vec);
	if (read_vec == NULL)
		return -ENOMEM;

	C2_ASSERT(curr_segs == c2_list_length(&aggr_list));
	C2_ALLOC_ARR(read_vec->fs_segs, curr_segs);
	if (read_vec->fs_segs == NULL) {
		c2_free(read_vec);
		return -ENOMEM;
	}
	read_vec->fs_count = 0;
	read_fop = c2_fop_data(b_fop);

	c2_list_for_each_entry_safe(&aggr_list, read_seg, read_seg_next,
			struct c2_io_read_segment, rs_linkage) {
		read_vec->fs_segs[read_vec->fs_count] = read_seg->rs_seg;
		read_vec->fs_count++;
		c2_list_del(&read_seg->rs_linkage);
		c2_free(read_seg);
	}

	/* Keep pointer to original IO vector to restore back on completion. */
	C2_ALLOC_PTR(vec->read_vec);
	if (vec->read_vec == NULL) {
		c2_free(read_vec->fs_segs);
		c2_free(read_vec);
		return -ENOMEM;
	}

	vec->read_vec->fs_count = read_fop->frd_ioseg.fs_count;
	vec->read_vec->fs_segs = read_fop->frd_ioseg.fs_segs;

	/* Assign new read vector to the current bound rpc item. */
	read_fop->frd_ioseg.fs_count = read_vec->fs_count;
	read_fop->frd_ioseg.fs_segs = read_vec->fs_segs;
	return res;
}

/**
   Restore the original IO vector of resultant read fop.
   @param fop - Incoming fop.
   @param vec - A union pointing to original IO vector.
 */
void io_fop_read_iovec_restore(struct c2_fop *fop, union c2_io_iovec *vec)
{
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);
	C2_PRE(vec != NULL);

	read_fop = c2_fop_data(fop);

	c2_free(read_fop->frd_ioseg.fs_segs);

	read_fop->frd_ioseg.fs_count = vec->read_vec->fs_count;
	read_fop->frd_ioseg.fs_segs = vec->read_vec->fs_segs;
}

/**
   Add a new write segment to aggr_list unconditionally.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_write_segments_coalesce is called.
   @pre io_fop_write_seg_coalesce is called || io_fop_write_seg_add_cond
   is called.

   @param ws - current write segment from IO vector.
   @param off1 - starting offset of current write segment from aggr_list.
   @param cnt1 - count of bytes in current write segment from aggr_list.
   @param off2 - starting offset of ws.
   @param cnt2 - count of bytes in ws.
 */
static int io_fop_write_seg_add(struct c2_io_write_segment *ws, uint64_t offset,
		uint32_t count, uint64_t *res_segs, struct c2_io_write_segment
		**ns)
{
	int				 rc = 0;
	struct c2_io_write_segment	*new_seg;

	C2_PRE(ws != NULL);
	C2_PRE(res_segs != NULL);
	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;

	new_seg->ws_seg.f_offset = offset;
	new_seg->ws_seg.f_buf.f_count = count;
	(*res_segs)++;
	*ns = new_seg;
	return rc;
}

/**
   Add a new write segment to the aggr_list if given segment
   did not match any of the existing segments in the list.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_write_segments_coalesce is called.
   @pre io_fop_write_seg_coalesce is called.

   @param ws - current write segment from IO vector.
   @param off1 - starting offset of current write segment from aggr_list.
   @param cnt1 - count of bytes in current write segment from aggr_list.
   @param off2 - starting offset of ws.
   @param cnt2 - count of bytes in ws.
   @param res_segs - number of segments in aggr_list.
 */
static int io_fop_write_seg_add_cond(struct c2_io_write_segment *ws,
		const uint64_t off1, const uint32_t cnt1, const uint64_t off2,
		const uint32_t cnt2, uint64_t *res_segs)
{
	int				 rc = 0;
	struct c2_io_write_segment	*new_seg;

	C2_PRE(ws != NULL);
	C2_PRE(res_segs != NULL);
	C2_PRE(ws->ws_seg.f_offset == off2);
	C2_PRE(ws->ws_seg.f_buf.f_count == cnt2);

	if (off1 + cnt1 < off2 || off1 < off2) {
		rc = io_fop_write_seg_add(ws, off1, cnt1, res_segs, &new_seg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&ws->ws_linkage, &new_seg->ws_linkage);
	}
	return rc;
}

/**
   If given 2 segments are adjacent, coalesce them and make a single
   segment.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_write_segments_coalesce is called.
   @pre io_fop_write_seg_coalesce is called.

   @param ws - current write segment from IO vector.
   @param off1 - starting offset of current write segment from aggr_list.
   @param cnt1 - count of bytes in current write segment from aggr_list.
   @param off2 - starting offset of ws.
   @param cnt2 - count of bytes in ws.
 */
static bool io_fop_write_segs_adjacent(struct c2_io_write_segment *ws,
		const uint64_t off1, const uint32_t cnt1, const uint64_t off2,
		const uint32_t cnt2)
{
	bool ret = false;

	C2_PRE(ws != NULL);
	C2_PRE(ws->ws_seg.f_offset == off2);
	C2_PRE(ws->ws_seg.f_buf.f_count == cnt2);

	/* If off1 + count1 == off2,
	   OR off2 + count2 == off1, merge 2 segments. */
	if (off1 + cnt1 == off2) {
		ws->ws_seg.f_offset = off1;
		ws->ws_seg.f_buf.f_count += cnt1;
		ret = true;
	} else if (off2 + cnt2 == off1) {
		ws->ws_seg.f_buf.f_count += cnt1;
		ret = true;
	}
	return ret;
}

/**
   Given a write segment from write IO vector, see if it can be fit with
   existing set of segments in aggr_list.
   If yes, change corresponding segment from aggr_list accordingly.
   Else, add a new segment to the aggr_list.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_write_segments_coalesce is called.

   @param seg - current write segment from IO vector.
   @param aggr_list - list of write segments which gets built during
    this operation.
   @param res_segs - number of segments in aggr_list.
 */
static void io_fop_write_seg_coalesce(const struct c2_fop_io_seg *seg,
		struct c2_list *aggr_list, uint64_t *res_segs)
{
	int				 rc = 0;
	bool				 res;
	uint64_t			 off1;
	uint32_t			 cnt1;
	uint64_t			 off2;
	uint32_t			 cnt2;
	struct c2_io_write_segment	*new_seg;
	struct c2_io_write_segment	*write_seg;
	struct c2_io_write_segment	*write_seg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	c2_list_for_each_entry_safe(aggr_list, write_seg, write_seg_next,
			struct c2_io_write_segment, ws_linkage) {
		/* off1 and cnt1 are offset and count of incoming
		   write segment. */
		off1 = seg->f_offset;
		cnt1 = seg->f_buf.f_count;

		/* off2 and cnt2 are offset and count of current segment
		   from the aggr_list. */
		off2 = write_seg->ws_seg.f_offset;
		cnt2 = write_seg->ws_seg.f_buf.f_count;

		/* Check if given segments can be merged. */
		res = io_fop_write_segs_adjacent(write_seg, off1, cnt1,
				off2, cnt2);
		if (!res) {
			/* If given segment fits before some other segment
			   in increasing order of offsets, add it before
			   current segments from aggr_list. */
			io_fop_write_seg_add_cond(write_seg, off1, cnt1,
					off2, cnt2, res_segs);
		}
	}

	/* The above loop either 
	   a) Runs till end
	   b) Does not run at all (due to list being empty)
	   In either case, add the new segment to the list. */
	if (c2_list_is_empty(aggr_list) || c2_list_link_is_last(
				&write_seg->ws_linkage, aggr_list)) {
		/* Add a new write segment unconditionally in aggr_list. */
		rc = io_fop_write_seg_add(write_seg, off1, cnt1,
				res_segs, &new_seg);
		if (rc < 0)
			return;
		c2_list_add_tail(aggr_list, &new_seg->ws_linkage);
	}
}

/**
   Contract the formed list of write IO segments further by merging
   adjacent segments.
   @pre fop->f_type->ft_ops->fto_io_coalesce is called.
   @pre io_fop_write_segments_coalesce is called.

   @param aggr_list - list of write IO segments.
   @param res_segs - number of write segments in aggr_list.
 */
static void io_fop_write_segments_contract(const struct c2_list *aggr_list,
		uint64_t *res_segs)
{
	struct c2_io_write_segment	*new_seg;
	struct c2_io_write_segment	*new_seg_next;

	c2_list_for_each_entry_safe(aggr_list, new_seg, new_seg_next,
			struct c2_io_write_segment, ws_linkage) {
                if ((new_seg->ws_seg.f_offset + new_seg->ws_seg.f_buf.f_count)
                                == new_seg_next->ws_seg.f_offset) {
                        new_seg_next->ws_seg.f_offset = new_seg->
                                ws_seg.f_offset;
                        new_seg_next->ws_seg.f_buf.f_count += new_seg->
                                ws_seg.f_buf.f_count;
                        c2_list_del(&new_seg->ws_linkage);
                        c2_free(new_seg);
                        (*res_segs)--;
                }
	}
}

/**
   Coalesce the IO segments from a number of write fops to create a list
   of IO segments containing merged segments.
   @param iovec - vector of write segments.
   @param aggr_list - list of write segments which gets populated during
   this operation.
   @param res_segs - number of segments in aggr_list.

   @pre fop->f_type->ft_ops->fto_io_coalesce is called.

   @param - target iovec
   @param - aggr_list is list of write segments that could be merged
   @param - number of resultant merged segments
*/
static int io_fop_write_segments_coalesce(struct c2_fop_io_vec *iovec,
		struct c2_list *aggr_list, uint64_t *res_segs)
{
	int	i;
	int	rc = 0;

	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < iovec->iov_count; ++i) {
		io_fop_write_seg_coalesce(&iovec->iov_seg[i], aggr_list,
				res_segs);
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
	io_fop_write_segments_contract(aggr_list, res_segs);

	return rc;
}

/**
   Coalesce the IO vectors of a list of write fops into IO vector
   of given resultant fop.
   @pre fop.f_item.ri_type->rit_ops->rio_io_coalesce is called.

   @param - list of write fops
   @param - resultant fop
 */
static int io_fop_write_coalesce(const struct c2_list *list,
		struct c2_fop *b_fop, union c2_io_iovec *vec)
{
	int				 res;
	uint64_t			 curr_segs = 0;
	struct c2_list			 aggr_list;
	struct c2_io_fop_member		*fop_member;
	struct c2_fop			*fop;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_io_vec		*write_vec;
	struct c2_io_write_segment	*write_seg;
	struct c2_io_write_segment	*write_seg_next;

	C2_PRE(list != NULL);
	C2_PRE(b_fop != NULL);

	c2_list_init(&aggr_list);

	/* Traverse the list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(list, fop_member, struct c2_io_fop_member,
			fop_linkage) {
		fop = fop_member->fop;
		write_fop = c2_fop_data(fop);
		res = io_fop_write_segments_coalesce(&write_fop->fwr_iovec,
				&aggr_list, &curr_segs);
	}

	/* Allocate a new write vector and copy all (merged) write segments
	   to the new vector and make changes to write fop accordingly. */
	C2_ALLOC_PTR(write_vec);
	if (write_vec == NULL)
		return -ENOMEM;

	C2_ASSERT(curr_segs == c2_list_length(&aggr_list));
	C2_ALLOC_ARR(write_vec->iov_seg, curr_segs);
	if (write_vec->iov_seg == NULL) {
		c2_free(write_vec);
		return -ENOMEM;
	}
	write_vec->iov_count = 0;
	write_fop = c2_fop_data(b_fop);

	c2_list_for_each_entry_safe(&aggr_list, write_seg,
			write_seg_next, struct c2_io_write_segment,
			ws_linkage) {
		write_vec->iov_seg[write_vec->iov_count] = write_seg->ws_seg;
		write_vec->iov_count++;
		c2_list_del(&write_seg->ws_linkage);
		c2_free(write_seg);
	}

	/* Keep pointer to original IO vector to restore back on completion. */
	C2_ALLOC_PTR(vec->write_vec);
	if (vec->write_vec == NULL) {
		c2_free(write_vec->iov_seg);
		c2_free(write_vec);
		return -ENOMEM;
	}

	vec->write_vec->iov_count = write_fop->fwr_iovec.iov_count;
	vec->write_vec->iov_seg = write_fop->fwr_iovec.iov_seg;

	/* Assign new write vector to the current bound rpc item. */
	write_fop->fwr_iovec.iov_count = write_vec->iov_count;
	write_fop->fwr_iovec.iov_seg = write_vec->iov_seg;
	return 0;
}

/**
   Restore the original IO vector of resultant write fop.
   @param fop - Incoming fop.
   @param vec - A union pointing to original IO vector.
 */
static void io_fop_write_iovec_restore(struct c2_fop *fop, union c2_io_iovec *vec)
{
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);
	C2_PRE(vec != NULL);

	write_fop = c2_fop_data(fop);

	c2_free(write_fop->fwr_iovec.iov_seg);

	write_fop->fwr_iovec.iov_count = vec->write_vec->iov_count;
	write_fop->fwr_iovec.iov_seg = vec->write_vec->iov_seg;
}

/**
   Return the opcode of given fop.
   @pre fop.f_item.ri_type->rit_ops->rio_io_get_opcode is called.

   @param - fop for which opcode has to be returned
 */
static int io_fop_get_opcode(const struct c2_fop *fop)
{
	int opcode;

	C2_PRE(fop != NULL);

	opcode = fop->f_type->ft_code;
	return opcode;
}

/**
 * readv FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = io_fop_cob_readv_replied,
	.fto_size_get = io_fop_cob_readv_getsize,
	.fto_op_equal = io_fop_type_equal,
	.fto_get_opcode = io_fop_get_opcode,
	.fto_get_fid = io_fop_get_read_fid,
	.fto_is_io = io_fop_is_rw,
	.fto_get_nfragments = io_fop_read_get_nfragments,
	.fto_io_coalesce = io_fop_read_coalesce,
	.fto_iovec_restore = io_fop_read_iovec_restore,
};

/**
 * writev FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = io_fop_cob_writev_replied,
	.fto_size_get = io_fop_cob_writev_getsize,
	.fto_op_equal = io_fop_type_equal,
	.fto_get_opcode = io_fop_get_opcode,
	.fto_get_fid = io_fop_get_write_fid,
	.fto_is_io = io_fop_is_rw,
	.fto_get_nfragments = io_fop_write_get_nfragments,
	.fto_io_coalesce = io_fop_write_coalesce,
	.fto_iovec_restore = io_fop_write_iovec_restore,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 * @param fop - fop on which this fom_init methods operates.
 * @param fom - fom object to be created here.
 */
static int io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
   Return the size of a read_reply fop.
   @pre fop.f_item.ri_type->rit_ops->rio_item_size is called.

   @param - Read fop for which size has to be calculated
 */
static uint64_t io_fop_cob_readv_rep_getsize(struct c2_fop *fop)
{
	uint64_t			 size;
	struct c2_fop_cob_readv_rep	*read_rep_fop;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	/* XXX Later 2 separate function are needed for read and write reply.*/
	if (fop->f_type->ft_code != C2_IO_SERVICE_READV_REP_OPCODE) {
		return size;
	}
	/* Add buffer payload for read reply */
	read_rep_fop = c2_fop_data(fop);
	/* Size of actual user data. */
	size += read_rep_fop->frdr_buf.f_count;
	return size;
}

/**
 * readv and writev reply FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = io_fop_cob_rwv_rep_fom_init,
	.fto_size_get = io_fop_cob_readv_rep_getsize,
};

int c2_io_fop_file_create_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
   Return size for a fop of type c2_fop_file_create;
   @param - Create fop for which size has to be calculated
 */
static uint64_t io_fop_create_getsize(struct c2_fop *fop)
{
	uint64_t size;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);
	return size;
}

/* Ops vector for file create request. */
const struct c2_fop_type_ops c2_fop_file_create_ops = {
	.fto_fom_init = c2_io_fop_file_create_fom_init,
	.fto_fop_replied = NULL,
	.fto_size_get = io_fop_create_getsize,
	.fto_op_equal = io_fop_type_equal,
	.fto_get_opcode = io_fop_get_opcode,
	.fto_get_fid = NULL,
	.fto_is_io = io_fop_is_rw,
	.fto_get_nfragments = NULL,
	.fto_io_coalesce = NULL,
};

/* Dummy init */
static int io_fop_file_create_rep_fom_init(struct c2_fop *fop,
		struct c2_fom **m)
{
	return 0;
}

/* Ops vector for file create request. */
const struct c2_fop_type_ops c2_fop_file_create_rep_ops = {
        .fto_fom_init = io_fop_file_create_rep_fom_init,
        .fto_fop_replied = NULL,
        .fto_size_get = io_fop_create_getsize,
        .fto_op_equal = io_fop_type_equal,
        .fto_get_opcode = io_fop_get_opcode,
        .fto_get_fid = NULL,
        .fto_is_io = io_fop_is_rw,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
};

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request",
		C2_IO_SERVICE_READV_OPCODE, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request",
		C2_IO_SERVICE_WRITEV_OPCODE, &c2_io_cob_writev_ops);
C2_FOP_TYPE_DECLARE(c2_fop_file_create, "File Create",
		C2_IO_SERVICE_CREATE_OPCODE, &c2_fop_file_create_ops);
C2_FOP_TYPE_DECLARE(c2_fop_file_create_rep, "File Create Reply",
		C2_IO_SERVICE_CREATE_REP_OPCODE, &c2_fop_file_create_rep_ops);

/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply",
		    C2_IO_SERVICE_WRITEV_REP_OPCODE, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply",
		    C2_IO_SERVICE_READV_REP_OPCODE, &c2_io_rwv_rep_ops);

#ifdef __KERNEL__

/** Placeholder API for c2t1fs build. */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

int c2_io_fop_file_create_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
