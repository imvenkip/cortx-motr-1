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

#include "fop/fop.h"
#include "ioservice/io_foms.h"
#include "ioservice/io_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"
#include "fid/fid.h"
#include "reqh/reqh.h"
#include "stob/linux.h"

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/**
 * @addtogroup io_foms
 * @{
 */

extern bool is_read(const struct c2_fop *fop);
extern bool is_write(const struct c2_fop *fop);
extern bool is_io(const struct c2_fop *fop);
extern struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
extern struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop);

static int io_fom_cob_rwv_state(struct c2_fom *fom);
static int io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);
static void io_fom_cob_rwv_fini(struct c2_fom *fom);
static size_t io_fom_locality_get(const struct c2_fom *fom);

static struct c2_fom_ops c2_io_fom_rwv_ops = {
	.fo_fini = io_fom_cob_rwv_fini,
	.fo_state = io_fom_cob_rwv_state,
	.fo_home_locality = io_fom_locality_get,
};

static const struct c2_fom_type_ops c2_io_cob_rwv_type_ops = {
	.fto_create = io_fop_cob_rwv_fom_init,
};

static struct c2_fom_type c2_io_cob_rwv_type = {
	.ft_ops = &c2_io_cob_rwv_type_ops,
};

/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to
 * change as per the future requirements.
 */
static void io_fid2stob_map(const struct c2_fid *in, struct c2_stob_id *out)
{
	out->si_bits.u_hi = in->f_container;
	out->si_bits.u_lo = in->f_key;
}

/**
 * Function to map the on-wire FOP format to in-core FOP format.
 */
static void io_fid_wire2mem(struct c2_fop_file_fid *in, struct c2_fid *out)
{
	out->f_container = in->f_seq;
	out->f_key = in->f_oid;
}

/**
 * Allocate struct c2_io_fom_cob_rwv and return generic struct c2_fom
 * which is embedded in struct c2_io_fom_cob_rwv.
 * Find the corresponding fom_type and associate it with c2_fom.
 * Associate fop with fom type.
 */
static int io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_io_fom_cob_rwv *fom_obj;
	struct c2_fop		 *rep_fop;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);
	C2_PRE(is_io(fop));

	C2_ALLOC_PTR(fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type.ft_ops = &c2_io_cob_rwv_type_ops;
	if (is_read(fop))
		rep_fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
	else
		rep_fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);

	if (rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	c2_fom_create(&fom_obj->fcrw_gen, &c2_io_cob_rwv_type,
			&c2_io_fom_rwv_ops, fop, rep_fop);

	fom_obj->fcrw_stob = NULL;
	*m = &fom_obj->fcrw_gen;
	return 0;
}

