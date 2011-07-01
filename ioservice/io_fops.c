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
   @param - Read fop for which reply is received
 */
int c2_io_fop_cob_readv_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop = NULL;

	C2_PRE(fop != NULL);
	read_fop = c2_fop_data(fop);
	c2_free(read_fop->frd_ioseg.fs_segs);
	c2_fop_free(fop);
	return 0;
}

/**
   Reply received callback for a c2_fop_cob_writev fop.
   @param - Write fop for which reply is received
 */
int c2_io_fop_cob_writev_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop = NULL;

	C2_PRE(fop != NULL);
	write_fop = c2_fop_data(fop);
	c2_free(write_fop->fwr_iovec.iov_seg);
	c2_fop_free(fop);
	return 0;
}

/**
   Return the size of a fop of type c2_fop_cob_readv.
   @param - Read fop for which size is to be calculated 
 */
uint64_t c2_io_fop_cob_readv_getsize(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop = NULL;
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
   @param - Write fop for which size is to be calculated 
 */
uint64_t c2_io_fop_cob_writev_getsize(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop = NULL;
	uint64_t			 size = 0;
	uint64_t			 vec_count = 0;
	int				 i = 0;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	write_fop = c2_fop_data(fop);
	C2_ASSERT(write_fop != NULL);
	vec_count = write_fop->fwr_iovec.iov_count;
	/* Size of actual user data. */
	for(i = 0; i < vec_count; i++) {
		size += write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
	}
	/* Size of holding structure. */
	size += write_fop->fwr_iovec.iov_count * sizeof(struct c2_fop_io_seg);

	return size;
}

/**
   Return if given 2 fops belong to same type.
   @param - Fop1 to be compared with Fop2
   @param - Fop2 to be compared with Fop1 
 */
bool c2_io_fop_type_equal(struct c2_fop *fop1, struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	if (fop1->f_type->ft_code == fop2->f_type->ft_code) {
		return true;
	} else {
		return false;
	}
}

/**
   Return fid for a fop of type c2_fop_cob_readv.
   @param - Read fop for which fid is to be calculated 
 */
struct c2_fop_file_fid c2_io_fop_get_read_fid(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop = NULL;
	struct c2_fop_file_fid		 ffid;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	ffid = read_fop->frd_fid;
	return ffid;
}

/**
   Return fid for a fop of type c2_fop_cob_writev.
   @param - Write fop for which fid is to be calculated 
 */
struct c2_fop_file_fid c2_io_fop_get_write_fid(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_file_fid		 ffid;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	ffid = write_fop->fwr_fid;
	return ffid;
}

/**
   Return status telling if given fop is an IO request or not.
   @param - fop for which rw status is to be found out 
 */
bool c2_io_fop_is_rw(struct c2_fop *fop)
{
	int	opcode = 0;

	C2_PRE(fop != NULL);

	opcode = fop->f_type->ft_code;
	if ((opcode == c2_io_service_readv_opcode) ||
			(opcode == c2_io_service_writev_opcode)) {
		return true;
	}
	return false;
}

/**
   Return the number of IO fragements(discontiguous buffers)
   for a fop of type c2_fop_cob_readv.
   @param - Read fop for which number of fragments is to be calculated 
 */
uint64_t c2_io_fop_read_get_nfragments(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop;
	uint64_t			 nfragments = 0;
	uint64_t			 s_offset = 0;
	uint64_t			 s_count = 0;
	uint64_t			 next_s_offset = 0;
	int				 i = 0;
	int				 seg_count = 0;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	seg_count = read_fop->frd_ioseg.fs_count;
	for (i = 0; i < seg_count - 1; i++) {
		s_offset = read_fop->frd_ioseg.fs_segs[i].f_offset;
		s_count = read_fop->frd_ioseg.fs_segs[i].f_count;
		next_s_offset = read_fop->frd_ioseg.fs_segs[i+1].f_offset;
		if ((s_offset + s_count) != next_s_offset) {
			nfragments++;
		}
	}
	return nfragments;
}

/**
   Return the number of IO fragements(discontiguous buffers)
   for a fop of type c2_fop_cob_writev.
   @param - Write fop for which number of fragments is to be calculated 
 */
uint64_t c2_io_fop_write_get_nfragments(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop;
	uint64_t			 nfragments = 0;
	int				 seg_count = 0;
	uint64_t			 s_offset = 0;
	uint64_t			 s_count = 0;
	uint64_t			 next_s_offset = 0;
	int				 i = 0;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	seg_count = write_fop->fwr_iovec.iov_count;
	for (i = 0; i < seg_count - 1; i++) {
		s_offset = write_fop->fwr_iovec.iov_seg[i].f_offset;
		s_count = write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
		next_s_offset = write_fop->fwr_iovec.iov_seg[i+1].f_offset;
		if ((s_offset + s_count) != next_s_offset) {
			nfragments++;
		}
	}
	return nfragments;
}

/**
   Coalesce the IO segments from a number of read fops to create a list
   of IO segments containing merged segments.
   @param - target iovec 
   @param - aggr_list is list of read segments that could be merged
   @param - number of resultant merged segments 
*/
int c2_io_fop_read_segments_coalesce(void *vec,
		struct c2_list *aggr_list, uint64_t *res_segs)
{
	int						 i = 0;
	struct c2_io_read_segment			*read_seg = NULL;
	struct c2_io_read_segment			*read_seg_next = NULL;
	struct c2_io_read_segment			*new_seg = NULL;
	struct c2_io_read_segment			*new_seg_next = NULL;
	struct c2_fop_segment_seq			*iovec = NULL;
	bool						 list_empty = true;

	C2_PRE(vec != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	iovec = (struct c2_fop_segment_seq*)vec;

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < iovec->fs_count; i++) {
		c2_list_for_each_entry_safe(aggr_list, read_seg,
				read_seg_next,
				struct c2_io_read_segment, rs_linkage) {
			list_empty = false;
			/* If off1 + count1 = off2, then merge two segments.*/
			if ((iovec->fs_segs[i].f_offset +
					iovec->fs_segs[i].f_count) ==
					read_seg->rs_seg.f_offset) {
				read_seg->rs_seg.f_offset =
					iovec->fs_segs[i].f_offset;
				read_seg->rs_seg.f_count +=
					iovec->fs_segs[i].f_count;
				break;
			} else if ((read_seg->rs_seg.f_offset +
					read_seg->rs_seg.f_count) ==
					iovec->fs_segs[i].f_offset) {
				/* If off2 + count2 = off1, then merge
				   two segments.*/
				read_seg->rs_seg.f_count +=
					iovec->fs_segs[i].f_count;
				break;
			} else if ( ((iovec->fs_segs[i].f_offset +
					iovec->fs_segs[i].f_count) <
					read_seg->rs_seg.f_offset) ||
					((iovec->fs_segs[i].f_offset <
					  read_seg-> rs_seg.f_offset)) ) {
				/* If (off1 + count1) < off2, OR
				   if (off1 < off2),
				   add a new segment in the merged list. */
				C2_ALLOC_PTR(new_seg);
				if (new_seg == NULL) {
					return -ENOMEM;
				}
				(*res_segs)++;
				new_seg->rs_seg.f_offset = iovec->fs_segs[i].
					f_offset;
				new_seg->rs_seg.f_count = iovec->fs_segs[i].
					f_count;
				c2_list_link_init(&new_seg->rs_linkage);
				c2_list_add_before(&read_seg->rs_linkage,
						&new_seg->rs_linkage);
				break;
			}
		}
		/* If the loop has run till the end of list or
		   if the list is empty, add the new read segment
		   to the list. */
		if ((&read_seg->rs_linkage == (void*)aggr_list) || list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				return -ENOMEM;
			}
			new_seg->rs_seg.f_offset = iovec->fs_segs[i].f_offset;
			new_seg->rs_seg.f_count = iovec->fs_segs[i].f_count;
			c2_list_link_init(&new_seg->rs_linkage);
			c2_list_add_tail(aggr_list, &new_seg->rs_linkage);
			(*res_segs)++;
		}
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
	c2_list_for_each_entry_safe(aggr_list, new_seg, new_seg_next,
			struct c2_io_read_segment, rs_linkage) {
		if ((new_seg->rs_seg.f_offset + new_seg->rs_seg.f_count)
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

	return 0;
}

/**
   Coalese the IO vectors of number of read fops and put the
   collated IO vector into given resultant fop.
   @param - list of read fops 
   @param - resultant fop 
 */
int c2_io_fop_read_coalesce(struct c2_list *list, struct c2_fop *b_fop)
{
	struct c2_list			 aggr_list;
	struct c2_io_fop_member		*fop_member = NULL;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	struct c2_fop_segment_seq	*read_vec = NULL;
	struct c2_io_read_segment	*read_seg = NULL;
	struct c2_io_read_segment	*read_seg_next = NULL;
	uint64_t			 curr_segs = 0;
	int				 res = 0;
	int				 i = 0;

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
		res = fop->f_type->ft_ops->fto_io_segment_coalesce((void*)
				&read_fop->frd_ioseg, &aggr_list, &curr_segs);
	}

	C2_ALLOC_PTR(read_vec);
	if (read_vec == NULL) {
		return -ENOMEM;
	}
	C2_ASSERT(curr_segs == c2_list_length(&aggr_list));
	C2_ALLOC_ARR(read_vec->fs_segs, curr_segs);
	if (read_vec->fs_segs == NULL) {
		return -ENOMEM;
	}
	c2_list_for_each_entry_safe(&aggr_list, read_seg,
			read_seg_next, struct c2_io_read_segment,
			rs_linkage) {
		read_vec->fs_segs[i] = read_seg->rs_seg;
		c2_list_del(&read_seg->rs_linkage);
		c2_free(read_seg);
		i++;
	}
	read_fop = c2_fop_data(b_fop);
	/* Should we free the IO vector here? */
	//c2_free(read_fop->frd_ioseg.fs_segs);
	/* Assign this vector to the current bound rpc item. */
	read_fop->frd_ioseg.fs_count = read_vec->fs_count;
	read_fop->frd_ioseg.fs_segs = read_vec->fs_segs;
	return 0;
}

/**
   Coalesce the IO segments from a number of write fops to create a list
   of IO segments containing merged segments.
   @param - target iovec 
   @param - aggr_list is list of read segments that could be merged
   @param - number of resultant merged segments 
 */
int c2_io_fop_write_segments_coalesce(void *vec,
		struct c2_list *aggr_list, uint64_t *nsegs)
{
	int					 i = 0;
	struct c2_io_write_segment		*write_seg = NULL;
	struct c2_io_write_segment		*write_seg_next = NULL;
	struct c2_io_write_segment		*new_seg = NULL;
	struct c2_io_write_segment		*new_seg_next = NULL;
	struct c2_fop_io_vec			*iovec = NULL;
	bool					 list_empty = true;

	C2_PRE(vec != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(nsegs != NULL);

	iovec = (struct c2_fop_io_vec*)vec;

	/* For all write segments in the write vector, check if they
	   can be merged with any of the segments from the aggregate list.
	   Merge if they can till all segments from write vector are
	   processed. */
	for (i = 0; i < iovec->iov_count; i++) {
		c2_list_for_each_entry_safe(aggr_list, write_seg,
				write_seg_next, struct
				c2_io_write_segment, ws_linkage) {
			list_empty = false;
			/* If off1 + count1 = off2, then merge two segments.*/
			if ((iovec->iov_seg[i].f_offset +
					iovec->iov_seg[i].f_buf.f_count)
					== write_seg->ws_seg.f_offset) {
				write_seg->ws_seg.f_offset =
					iovec->iov_seg[i].f_offset;
				write_seg->ws_seg.f_buf.f_count +=
					iovec->iov_seg[i].f_buf.f_count;
				break;
			} else if (write_seg->ws_seg.f_offset +
					write_seg->ws_seg.f_buf.f_count
					== iovec->iov_seg[i].f_offset) {
				/* If off2 + count2 = off1, then merge
				   two segments.*/
				write_seg->ws_seg.f_buf.f_count +=
					iovec->iov_seg[i].f_buf.f_count;
				break;
			} else if (((iovec->iov_seg[i].f_offset +
					iovec->iov_seg[i].f_buf.f_count) <
					write_seg->ws_seg.f_offset) ||
					(iovec->iov_seg[i].f_offset <
					 write_seg->ws_seg.f_offset)) {
				/* If (off1 + count1) < off2, OR
				   (off1 < off2),
				   add a new segment in the merged list. */
				C2_ALLOC_PTR(new_seg);
				if (new_seg == NULL) {
					return -ENOMEM;
				}
				(*nsegs)++;
				new_seg->ws_seg.f_offset = iovec->
					iov_seg[i].f_offset;
				new_seg->ws_seg.f_buf.f_count = iovec->
					iov_seg[i].f_buf.f_count;
				c2_list_add_before(&write_seg->ws_linkage,
						&new_seg->ws_linkage);
				break;
			}
		}
		/* If the loop has run till end of list or
		   if the list is empty, add the new write segment
		   in the list. */
		if ((&write_seg->ws_linkage == (void*)aggr_list)
				|| list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				return -ENOMEM;
			}
			new_seg->ws_seg.f_offset = iovec->
				iov_seg[i].f_offset;
			new_seg->ws_seg.f_buf.f_count = iovec->
				iov_seg[i].f_buf.f_count;
			c2_list_link_init(&new_seg->ws_linkage);
			c2_list_add_tail(aggr_list, &new_seg->ws_linkage);
			(*nsegs)++;
		}
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
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
			(*nsegs)--;
		}
	}

	return 0;
}

/**
   Coalesce the IO vectors of a list of write fops into IO vector
   of given resultant fop.
   @param - list of write fops 
   @param - resultant fop 
 */
int c2_io_fop_write_coalesce(struct c2_list *list, struct c2_fop *b_fop)
{
	struct c2_list			 aggr_list;
	struct c2_io_fop_member		*fop_member = NULL;
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_io_vec		*write_vec = NULL;
	struct c2_io_write_segment	*write_seg = NULL;
	struct c2_io_write_segment	*write_seg_next = NULL;
	uint64_t			 curr_segs = 0;
	int				 res = 0;
	int				 i = 0;

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
		res = fop->f_type->ft_ops->fto_io_segment_coalesce((void*)
				&write_fop->fwr_iovec, &aggr_list, &curr_segs);
	}

	C2_ALLOC_PTR(write_vec);
	if (write_vec == NULL) {
		return -ENOMEM;
	}
	C2_ASSERT(curr_segs == c2_list_length(&aggr_list));
	C2_ALLOC_ARR(write_vec->iov_seg, curr_segs);
	if (write_vec->iov_seg == NULL) {
		return -ENOMEM;
	}
	c2_list_for_each_entry_safe(&aggr_list, write_seg,
			write_seg_next, struct c2_io_write_segment,
			ws_linkage) {
		write_vec->iov_seg[i] = write_seg->ws_seg;
		c2_list_del(&write_seg->ws_linkage);
		c2_free(write_seg);
		i++;
	}
	write_fop = c2_fop_data(b_fop);
	/* Should we free the old IO vector? */
	//c2_free(write_fop->fwr_iovec.iov_seg);
	/* Assign this vector to the current bound rpc item. */
	write_fop->fwr_iovec.iov_count = write_vec->iov_count;
	write_fop->fwr_iovec.iov_seg = write_vec->iov_seg;
	return 0;
}

