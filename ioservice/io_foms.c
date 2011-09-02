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

#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "fop/fop_format_def.h"
#include "ioservice/io_fops.ff"

/**
 * @addtogroup io_foms
 * @{
 */

/** Generic ops object for c2_fop_cob_writev */
static struct c2_fom_ops c2_io_fom_write_ops = {
	.fo_fini = NULL,
	.fo_state = c2_io_fom_cob_rwv_state,
};

/** Generic ops object for c2_fop_cob_readv */
static struct c2_fom_ops c2_io_fom_read_ops = {
	.fo_fini = NULL,
	.fo_state = c2_io_fom_cob_rwv_state,
};

/** Generic ops object for readv and writev reply FOPs */
struct c2_fom_ops c2_io_fom_rwv_rep = {
	.fo_fini = NULL,
	.fo_state = NULL,
};

/** FOM type specific functions for readv FOP. */
static const struct c2_fom_type_ops c2_io_cob_readv_type_ops = {
	.fto_create = NULL,
};

/** FOM type specific functions for writev FOP. */
static const struct c2_fom_type_ops c2_io_cob_writev_type_ops = {
	.fto_create = NULL,
};

/** Readv specific FOM type operations vector. */
static struct c2_fom_type c2_io_fom_cob_readv_mopt = {
	.ft_ops = &c2_io_cob_readv_type_ops,
};

/** Writev specific FOM type operations vector. */
static struct c2_fom_type c2_io_fom_cob_writev_mopt = {
	.ft_ops = &c2_io_cob_writev_type_ops,
};

/**
 *  An array of c2_fom_type structs for all possible FOMs.
 */
static struct c2_fom_type *c2_io_fom_types[] = {
	&c2_io_fom_cob_readv_mopt,
	&c2_io_fom_cob_writev_mopt,
};

/**
 * Find out the respective FOM type object (c2_fom_type)
 * from the given opcode.
 * This opcode is obtained from the FOP type (c2_fop_type->ft_code)
 */
struct c2_fom_type *c2_io_fom_type_map(c2_fop_type_code_t code)
{
	C2_PRE(IS_IN_ARRAY((code - C2_IO_SERVICE_READV_OPCODE),
			   c2_io_fom_types));
	return c2_io_fom_types[code - C2_IO_SERVICE_READV_OPCODE];
}

/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to
 * change as per the future requirements.
 */
void c2_io_fid2stob_map(struct c2_fid *in, struct c2_stob_id *out)
{
	out->si_bits.u_hi = in->f_container;
	out->si_bits.u_lo = in->f_key;
}

/**
 * Function to map the on-wire FOP format to in-core FOP format.
 */
static void c2_io_fid_wire2mem(struct c2_fop_file_fid *in, struct c2_fid *out)
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
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom			*fom;
	struct c2_io_fom_cob_rwv	*fom_obj;
	struct c2_fom_type 		*fom_type;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom_type = c2_io_fom_type_map(fop->f_type->ft_code);
	C2_ASSERT(fom_type != NULL);
	fop->f_type->ft_fom_type = *fom_type;
	fom = &fom_obj->fcrw_gen;
	fom->fo_type = fom_type;

	if (fop->f_type->ft_code == C2_IO_SERVICE_READV_OPCODE) {
		fom->fo_ops = &c2_io_fom_read_ops;
		fom_obj->fcrw_rep_fop =
			c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
		if (fom_obj->fcrw_rep_fop == NULL) {
			c2_free(fom_obj);
			return -ENOMEM;
		}
	} else if (fop->f_type->ft_code == C2_IO_SERVICE_WRITEV_OPCODE) {
		fom->fo_ops = &c2_io_fom_write_ops;
		fom_obj->fcrw_rep_fop =
			c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
		if (fom_obj->fcrw_rep_fop == NULL) {
			c2_free(fom_obj);
			return -ENOMEM;
		}
	}

	fom_obj->fcrw_fop = fop;
	fom_obj->fcrw_stob = NULL;
	*m = &fom_obj->fcrw_gen;
	return 0;
}

/**
 * <b> State Transition function for "read and write IO" operation
 *     that executes on data server. </b>
 *  - Submit the read/write IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */
