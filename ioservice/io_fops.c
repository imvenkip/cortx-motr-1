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
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif
#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fop.h"

/*
   Forward declarations.
 */
static int io_fop_get_opcode(const struct c2_fop *fop);

/**
   Generic IO segments ops.
   @param seg - Generic io segment.
   @retval - Returns the starting offset of given io segment.
 */
static uint64_t ioseg_offset_get(const struct c2_io_ioseg *seg)
{
	C2_PRE(seg != NULL);

	return seg->rw_seg->f_offset;
}

/**
   Return the number of bytes in input IO segment.
   @param seg - Generic IO segment.
   @retval - Returns the number of bytes in current IO segment.
 */
static uint64_t ioseg_count_get(const struct c2_io_ioseg *seg)
{
	C2_PRE(seg != NULL);

	return seg->rw_seg->f_buf.f_count;
}

/**
   Generic IO segments ops.
   Set the starting offset of given IO segment.
   @param seg - Generic io segment.
   @param offset - Offset to be set.
 */
static void ioseg_offset_set(struct c2_io_ioseg *seg, const uint64_t offset)
{
	C2_PRE(seg != NULL);

	seg->rw_seg->f_offset = offset;
}

/**
   Sets the number of bytes in input IO segment.
   @param seg - Generic IO segment.
   @param count - Count to be set.
 */
static void ioseg_count_set(struct c2_io_ioseg *seg, const uint64_t count)
{
	C2_PRE(seg != NULL);

	seg->rw_seg->f_buf.f_count = count;
}

/**
   Get IO segment from array given the index.
   @param iovec - IO vector, given segment belongs to.
   @param index - Index into array of io segments.
   @param ioseg - Out parameter for io segment.
 */
static void ioseg_get(const struct c2_fop_io_vec *iovec, const int index,
		struct c2_io_ioseg *ioseg)
{
	C2_PRE(iovec != NULL);
	C2_PRE(ioseg != NULL);

	ioseg->rw_seg = &iovec->iov_segs[index];
}

/**
   Copy src IO segment into destination. The data buffer is not copied.
   Only offset and count are copied.
   @param src - Input IO segment.
   @param dest - Output IO segment.
 */
static void ioseg_copy(const struct c2_io_ioseg *src, struct c2_io_ioseg *dest)
{
	uint64_t offset;
	uint64_t cnt;

	C2_PRE(src != NULL);
	C2_PRE(dest != NULL);

	offset = ioseg_offset_get(src);
	ioseg_offset_set(dest, offset);

	cnt = ioseg_count_get(src);
	ioseg_count_set(dest, cnt);
}

/**
   Return the number of IO segments in given IO vector.
   @param iovec - Input io vector.
   @retval - Number of segments contained in given io vector.
 */
static uint32_t ioseg_nr_get(const struct c2_fop_io_vec *iovec)
{
	C2_PRE(iovec != NULL);

	return iovec->iov_count;
}

/**
   Set the number of IO segments in given IO vector.
   @param iovec - Input io vector.
   @param count - Set the number of IO segments to count.
 */
static void ioseg_nr_set(struct c2_fop_io_vec *iovec, const uint64_t count)
{
	C2_PRE(iovec != NULL);

	iovec->iov_count = count;
}

/**
   Allocate the array of IO segments from IO vector.
   @param iovec - Input io vector.
   @param count - Number of segments to be allocated.
   @retval - 0 if succeeded, negative error code otherwise.
 */
static int iosegs_alloc(struct c2_fop_io_vec *iovec, const uint32_t count)
{
	int rc = 0;

	C2_PRE(iovec != NULL);
	C2_PRE(count != 0);

	C2_ALLOC_ARR(iovec->iov_segs, count);
	if (iovec->iov_segs == NULL)
		rc = -ENOMEM;
	return rc;
}

/**
   Deallocate the IO vector.
   @param iovec - Input io vector.
 */
static void iovec_free(struct c2_fop_io_vec *iovec)
{
	C2_PRE(iovec != NULL);

	c2_free(iovec);
}