/**
   Return the opcode of given fop.
   @param - fop for which opcode has to be returned
 */
int c2_io_fop_get_opcode(struct c2_fop *fop)
{
	int opcode = 0;

	C2_PRE(fop != NULL);

	opcode = fop->f_type->ft_code;
	return opcode;
}

/**
 * readv FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = c2_io_fop_cob_readv_replied,
	.fto_getsize = c2_io_fop_cob_readv_getsize,
	.fto_op_equal = c2_io_fop_type_equal,
	.fto_get_opcode = c2_io_fop_get_opcode,
	.fto_get_fid = c2_io_fop_get_read_fid,
	.fto_is_io = c2_io_fop_is_rw,
	.fto_get_nfragments = c2_io_fop_read_get_nfragments,
	.fto_io_coalesce = c2_io_fop_read_coalesce,
	.fto_io_segment_coalesce = c2_io_fop_read_segments_coalesce,
};

/**
 * writev FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = c2_io_fop_cob_writev_replied,
	.fto_getsize = c2_io_fop_cob_writev_getsize,
	.fto_op_equal = c2_io_fop_type_equal,
	.fto_get_opcode = c2_io_fop_get_opcode,
	.fto_get_fid = c2_io_fop_get_write_fid,
	.fto_is_io = c2_io_fop_is_rw,
	.fto_get_nfragments = c2_io_fop_write_get_nfragments,
	.fto_io_coalesce = c2_io_fop_write_coalesce,
	.fto_io_segment_coalesce = c2_io_fop_write_segments_coalesce,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 */