int c2_io_fom_cob_rwv_state(struct c2_fom *fom)
{
	struct c2_fop_file_fid		*ffid;
	struct c2_fid			 fid;
	struct c2_io_fom_cob_rwv	*fom_obj;
	struct c2_stob_id		 stobid;
	struct c2_dtx			 tx;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int				 result;
	void				*addr;
	c2_bcount_t			 count;
	c2_bindex_t			 offset;
	struct c2_clink			 clink;
	int				 rc;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop_cob_writev_rep	*wr_rep_fop;
	struct c2_fop_cob_readv_rep	*rd_rep_fop;
	struct c2_fop_io_seg		*write_seg;
	struct c2_fop_io_seg		*read_seg;
	struct c2_fop_type		*fopt;
	struct c2_stob_io		*stio;

	C2_PRE(fom != NULL);

	/* Since a c2_fom object is passed down to every FOM state method,
	   the context structure which is the parent structure of the FOM
	   is type casted from c2_fom. */
	fom_obj = container_of(fom, struct c2_io_fom_cob_rwv, fcrw_gen);
	fopt = fom_obj->fcrw_fop->f_type;

	/* Retrieve the request and reply FOPs. Extract the on-write FID
	   from the FOPs. */
	if (fopt == &c2_fop_cob_writev_fopt) {
		write_fop = c2_fop_data(fom_obj->fcrw_fop);
		wr_rep_fop = c2_fop_data(fom_obj->fcrw_rep_fop);
		ffid = &write_fop->c_rwv.crw_fid;
		/* Change the phase of FOM */
		fom->fo_phase = FOPH_COB_WRITE;
	} else {
		read_fop = c2_fop_data(fom_obj->fcrw_fop);
		rd_rep_fop = c2_fop_data(fom_obj->fcrw_rep_fop);
		ffid = &read_fop->c_rwv.crw_fid;
		/* Change the phase of FOM */
		fom->fo_phase = FOPH_COB_READ;
	}

	/* Find out the in-core fid from on-wire fid. */
	c2_io_fid_wire2mem(ffid, &fid);

	/* Map the given fid to find out corresponding stob id. */
	c2_io_fid2stob_map(&fid, &stobid);

	/* This is a transaction IO and should be a separate phase with
	   full fledged FOM. */
	result = fom->fo_domain->sd_ops->sdo_tx_make(fom->fo_domain, &tx);
	if (result != 0)
		return result;

	/* Allocate and find out the c2_stob object from given domain. */
	result = c2_stob_find(fom->fo_domain,
			(const struct c2_stob_id*)&stobid,
			&fom_obj->fcrw_stob);
	if (result != 0)
		return result;

	result = c2_stob_locate(fom_obj->fcrw_stob, &tx);
	if (result != 0) {
		c2_stob_put(fom_obj->fcrw_stob);
		return result;
	}

	/* Initialize the stob io routine. */
	c2_stob_io_init(&fom_obj->fcrw_st_io);

	/* Since the upper layer IO block size could differ with IO block size
	   of storage object, the block alignment and mapping is necesary. */
	bshift = fom_obj->fcrw_stob->so_op->sop_block_shift(fom_obj->fcrw_stob);
	bmask = (1 << bshift) - 1;

	if (fopt == &c2_fop_cob_writev_fopt) {
		/* Make an FOL transaction record. */
		result = c2_fop_fol_rec_add(fom_obj->fcrw_fop,
				fom->fo_fol, &tx.tx_dbtx);
		if (result != 0) {
			c2_stob_put(fom_obj->fcrw_stob);
			return result;
		}
	}

	/* Find out buffer address, offset and count required for stob io.
	   Due to existing limitations of kxdr wrapper over sunrpc, read reply
	   fop can not contain a vector, only a segment. Ideally, all IO fops
	   should carry an IO vector. Also with introduction of new rpc layer,
	   ioservice functionality has to be changed to handle the whole
	   vector, not just one segment. */
	if (fopt == &c2_fop_cob_writev_fopt) {
		write_seg = write_fop->c_rwv.crw_iovec.iv_segs;
		addr = c2_stob_addr_pack(write_seg->is_buf.ib_buf, bshift);
		count = write_seg->is_buf.ib_count;
		offset = write_seg->is_offset;
		fom_obj->fcrw_st_io.si_opcode = SIO_WRITE;
	} else {
		read_seg = read_fop->c_rwv.crw_iovec.iv_segs;

		/* Allocate the read buffer. */
		C2_ALLOC_ARR(rd_rep_fop->crr_iobuf.ib_buf,
				read_seg->is_buf.ib_count);
		if (rd_rep_fop->crr_iobuf.ib_buf == NULL) {
			c2_stob_put(fom_obj->fcrw_stob);
			return -ENOMEM;
		}
		addr = c2_stob_addr_pack(rd_rep_fop->crr_iobuf.ib_buf, bshift);
		count = read_seg->is_buf.ib_count;
		offset = read_seg->is_offset;
		fom_obj->fcrw_st_io.si_opcode = SIO_READ;
	}
	C2_ASSERT((offset & bmask) == 0);
	C2_ASSERT((count & bmask) == 0);

	count = count >> bshift;
	offset = offset >> bshift;

	stio = &fom_obj->fcrw_st_io;
	stio->si_user.div_vec.ov_vec.v_count = &count;
	stio->si_user.div_vec.ov_buf = &addr;

	stio->si_stob.iv_index = &offset;
	stio->si_stob.iv_vec.v_count = &count;

	/* Total number of segments in IO vector. */
	stio->si_user.div_vec.ov_vec.v_nr = 1;
	stio->si_stob.iv_vec.v_nr = 1;
	stio->si_flags = 0;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&stio->si_wait, &clink);

	result = c2_stob_io_launch(stio, fom_obj->fcrw_stob, &tx, NULL);
	if (result == 0)
		c2_chan_wait(&clink);
	else {
		c2_clink_del(&clink);
		c2_clink_fini(&clink);
		c2_stob_io_fini(stio);
		c2_stob_put(fom_obj->fcrw_stob);
		if (fopt == &c2_fop_cob_readv_fopt)
			c2_free(rd_rep_fop->crr_iobuf.ib_buf);
		return result;
	}

	/* Retrieve the status code and no of bytes read/written and
	   place it in respective reply FOP. */
	if (fopt == &c2_fop_cob_writev_fopt) {
		wr_rep_fop->cwr_rc = stio->si_rc;
		wr_rep_fop->cwr_count = stio->si_count << bshift;
	} else {
		rd_rep_fop->crr_rc = stio->si_rc;
		rd_rep_fop->crr_iobuf.ib_count = stio->si_count << bshift;
	}

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(stio);

	c2_stob_put(fom_obj->fcrw_stob);

	if (result != -EDEADLK)
		rc = c2_db_tx_commit(&tx.tx_dbtx);
	else
		rc = c2_db_tx_abort(&tx.tx_dbtx);

	if (rc != 0) {
		if (fopt == &c2_fop_cob_readv_fopt)
			c2_free(rd_rep_fop->crr_iobuf.ib_buf);
		return rc;
	}

	/* Send reply FOP. */
	c2_net_reply_post(fom->fo_fop_ctx->ft_service, fom_obj->fcrw_rep_fop,
			fom->fo_fop_ctx->fc_cookie);

	/* This goes into DONE phase */
	fom->fo_phase = FOPH_DONE;
	c2_io_fom_cob_rwv_fini(fom);
	return FSO_AGAIN;
}