/**
   Return IO vector from given IO fop.
   @param fop - Input fop whose IO vector has to be returned.
   @param iovec - out parameter for IO vector.
   @retval - iovec from given fop.
 */
static void iovec_get(struct c2_fop *fop, struct c2_fop_io_vec *iovec)
{
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_writev	*write_fop;
	enum c2_io_service_opcodes	 op;

	op = io_fop_get_opcode(fop);
	C2_PRE(op == C2_IO_SERVICE_READV_OPCODE ||
			op == C2_IO_SERVICE_WRITEV_OPCODE);

	if (op == C2_IO_SERVICE_READV_OPCODE) {
		read_fop = c2_fop_data(fop);
		iovec = &read_fop->frd_iovec;
	} else {
		write_fop = c2_fop_data(fop);
		iovec = &write_fop->fwr_iovec;
	}
}

/**
   Allocate a new IO vector.
   @param iovec - out parameter for IO vector.
   @retval - 0 if succeeded, negative error code otherwise.
 */
static int iovec_alloc(struct c2_fop_io_vec **iovec)
{
	int			 rc = 0;
	struct c2_fop_io_vec	*vec;

	C2_PRE(iovec != NULL);

	vec = *iovec;
	C2_ALLOC_PTR(vec);
	if (vec == NULL)
		rc = -ENOMEM;
	return rc;
}

/**
   Copy src IO vector into destination. The IO segments are not
   copied completely, rather just the pointer to it is copied.
   @param src - Source IO vector.
   @param dest - Destination IO vector.
 */
static void iovec_copy(const struct c2_fop_io_vec *src,
		struct c2_fop_io_vec *dest)
{
	C2_PRE(src != NULL);
	C2_PRE(dest != NULL);

	/* The io segment pointer is copied not the io segment itself. */
	dest->iov_count = src->iov_count;
	dest->iov_segs = src->iov_segs;
}

/**
   Deallocate and remove a generic IO segment from aggr_list.
   @param ioseg - Input generic io segment.
 */
static void ioseg_unlink_free(struct c2_io_ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);

	c2_list_del(&ioseg->io_linkage);
	c2_free(ioseg);
}


int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
   Reply received callback for a c2_fop_cob_readv fop.
   @note fop.f_item.ri_type->rit_ops->rio_replied is called.

   @param - Read fop for which reply is received
 */
static void io_fop_cob_readv_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);

	read_fop = c2_fop_data(fop);
	c2_free(read_fop->frd_iovec.iov_segs);
	c2_fop_free(fop);
}

/**
   Reply received callback for a c2_fop_cob_writev fop.
   @note fop.f_item.ri_type->rit_ops->rio_replied is called.

   @param - Write fop for which reply is received
 */
static void io_fop_cob_writev_replied(struct c2_fop *fop)
{
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(fop != NULL);

	write_fop = c2_fop_data(fop);
	c2_free(write_fop->fwr_iovec.iov_segs);
	c2_fop_free(fop);
}

/**
   Return the size of a fop of type c2_fop_cob_readv.
   @note fop.f_item.ri_type->rit_ops->rio_item_size is called.
   @param - Read fop for which size is to be calculated
   @retval - Returns the size of fop in number of bytes.
   @todo Eventually will be replaced by wire formats _getsize API.
 */
static uint64_t io_fop_cob_readv_getsize(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop;
	uint64_t			 size = 0;

	C2_PRE(fop != NULL);

	/* Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;

	read_fop = c2_fop_data(fop);
	C2_ASSERT(read_fop != NULL);
	size += read_fop->frd_iovec.iov_count * sizeof(struct c2_fop_io_seg);
	return size;
}

/**
   Return the size of a fop of type c2_fop_cob_writev.
   @note fop.f_item.ri_type->rit_ops->rio_item_size is called.
   @param - Write fop for which size is to be calculated
   @retval - Returns the size of fop in number of bytes.
   @todo Eventually will be replaced by wire formats _getsize API.
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

	write_fop = c2_fop_data(fop);
	C2_ASSERT(write_fop != NULL);
	vec_count = write_fop->fwr_iovec.iov_count;
	/* Size of actual user data. */
	for (i = 0; i < vec_count; ++i)
		size += write_fop->fwr_iovec.iov_segs[i].f_buf.f_count;
	/* Size of holding structure. */
	size += vec_count * sizeof(struct c2_fop_io_seg);

	return size;
}

