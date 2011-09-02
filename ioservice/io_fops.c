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

/**
   Generic IO segments ops.
   @param seg - Generic io segment.
   @retval - Returns the starting offset of given io segment.
 */
static uint64_t ioseg_offset_get(const struct c2_io_ioseg *seg)
{
	C2_PRE(seg != NULL);

	return seg->rw_seg->is_offset;
}

/**
   Return the number of bytes in input IO segment.
   @param seg - Generic IO segment.
   @param index - Index is needed for kernel implementation of this API.
   @retval - Returns the number of bytes in current IO segment.
 */
static uint64_t ioseg_count_get(const struct c2_io_ioseg *seg)
{
	C2_PRE(seg != NULL);

	return seg->rw_seg->is_buf.ib_count;
}

/**
   Sets the number of bytes in input IO segment.
   @param seg - Generic IO segment.
   @param count - Count to be set.
 */
static void ioseg_count_set(struct c2_io_ioseg *seg, const uint64_t count)
{
	C2_PRE(seg != NULL);

	seg->rw_seg->is_buf.ib_count = count;
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

	seg->rw_seg->is_offset = offset;
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

	ioseg->rw_seg = &iovec->iv_segs[index];
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

	return iovec->iv_count;
}

/**
   Set the number of IO segments in given IO vector.
   @param iovec - Input io vector.
   @param count - Set the number of IO segments to count.
 */