static int c2_io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
   Return the size of a read_reply fop.
   @param - Read fop for which size has to be calculated
 */
uint64_t c2_io_fop_cob_readv_rep_getsize(struct c2_fop *fop)
{
	struct c2_fop_cob_readv_rep	*read_rep_fop = NULL;
	uint64_t			 size = 0;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	/* XXX Later 2 separate function are needed for read and write reply.*/
	if (fop->f_type->ft_code != c2_io_service_readv_rep_opcode) {
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
struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_rep_fom_init,
	.fto_getsize = c2_io_fop_cob_readv_rep_getsize,
};

int c2_io_fop_file_create_fom_init(struct c2_fop *fop, struct c2_fom **m);
/**
   Return size for a fop of type c2_fop_file_create;
   @param - Create fop for which size has to be calculated
 */
uint64_t c2_io_fop_create_getsize(struct c2_fop *fop)
{
	uint64_t			 size = 0;

	C2_PRE(fop != NULL);
	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);
	return size;
}

/* Ops vector for file create request. */
struct c2_fop_type_ops c2_fop_file_create_ops = {
	.fto_fom_init = c2_io_fop_file_create_fom_init,
	.fto_fop_replied = NULL,
	.fto_getsize = c2_io_fop_create_getsize,
	.fto_op_equal = c2_io_fop_type_equal,
	.fto_get_opcode = c2_io_fop_get_opcode,
	.fto_get_fid = NULL,
	.fto_is_io = c2_io_fop_is_rw,
	.fto_get_nfragments = NULL,
	.fto_io_coalesce = NULL,
	.fto_io_segment_coalesce = NULL,
};