static int io_fom_rwv_io_launch(struct c2_fom *fom)
{
	int				 rc;
	void				*addr;
	uint32_t			 bshift;
	uint64_t			 bmask;
	c2_bcount_t			 count;
	c2_bindex_t			 offset;
	struct c2_fid			 fid;
	struct c2_fop			*fop;
	struct c2_bufvec		*bufvec;
	struct c2_stob_id		 stobid;
	struct c2_stob_io		*stio;
	struct c2_indexvec		*ivec;
	struct c2_fop_cob_rw		*iofop;
	struct c2_fop_io_seg		*ioseg;
	struct c2_stob_domain		*fom_stdom;
	struct c2_fop_file_fid		*ffid;
	struct c2_io_fom_cob_rwv	*fom_obj;
	struct c2_fop_cob_readv_rep	*rrfop;

	C2_PRE(fom != NULL);

	fom_obj = container_of(fom, struct c2_io_fom_cob_rwv, fcrw_gen);
	fop = fom->fo_fop;

	iofop = io_rw_get(fop);
	ffid = &iofop->crw_fid;
	ioseg = iofop->crw_iovec.iv_segs;
	stio = &fom_obj->fcrw_st_io;
	fom->fo_phase = FOPH_COB_IO;

	io_fid_wire2mem(ffid, &fid);
	io_fid2stob_map(&fid, &stobid);
	fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	rc = c2_stob_find(fom_stdom, &stobid, &fom_obj->fcrw_stob);
	if (rc != 0)
		goto cleanup;

	rc = c2_stob_locate(fom_obj->fcrw_stob, &fom->fo_tx);
	if (rc != 0)
		goto cleanup_st;

	c2_stob_io_init(stio);

	/* Since the upper layer IO block size could differ with IO block size
	   of storage object, the block alignment and mapping is necesary. */
	bshift = fom_obj->fcrw_stob->so_op->sop_block_shift(fom_obj->fcrw_stob);
	bmask = (1 << bshift) - 1;

	/* Find out buffer address, offset and count required for stob io.
	   Due to existing limitations of kxdr wrapper over sunrpc, read reply
	   fop can not contain a vector, only a segment. Ideally, all IO fops
	   should carry an IO vector. */
	/** @todo With introduction of new rpc layer, ioservice has to be
	    changed to handle the whole vector, not just one segment. */
	/* This condition exists since read and write replies are not similar
	   at the moment due to sunrpc restrictions mentioned above! */
	if (is_write(fop)) {
		/* Make an FOL transaction record. */
		rc = c2_fop_fol_rec_add(fop, fom->fo_fol, &fom->fo_tx.tx_dbtx);
		if (rc != 0)
			goto cleanup_st;
		addr = c2_stob_addr_pack(ioseg->is_buf.ib_buf, bshift);
		stio->si_opcode = SIO_WRITE;
	} else {
		rrfop = c2_fop_data(fom->fo_rep_fop);
		C2_ALLOC_ARR(rrfop->c_iobuf.ib_buf,
				ioseg->is_buf.ib_count);
		if (rrfop->c_iobuf.ib_buf == NULL) {
			rc = -ENOMEM;
			goto cleanup_st;
		}
		addr = c2_stob_addr_pack(rrfop->c_iobuf.ib_buf, bshift);
		stio->si_opcode = SIO_READ;
	}

	count = ioseg->is_buf.ib_count;
	offset = ioseg->is_offset;

	C2_ASSERT((offset & bmask) == 0);
	C2_ASSERT((count & bmask) == 0);

	count = count >> bshift;
	offset = offset >> bshift;

	bufvec = &stio->si_user;
	bufvec->ov_vec.v_count = &count;
	bufvec->ov_buf = &addr;
	bufvec->ov_vec.v_nr = 1;

	ivec = &stio->si_stob;
	ivec->iv_index = &offset;
	ivec->iv_vec.v_count = &count;
	ivec->iv_vec.v_nr = 1;
	stio->si_flags = 0;

	c2_fom_block_at(fom, &stio->si_wait);
	rc = c2_stob_io_launch(stio, fom_obj->fcrw_stob, &fom->fo_tx, NULL);
	if (rc == 0) {
		fom->fo_rc = rc;
		fom->fo_phase = FOPH_COB_IO_WAIT;
	}
	else {
		if (is_read(fop))
			c2_free(rrfop->c_iobuf.ib_buf);
		goto cleanup_st;
	}
	return FSO_WAIT;

cleanup_st:
	c2_stob_put(fom_obj->fcrw_stob);
cleanup:
	C2_ASSERT(rc != 0);
	fom->fo_rc = rc;
	fom->fo_phase = FOPH_FAILURE;
	return FSO_AGAIN;
}

static int io_fom_rwv_io_launch_wait(struct c2_fom *fom)
{
	uint32_t			 bshift;
	struct c2_stob_io		*stio;
	struct c2_io_fom_cob_rwv	*fom_obj;
	struct c2_fop_cob_readv_rep	*rrfop;
	struct c2_fop_cob_rw_reply	*rwrep;

	C2_PRE(fom != NULL);

	fom_obj = container_of(fom, struct c2_io_fom_cob_rwv, fcrw_gen);
	bshift = fom_obj->fcrw_stob->so_op->sop_block_shift(fom_obj->fcrw_stob);
	stio = &fom_obj->fcrw_st_io;
	rwrep = io_rw_rep_get(fom->fo_rep_fop);

	rwrep->rwr_rc = stio->si_rc;
	rwrep->rwr_count = stio->si_count << bshift;

	/* Retrieve the status code and no of bytes read/written and
	   place it in respective reply FOP. This has to be handled
	   differently for read and write since reply fops for read
	   and write are not same at the moment due to kxdr limitation
	   mentioned above. */
	if (is_read(fom->fo_fop)) {
		rrfop = c2_fop_data(fom->fo_rep_fop);
		rrfop->c_iobuf.ib_count = stio->si_count << bshift;
	}

	c2_stob_io_fini(stio);
	c2_stob_put(fom_obj->fcrw_stob);

	fom->fo_rc = 0;
	fom->fo_phase = FOPH_SUCCESS;
	return FSO_AGAIN;
}

/**
 * State Transition function for read and write IO operation
 * that executes on data server.
 *  - Submit the read/write IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */
static int io_fom_cob_rwv_state(struct c2_fom *fom)
{
	int		 rc;

	C2_PRE(fom != NULL);
	C2_PRE(is_io(fom->fo_fop));

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	switch (fom->fo_phase) {
	case FOPH_COB_IO:
		rc = io_fom_rwv_io_launch(fom);
		break;
	case FOPH_COB_IO_WAIT:
		rc = io_fom_rwv_io_launch_wait(fom);
		break;
	default:
		C2_IMPOSSIBLE("Invalid phase of rw fom.");
	};

	return rc;
}

static void io_fom_cob_rwv_fini(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rwv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_io_fom_cob_rwv, fcrw_gen);
	c2_free(fom_ctx);
}

static size_t io_fom_locality_get(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_fop != NULL);

	return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

/** @} end of io_foms */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