static void ioseg_nr_set(struct c2_fop_io_vec *iovec, const uint64_t count)
{
	C2_PRE(iovec != NULL);

	iovec->iv_count = count;
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

	C2_ALLOC_ARR(iovec->iv_segs, count);
	if (iovec->iv_segs == NULL)
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
   @retval - iovec from given fop.
 */
static struct c2_fop_io_vec *iovec_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_type		*fopt;

	fopt = fop->f_type;
	C2_PRE(fopt == &c2_fop_cob_readv_fopt ||
			fopt == &c2_fop_cob_writev_fopt);

	if (fopt == &c2_fop_cob_readv_fopt) {
		read_fop = c2_fop_data(fop);
		return &read_fop->cr_rwv.crw_iovec;
	} else {
		write_fop = c2_fop_data(fop);
		return &write_fop->cw_rwv.crw_iovec;
	}
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
	dest->iv_count = src->iv_count;
	dest->iv_segs = src->iv_segs;
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

/**
 * Allocate struct c2_io_fom_cob_rwv and return generic struct c2_fom
 * which is embedded in struct c2_io_fom_cob_rwv.
 * Find the corresponding fom_type and associate it with c2_fom.
 * Associate fop with fom type.
 */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

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
	size += read_fop->cr_rwv.crw_iovec.iv_count *
		sizeof(struct c2_fop_io_seg);
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
	vec_count = write_fop->cw_rwv.crw_iovec.iv_count;
	/* Size of actual user data. */
	for (i = 0; i < vec_count; ++i)
		size += write_fop->cw_rwv.crw_iovec.iv_segs[i].is_buf.ib_count;
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

uint64_t iovec_fragments_nr_get(struct c2_fop_io_vec *iovec)
{
	uint64_t		frag_nr = 1;
	uint32_t		i;
	uint64_t		off;
	uint64_t		cnt;
	uint64_t		off_next;
	uint32_t		segs_nr;
	struct c2_io_ioseg	ioseg;
	struct c2_io_ioseg	ioseg_next;

	C2_PRE(iovec != NULL);

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
	struct c2_fop_io_vec		*iovec;
	struct c2_fop_type		*fopt;

	C2_PRE(fop != NULL);

	fopt = fop->f_type;

	C2_PRE(fopt == &c2_fop_cob_readv_fopt ||
			fopt == &c2_fop_cob_writev_fopt);

	iovec = iovec_get(fop);
	return iovec_fragments_nr_get(iovec);
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
	C2_ALLOC_PTR(new_seg->rw_seg);
	if (new_seg->rw_seg == NULL) {
		c2_free(new_seg);
		return -ENOMEM;
	}

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
 */
static void io_fop_seg_coalesce(const struct c2_io_ioseg *seg,
		struct c2_list *aggr_list)
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
		if (rc == 0) {
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
   @retval - 0 if succeeded, negative error code otherwise.
   @note fop->f_type->ft_ops->fto_io_coalesce is called.
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
	segs_nr = ioseg_nr_get(iovec);
	for (i = 0; i < segs_nr; ++i) {
		ioseg_get(iovec, i, &ioseg);
		io_fop_seg_coalesce(&ioseg, aggr_list);
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
   @param bkpfop - A fop used to store the original IO vector of res_fop
   whose IO vector is replaced by the coalesced IO vector.
   from resultant fop and it is restored on receving the reply of this
   coalesced IO request. @see io_fop_iovec_restore.
 */
static int io_fop_coalesce(const struct c2_list *fop_list,
		struct c2_fop *res_fop, struct c2_fop *bkp_fop)
{
	int				 res = 0;
	int				 i = 0;
	uint64_t			 curr_segs;
	struct c2_fop			*fop;
	struct c2_list			 aggr_list;
	struct c2_io_ioseg		*ioseg;
	struct c2_io_ioseg		*ioseg_next;
	struct c2_io_ioseg		 res_ioseg;
	struct c2_fop_type		*fopt;
	struct c2_fop_io_vec		*iovec;
	struct c2_fop_io_vec		*bkp_vec;
	struct c2_fop_io_vec		*res_iovec = NULL;

	C2_PRE(fop_list != NULL);
	C2_PRE(res_fop != NULL);

	fopt = res_fop->f_type;
	C2_PRE(fopt == &c2_fop_cob_readv_fopt ||
			fopt == &c2_fop_cob_writev_fopt);

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
		ioseg_get(res_iovec, i, &res_ioseg);
		ioseg_copy(ioseg, &res_ioseg);
		ioseg_unlink_free(ioseg);
		i++;
	}
	c2_list_fini(&aggr_list);
	ioseg_nr_set(res_iovec, i);

	iovec = iovec_get(res_fop);
	bkp_vec = iovec_get(bkp_fop);
	iovec_copy(iovec, bkp_vec);
	iovec_copy(res_iovec, iovec);
	return res;
cleanup:
	C2_ASSERT(res != 0);
	if (res_iovec != NULL)
		iovec_free(res_iovec);
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
static void io_fop_iovec_restore(struct c2_fop *fop, struct c2_fop *bkpfop)
{
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_readv		*read_fop_bkp;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_cob_writev	*write_fop_bkp;
	struct c2_fop_type		*fopt_orig;
	struct c2_fop_type		*fopt_bkp;
	struct c2_fop_io_vec		*write_vec;
	struct c2_fop_io_vec		*write_vec_bkp;
	struct c2_fop_io_vec		*read_vec;
	struct c2_fop_io_vec		*read_vec_bkp;

	C2_PRE(fop != NULL);
	C2_PRE(bkpfop != NULL);

	fopt_orig = fop->f_type;
	fopt_bkp = bkpfop->f_type;
	C2_PRE(fopt_orig == fopt_bkp);

	if (fopt_orig == &c2_fop_cob_readv_fopt) {
		read_fop = c2_fop_data(fop);
		read_fop_bkp = c2_fop_data(bkpfop);
		read_vec = &read_fop->cr_rwv.crw_iovec;
		read_vec_bkp = &read_fop_bkp->cr_rwv.crw_iovec;
		c2_free(read_vec->iv_segs);
		read_vec->iv_count = read_vec_bkp->iv_count;
		read_vec->iv_segs = read_vec_bkp->iv_segs;
	} else {
		write_fop = c2_fop_data(fop);
		write_fop_bkp = c2_fop_data(bkpfop);
		write_vec = &write_fop->cw_rwv.crw_iovec;
		write_vec_bkp = &write_fop_bkp->cw_rwv.crw_iovec;
		c2_free(write_vec->iv_segs);
		write_vec->iv_count = write_vec_bkp->iv_count;
		write_vec->iv_segs = write_vec_bkp->iv_segs;
	}
	c2_fop_free(bkpfop);
}

/**
   Return the fid of given IO fop.
   @param fop - Incoming fop.
   @note This method only works for read and write IO fops.
   @retval On-wire fid of given fop.
 */
static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop)
{
	struct c2_fop_file_fid		*ffid;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_type		*fopt;

	C2_PRE(fop != NULL);

	fopt = fop->f_type;
	if (fopt != &c2_fop_cob_readv_fopt &&
			fopt != &c2_fop_cob_writev_fopt)
		return NULL;

	if (fopt == &c2_fop_cob_readv_fopt) {
		read_fop = c2_fop_data(fop);
		ffid = &read_fop->cr_rwv.crw_fid;
	} else {
		write_fop = c2_fop_data(fop);
		ffid = &write_fop->cw_rwv.crw_fid;
	}
	return ffid;
}

/**
   Return if given 2 fops refer to same fid. The fids mentioned here
   are on-wire fids.
   @param fop1 - First fop.
   @param fop2 - Second fop.
   @retval true if both fops refer to same fid, false otherwise.
 */
static bool io_fop_fid_equal(struct c2_fop *fop1, struct c2_fop *fop2)
{
	struct c2_fop_file_fid *ffid1;
	struct c2_fop_file_fid *ffid2;

	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	ffid1 = io_fop_fid_get(fop1);
	if (ffid1 == NULL)
		return false;

	ffid2 = io_fop_fid_get(fop2);
	if (ffid2 == NULL)
		return false;

	return (ffid1->f_seq == ffid2->f_seq && ffid1->f_oid == ffid2->f_oid);
}

/**
 * readv FOP operation vector.
 */
const struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = NULL,
	.fto_size_get = io_fop_cob_readv_getsize,
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
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
	.fto_fop_replied = NULL,
	.fto_size_get = io_fop_cob_writev_getsize,
	.fto_op_equal = io_fop_type_equal,
	.fto_fid_equal = io_fop_fid_equal,
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

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "Read request",
		C2_IO_SERVICE_READV_OPCODE, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "Write request",
		C2_IO_SERVICE_WRITEV_OPCODE, &c2_io_cob_writev_ops);

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
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_cob_rwv_tfmt,
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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