/**
   Return if given 2 fops belong to same type.
   @note fop.f_item.ri_type->rit_ops->rio_items_equal is called.
   @param - Fop1 to be compared with Fop2
   @param - Fop2 to be compared with Fop1
   @retval - TRUE if fops are of same type, FALSE otherwise.
 */
static bool io_fop_type_equal(const struct c2_fop *fop1,
		const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

/**
   Return fid for a fop of type c2_fop_cob_readv.
   @note fop.f_item.ri_type->rit_ops->rio_io_get_fid is called.
   @param - Read fop for which fid is to be calculated
   @retval - fid of incoming fop.
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
   @note fop.f_item.ri_type->rit_ops->rio_io_get_fid is called.
   @param - Write fop for which fid is to be calculated
   @retval - fid of incoming fop.
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
   @note fop.f_item.ri_type->rit_ops->rio_is_io_req is called.

   @param - fop for which rw status is to be found out
   @retval - TRUE if fop is read or write operation, FALSE otherwise.
 */
static bool io_fop_is_rw(const struct c2_fop *fop)
{
	int op;

	C2_PRE(fop != NULL);

	op = fop->f_type->ft_code;
	return op == C2_IO_SERVICE_READV_OPCODE ||
			op == C2_IO_SERVICE_WRITEV_OPCODE;
}

uint64_t iovec_fragments_nr_get(struct c2_fop_io_vec *iovec,
		enum c2_io_service_opcodes op)
{
	uint64_t		frag_nr = 1;
	uint64_t		i;
	uint64_t		off;
	uint64_t		cnt;
	uint64_t		off_next;
	uint32_t		segs_nr;
	struct c2_io_ioseg	ioseg;
	struct c2_io_ioseg	ioseg_next;

	C2_PRE(iovec != NULL);
	C2_PRE(op == C2_IO_SERVICE_READV_OPCODE ||
			op == C2_IO_SERVICE_WRITEV_OPCODE);

	segs_nr = ioseg_nr_get(iovec);
	for (i = 0; i < segs_nr - 1; ++i) {
		ioseg_get(iovec, i, &ioseg);
		off = ioseg_offset_get(&ioseg);
		cnt = ioseg_count_get(&ioseg);
		ioseg_get(iovec, i+1, &ioseg_next);
		off_next = ioseg_offset_get(&ioseg_next);
		if (off + cnt != off_next)
			frag_nr++;
	}
	return frag_nr;
}

/**
   Return the number of IO fragements(discontiguous buffers)
   for a fop of type read or write.
   @note fop.f_item.ri_type->rit_ops->rio_get_io_fragment_count is called.
   @param - Read fop for which number of fragments is to be calculated
   @retval - Returns number of IO fragments.
 */
static uint64_t io_fop_fragments_nr_get(struct c2_fop *fop)
{
	struct c2_fop_io_vec		iovec;
	enum c2_io_service_opcodes	op;

	C2_PRE(fop != NULL);

	iovec_get(fop, &iovec);
	op = io_fop_get_opcode(fop);
	return iovec_fragments_nr_get(&iovec, op);
}

/**
   Restore the original IO vector of resultant read fop.
   @param fop - Incoming fop.
   @param vec - A union pointing to original IO vector.
 */
void io_fop_read_iovec_restore(struct c2_fop *fop, struct c2_fop_io_vec *vec)
{
	struct c2_fop_cob_readv	*read_fop;

	C2_PRE(fop != NULL);
	C2_PRE(vec != NULL);

	read_fop = c2_fop_data(fop);

	c2_free(read_fop->frd_iovec.iov_segs);

	read_fop->frd_iovec.iov_count = vec->iov_count;
	read_fop->frd_iovec.iov_segs = vec->iov_segs;
}

/**
   Allocate a new generic IO segment
   @note fop->f_type->ft_ops->fto_io_coalesce is called.
   @note io_fop_segments_coalesce is called.
   @note io_fop_seg_coalesce is called || io_fop_seg_add_cond is called.

   @param offset - starting offset of current IO segment from aggr_list.
   @param count - count of bytes in current IO segment from aggr_list.
   @param ns - New IO segment from IO vector.
   @retval - 0 if succeeded, negative error code otherwise.
 */
static int io_fop_seg_init(uint64_t offset, uint32_t count,
		struct c2_io_ioseg **ns)
{
	struct c2_io_ioseg	*new_seg;

	C2_PRE(ns != NULL);

	C2_ALLOC_PTR(new_seg);
	if (new_seg == NULL)
		return -ENOMEM;

	ioseg_offset_set(new_seg, offset);
	ioseg_count_set(new_seg, count);
	*ns = new_seg;
	return 0;
}

/**
   Add a new IO segment to the aggr_list conditionally.

   @note fop->f_type->ft_ops->fto_io_coalesce is called.
   @note io_fop_segments_coalesce is called.
   @note io_fop_seg_coalesce is called.

   @param seg - current IO segment from IO vector.
   @param off1 - starting offset of current IO segment from aggr_list.
   @param cnt1 - count of bytes in current IO segment from aggr_list.
   @reval - 0 if succeeded, negative error code otherwise.
 */
static int io_fop_seg_add_cond(struct c2_io_ioseg *seg, const uint64_t off1,
		const uint32_t cnt1)
{
	int			 rc = 0;
	uint64_t		 off2;
	struct c2_io_ioseg	*new_seg;

	C2_PRE(seg != NULL);

	off2 = ioseg_offset_get(seg);

	if (off1 < off2) {
		rc = io_fop_seg_init(off1, cnt1, &new_seg);
		if (rc < 0)
			return rc;

		c2_list_add_before(&seg->io_linkage, &new_seg->io_linkage);
	} else
		rc = -EINVAL;

	return rc;
}

/**
   Given an IO segment from IO vector, see if it can be fit with
   existing set of segments in aggr_list.
   If yes, change corresponding segment from aggr_list accordingly.
   The segment is added in a sorted manner of starting offset in aggr_list.
   Else, add a new segment to the aggr_list.
   @note fop->f_type->ft_ops->fto_io_coalesce is called.
   @note io_fop_segments_coalesce is called.
   @note This is a best-case effort or an optimization effort. That is why
   return value is void. If something fails, everything is undone and function
   returns.

   @param seg - current write segment from IO vector.
   @param aggr_list - list of write segments which gets built during
    this operation.
   @param op - Opcode, given seg belongs to.
 */
static void io_fop_seg_coalesce(const struct c2_io_ioseg *seg,
		struct c2_list *aggr_list, enum c2_io_service_opcodes op)
{
	int				 rc = 0;
	bool				 added = false;
	uint64_t			 off1;
	uint32_t			 cnt1;
	struct c2_io_ioseg		*new_seg;
	struct c2_io_ioseg		*ioseg;
	struct c2_io_ioseg		*ioseg_next;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_list != NULL);

	/* off1 and cnt1 are offset and count of incoming IO segment. */
	off1 = ioseg_offset_get(seg);
	cnt1 = ioseg_count_get(seg);

	c2_list_for_each_entry_safe(aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		/* If given segment fits before some other segment
		   in increasing order of offsets, add it before
		   current segments from aggr_list. */
		rc = io_fop_seg_add_cond(ioseg, off1, cnt1);
		if (rc == -ENOMEM)
			return;
		else {
			added = true;
			break;
		}
	}

	/* Add a new IO segment unconditionally in aggr_list. */
	if (!added) {
		rc = io_fop_seg_init(off1, cnt1, &new_seg);
		if (rc < 0)
			return;
		c2_list_add_tail(aggr_list, &new_seg->io_linkage);
	}
}