/* Dummy init */
int c2_io_fop_file_create_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/* Ops vector for file create request. */
struct c2_fop_type_ops c2_fop_file_create_rep_ops = {
        .fto_fom_init = c2_io_fop_file_create_rep_fom_init,
        .fto_fop_replied = NULL,
        .fto_getsize = c2_io_fop_create_getsize,
        .fto_op_equal = c2_io_fop_type_equal,
        .fto_get_opcode = c2_io_fop_get_opcode,
        .fto_get_fid = NULL,
        .fto_is_io = c2_io_fop_is_rw,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
        .fto_io_segment_coalesce = NULL,
};

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request",
		c2_io_service_readv_opcode, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request",
		c2_io_service_writev_opcode, &c2_io_cob_writev_ops);
C2_FOP_TYPE_DECLARE(c2_fop_file_create, "File Create",
		c2_io_service_create_opcode, &c2_fop_file_create_ops);
C2_FOP_TYPE_DECLARE(c2_fop_file_create_rep, "File Create Reply",
		c2_io_service_create_rep_opcode, &c2_fop_file_create_rep_ops);
/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply",
		    c2_io_service_writev_rep_opcode, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply",
		    c2_io_service_readv_rep_opcode, &c2_io_rwv_rep_ops);

/**
   Function to take action on fop after it is replied to.
   @param - fop for which reply has been received 
 */
int c2_fop_replied(struct c2_fop *fop)
{
        struct c2_fop_cob_readv         *read_fop = NULL;
        struct c2_fop_cob_writev        *write_fop = NULL;

	C2_PRE(fop != NULL);

        if (fop->f_type->ft_code == c2_io_service_readv_opcode) {
                read_fop = c2_fop_data(fop);
		/* Not sure of fops should be deallocated here.*/
                c2_free(read_fop->frd_ioseg.fs_segs);
        } else if (fop->f_type->ft_code == c2_io_service_writev_opcode) {
                write_fop = c2_fop_data(fop);
		/* Not sure of fops should be deallocated here.*/
                c2_free(write_fop->fwr_iovec.iov_seg);
        }
        c2_fop_free(fop);
        return 0;
}

#ifdef __KERNEL__

/** Placeholder API for c2t1fs build. */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

int c2_io_fop_get_read_fop(struct c2_fop *curr_fop, struct c2_fop **res_fop,
		void *ioseg)
{
	return 0;
}

int c2_io_fop_get_write_fop(struct c2_fop *curr_fop, struct c2_fop **res_fop,
		void *iovec)
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