/** Fini of read FOM object */
void c2_io_fom_cob_rwv_fini(struct c2_fom *fom)
{
	struct c2_io_fom_cob_rwv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_io_fom_cob_rwv, fcrw_gen);
	c2_free(fom_ctx);
}

/**
 * A dummy request handler API to handle incoming FOPs.
 * Actual reqh will be used in future.
 */
int c2_io_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
			 void *cookie, struct c2_fol *fol,
			 struct c2_stob_domain *dom)
{
	struct c2_fop_ctx	ctx;
	int			result = 0;
	struct c2_fom	       *fom = NULL;

	ctx.ft_service = s;
	ctx.fc_cookie  = cookie;

	/*
	 * Reqh generic phases will be run here that will do
	 * the standard actions like authentication, authorization,
	 * resource allocation, locking &c.
	 */

	/*
	 * This init function will allocate memory for a c2_io_fom_cob_rwv
	 * structure.
	 * It will find out the respective c2_fom_type object
	 * for the given c2_fop_type object using a mapping function
	 * and will embed the c2_fom_type object in c2_fop_type object.
	 * It will populate respective fields and do the necessary
	 * associations with fop and fom.
	 */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_domain = dom;
	fom->fo_fop_ctx = &ctx;
	fom->fo_fol = fol;

	/*
	 * Start the FOM.
	 */
	return fom->fo_ops->fo_state(fom);
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