/**
   Coalesce the IO segments from a number of IO fops to create a list
   of IO segments containing merged segments.
   @param iovec - vector of IO segments.
   @param aggr_list - list of IO segments which gets populated during
   this operation.
   @param op - Operation type, given iovec belongs to.
   @retval - 0 if succeeded, negative error code otherwise.
   @note fop->f_type->ft_ops->fto_io_coalesce is called.
*/
static int io_fop_segments_coalesce(struct c2_fop_io_vec *iovec,
		struct c2_list *aggr_list, enum c2_io_service_opcodes op)
{
	int			i;
	int			rc = 0;
	struct c2_io_ioseg	ioseg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_list != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < ioseg_nr_get(iovec); ++i) {
		ioseg_get(iovec, i, &ioseg);
		io_fop_seg_coalesce(&ioseg, aggr_list, op);
	}

	return rc;
}

/**
   Coalesce the IO vectors of a list of read/write fops into IO vector
   of given resultant fop. At a time, all fops in the list are either
   read fops or write fops. Both fop types can not be present simultaneously.
   @note fop.f_item.ri_type->rit_ops->rio_io_coalesce is called.

   @param fop_list - list of fops. These structures
   contain either read or write fops. All fops from list are either
   read fops or write fops. Both fop types can not be present in the fop_list
   simultaneously.
   @param res_fop - resultant fop with which the resulting IO vector is
   associated.
   @param vec - Input IO vector which stores the original IO vector
   from resultant fop and it is restored on receving the reply of this
   coalesced IO request. @see io_fop_iovec_restore.
 */
static int io_fop_coalesce(const struct c2_list *fop_list,
		struct c2_fop *res_fop, struct c2_fop_io_vec *vec)
{
	int				 res = 0;
	int				 i = 0;
	uint64_t			 curr_segs;
	struct c2_fop			*fop;
	struct c2_list			 aggr_list;
	struct c2_fop_io_vec		 iovec;
	struct c2_fop_io_vec		*res_iovec = NULL;
	struct c2_io_ioseg		*ioseg;
	struct c2_io_ioseg		*ioseg_next;
	struct c2_io_ioseg		 res_ioseg;
	enum c2_io_service_opcodes	 op;

	C2_PRE(fop_list != NULL);
	C2_PRE(res_fop != NULL);
	C2_PRE(vec != NULL);

	op = res_fop->f_type->ft_code;

        /* Make a copy of original IO vector belonging to res_fop and place
           it in input parameter vec which can be used while restoring the
           IO vector. */
        res = iovec_alloc(&vec);
        if (res != 0)
                return -ENOMEM;

	c2_list_init(&aggr_list);

	/* Traverse the fop_list, get the IO vector from each fop,
	   pass it to a coalescing routine and get result back
	   in another list. */
	c2_list_for_each_entry(fop_list, fop, struct c2_fop, f_link) {
		iovec_get(fop, &iovec);
		res = io_fop_segments_coalesce(&iovec, &aggr_list, op);
	}

	/* Allocate a new generic IO vector and copy all (merged) IO segments
	   to the new vector and make changes to res_fop accordingly. */
	res = iovec_alloc(&res_iovec);
	if (res != 0)
		goto cleanup;

	curr_segs = c2_list_length(&aggr_list);
	res = iosegs_alloc(res_iovec, curr_segs);
	if (res != 0)
		goto cleanup;

	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
			struct c2_io_ioseg, io_linkage) {
		ioseg_get(res_iovec, i, &res_ioseg);
		ioseg_copy(ioseg, &res_ioseg);
		ioseg_unlink_free(ioseg);
		i++;
	}
	c2_list_fini(&aggr_list);
	ioseg_nr_set(res_iovec, i);

	iovec_get(res_fop, &iovec);
	iovec_copy(&iovec, vec);
	iovec_copy(res_iovec, &iovec);
	return res;
cleanup:
	C2_ASSERT(res != 0);
	if (res_iovec != NULL)
		iovec_free(res_iovec);
	if (vec != NULL)
		iovec_free(vec);
	c2_list_for_each_entry_safe(&aggr_list, ioseg, ioseg_next,
				    struct c2_io_ioseg, io_linkage)
		ioseg_unlink_free(ioseg);
	c2_list_fini(&aggr_list);
	return res;
}

/**
   Restore the original IO vector of resultant fop from the appropriate
   IO vector from parameter vec.
   @param fop - Incoming fop. This fop is same as res_fop parameter from
   the subroutine io_fop_coalesce. @see io_fop_coalesce.
   @param vec - A union pointing to original IO vector.
 */
static void io_fop_iovec_restore(struct c2_fop *fop, struct c2_fop_io_vec *vec)
{
	enum c2_io_service_opcodes	 op;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_writev	*write_fop;

	C2_PRE(vec != NULL);

	op = io_fop_get_opcode(fop);
	C2_PRE(op == C2_IO_SERVICE_READV_OPCODE ||
			op == C2_IO_SERVICE_WRITEV_OPCODE);

	if (op == C2_IO_SERVICE_READV_REP_OPCODE) {
		read_fop = c2_fop_data(fop);
		c2_free(read_fop->frd_iovec.iov_segs);
		read_fop->frd_iovec.iov_count = vec->iov_count;
		read_fop->frd_iovec.iov_segs = vec->iov_segs;
	} else {
		write_fop = c2_fop_data(fop);
		c2_free(write_fop->fwr_iovec.iov_segs);
		write_fop->fwr_iovec.iov_count = vec->iov_count;
		write_fop->fwr_iovec.iov_segs = vec->iov_segs;
	}
	c2_free(vec);
}

/**
   Return the op of given fop.
   @note fop.f_item.ri_type->rit_ops->rio_io_get_opcode is called.

   @param - fop for which op has to be returned
 */
static int io_fop_get_opcode(const struct c2_fop *fop)
{
	int op;

	C2_PRE(fop != NULL);

	op = fop->f_type->ft_code;
	return op;
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
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
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
	.fto_get_nfragments = io_fop_fragments_nr_get,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_iovec_restore = io_fop_iovec_restore,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 * @param fop - fop on which this fom_init methods operates.
 * @param m - fom object to be created here.
 */
static int io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
   Return the size of a read_reply fop.
   @note fop.f_item.ri_type->rit_ops->rio_item_size is called.
   @todo Eventually will be replaced by wire formats _getsize API.

   @param - Read fop for which size has to be calculated
 */
static uint64_t io_fop_cob_readv_rep_getsize(struct c2_fop *fop)
{
	uint64_t			 size;
	struct c2_fop_cob_readv_rep	*read_rep_fop;

	C2_PRE(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;

	if (fop->f_type->ft_code != C2_IO_SERVICE_READV_REP_OPCODE)
		return size;
	/* Add buffer payload for read reply */
	read_rep_fop = c2_fop_data(fop);
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
   @todo Eventually will be replaced by wire formats _getsize API.
   @param - Create fop for which size has to be calculated
 */
static uint64_t io_fop_create_getsize(struct c2_fop *fop)
{
	uint64_t size;

	C2_PRE(fop != NULL);

	/* Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
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

static struct c2_fop_type_format *ioservice_fmts[] = {
	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_segment_tfmt,
	&c2_fop_segment_seq_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_readv_rep_tfmt,
	&c2_fop_cob_readv_tfmt,
	&c2_fop_cob_writev_rep_tfmt,
	&c2_fop_cob_writev_tfmt,
	&c2_fop_file_create_tfmt,
	&c2_fop_file_create_rep_tfmt,
};

static struct c2_fop_type *ioservice_fops[] = {
	&c2_fop_cob_readv_rep_fopt,
	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_rep_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_file_create_fopt,
	&c2_fop_file_create_rep_fopt,
};

void ioservice_fop_fini(void)
{
	c2_fop_type_fini_nr(ioservice_fops, ARRAY_SIZE(ioservice_fops));
	c2_fop_type_format_fini_nr(ioservice_fmts, ARRAY_SIZE(ioservice_fmts));
}

int ioservice_fop_init(void)
{
	int rc;

	rc = c2_fop_type_format_parse_nr(ioservice_fmts,
			ARRAY_SIZE(ioservice_fmts));
	if (rc == 0)
		rc = c2_fop_type_build_nr(ioservice_fops,
				ARRAY_SIZE(ioservice_fops));
	if (rc != 0)
		ioservice_fop_fini();
	return rc;
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
