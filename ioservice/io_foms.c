/* -*- C -*- */

#include "fop/fop.h"
#include "io_foms.h"
#include "io_fops.h"
#include "stob/stob.h"
#include "stob/ut/fop_types.h"
#include <lib/errno.h>
#include <net/net.h>
#ifdef __KERNEL__
#include "io_fops_k.h"
#else
#include "io_fops_u.h"
#endif


/**
 * @addtogroup io_foms
 * @{
 */

struct c2_fom_type c2_fom_cob_readv_mopt = {
	.ft_ops = &cob_readv_type_ops,
};

struct c2_fom_type c2_fom_cob_writev_mopt = {
	.ft_ops = &cob_writev_type_ops,
};

/*
 *  An array of c2_fom_type structs for all possible FOMs.
 */
struct c2_fom_type *fom_types[] = {
	&c2_fom_cob_readv_mopt,
	&c2_fom_cob_writev_mopt,
};

/*
 * Find out the respective FOM type object (c2_fom_type)
 * from the given opcode.
 * This opcode is obtained from the FOP type (c2_fop_type->ft_code) 
 */

struct c2_fom_type* c2_fom_type_map(c2_fop_type_code_t code)
{
	//return fom_types[code - c2_io_service_fom_start_opcode];
	return fom_types[code - 14];
}

/* 
 * Create FOM context object for c2_fop_cob_writev FOP.
 */

int c2_fom_cob_writev_create(struct c2_fom_type *t, struct c2_fop *fop, 
		struct c2_fom **out)
{
	struct c2_fom *fom = *out;
	struct c2_fom_cob_writev *fom_ctx = NULL;

	C2_PRE(t != NULL);
	C2_PRE(out != NULL);
	C2_PRE(fop != NULL);

	fom_ctx = c2_alloc(sizeof(struct c2_fom_cob_writev));
	fom_ctx->fmcw_gen = *fom;

	/* 
	 * Initialize the FOM context object 
	 */
	fom_ctx->fmcw_fop = fop;
	fom_ctx->fmcw_domain = NULL;
	fom_ctx->fmcw_fop_ctx = NULL;
	fom_ctx->fmcw_stob = NULL;
	fom_ctx->fmcw_st_io = NULL;
	*out = &fom_ctx->fmcw_gen;
	return 0;
}

/*
 * Populate the FOM context object
 */

int c2_fom_cob_writev_ctx_populate(struct c2_fom *fom, 
		struct c2_stob_domain *d, struct c2_fop_ctx *fopctx,
		struct c2_fol *fol)
{
	struct c2_fom_cob_writev *fom_ctx;
	C2_PRE(d != NULL);
	C2_PRE(fopctx != NULL);
	C2_PRE(fom != NULL);

	fom_ctx = container_of(fom, struct c2_fom_cob_writev, fmcw_gen);
	//fom_ctx = (struct c2_fom_cob_writev*)fom;
	fom_ctx->fmcw_domain = d;
	fom_ctx->fmcw_fop_ctx = fopctx;
	fom_ctx->fmcw_fol = fol;
	return 0;
}

/* Fini of write FOM object */
void c2_fom_cob_writev_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_writev *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_writev, fmcw_gen);
	c2_free(fom_ctx);
}

/* Fini of read FOM object */
void c2_fom_cob_readv_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_readv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_readv, fmcr_gen);
	c2_free(fom_ctx);
}

/* 
 * Create FOM context object for c2_fop_cob_readv FOP.
 */

int c2_fom_cob_readv_create(struct c2_fom_type *t, struct c2_fop *fop, 
		struct c2_fom **out)
{
	struct c2_fom *fom = *out;
	struct c2_fom_cob_readv *fom_ctx = NULL;

	C2_PRE(t != NULL);
	C2_PRE(out != NULL);
	C2_PRE(fop != NULL);

	fom_ctx = c2_alloc(sizeof(struct c2_fom_cob_readv));
	fom_ctx->fmcr_gen = *fom;

	/* 
	 * Initialize the FOM context object 
	 */
	fom_ctx->fmcr_fop = fop;
	fom_ctx->fmcr_domain = NULL;
	fom_ctx->fmcr_fop_ctx = NULL;
	fom_ctx->fmcr_stob = NULL;
	fom_ctx->fmcr_st_io = NULL;
	*out = &fom_ctx->fmcr_gen;
	return 0;
}

/*
 * Populate the FOM context object
 */

int c2_fom_cob_readv_ctx_populate(struct c2_fom *fom, 
		struct c2_stob_domain *d, struct c2_fop_ctx *fopctx,
		struct c2_fol *fol)
{
	struct c2_fom_cob_readv *fom_ctx;
	C2_PRE(d != NULL);
	C2_PRE(fopctx != NULL);
	C2_PRE(fom != NULL);

	fom_ctx = container_of(fom, struct c2_fom_cob_readv, fmcr_gen);
	fom_ctx->fmcr_domain = d;
	fom_ctx->fmcr_fop_ctx = fopctx;
	fom_ctx->fmcr_fol = fol;
	return 0;
}

/*
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to 
 * change as per the future requirements.
 */

struct c2_stob_id *c2_fid2stob_map(struct c2_fop_file_fid *fid)
{
	struct c2_stob_id	*stobid;
	stobid = c2_alloc(sizeof(struct c2_stob_id));
	stobid->si_bits.u_hi = fid->f_container;
	stobid->si_bits.u_lo = fid->f_key;
	return stobid;
}

/**
 * <b> State Transition function for "write IO" operation
 *     that executes on data server. </b>
 *  - Submit the write IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */

int c2_fom_cob_write_state(struct c2_fom *fom) 
{
	struct c2_fop_file_fid 		*fid;
	struct c2_fom_cob_writev 	*ctx;
	struct c2_stob_id		*stobid;
	struct c2_dtx			tx;
	uint32_t			bshift;
	uint64_t			bmask;
	int 				result;
	void				*addr;
	c2_bcount_t			count;
	c2_bindex_t			offset;
	struct c2_clink			clink;
	int				rc;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop			*rep_fop;
	struct c2_fop_cob_writev_rep	*rep_fop_data;

	C2_PRE(fom != NULL);
	printf("c2_fom_cob_write_state entered.\n");
	rep_fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
	C2_ASSERT(rep_fop != NULL);
	rep_fop_data = c2_fop_data(rep_fop);

	/*
	 * Change the phase of FOM
	 */

	fom->fo_phase = FOPH_COB_WRITE;

	/*
	 * Since a c2_fom object is passed down to every FOM 
	 * state method, the context structure which is the parent structure 
	 * of the FOM is type casted from c2_fom.
	 * This is possible since the context structure has the 
	 * first element as FOM.
	 */

	ctx = container_of(fom, struct c2_fom_cob_writev, fmcw_gen);
	write_fop = c2_fop_data(ctx->fmcw_fop);

	/* 
	 * Map the given fid to find out corresponding stob id.
	 */

	printf("write: fop retrieved from c2_fop.\n");
	fid = &write_fop->fwr_fid;
	stobid = c2_fid2stob_map(fid);
	C2_ASSERT(stobid != NULL);

	/* 
	 * This is a transaction IO and should be a separate phase 
	 * with full fledged FOM. 
	 */

	result = ctx->fmcw_domain->sd_ops->sdo_tx_make(ctx->fmcw_domain, &tx);
	C2_ASSERT(result == 0);

	/*
	 * Allocate and find out the c2_stob object from given domain. 
	 */

	//ctx->fmcw_stob = c2_alloc(sizeof(struct c2_stob));
	result = c2_stob_find(ctx->fmcw_domain, stobid, &ctx->fmcw_stob);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(ctx->fmcw_stob, &tx);
	printf("write: stob found.\n");

	/* 
	 * Allocate and initialize stob io object 
	 */

	ctx->fmcw_st_io = c2_alloc(sizeof(struct c2_stob_io));
	c2_stob_io_init(ctx->fmcw_st_io);
	printf("write: stob io init.\n");

	/*
	 * Since the upper layer IO block size could differ
	 * with IO block size of storage object, the block
	 * alignment and mapping is necesary. 
	 */

	bshift = ctx->fmcw_stob->so_op->sop_block_shift(ctx->fmcw_stob);
	bmask = (1 << bshift) - 1;
	C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_offset & bmask) == 0);
	C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_addr.f_count & bmask) == 0);

	/*
	 * Memory allocation for necessary buffers in c2_diovec
	 * and c2_indexvec structures in c2_stob_io
	 */
	
	//c2_stob_io_vec_alloc(ctx->fmcw_st_io, write_fop->fwr_iovec.iov_count);
	printf("write: stob io vec allocated.\n");

	/*
	 * Populate the c2_diovec and c2_indexvec structures 
	 * from structure c2_stob_io.
	 * The for loop is used to transfer vectored IO segments
	 * from user space buffers to stob io buffers.
	 */

	//for(i = 0; i < write_fop->fwr_iovec.iov_count; ++i)
	//{
		addr = c2_stob_addr_pack(write_fop->fwr_iovec.
				iov_seg.f_addr.f_buf, bshift);
		/*if(i == 0) {
			count = write_fop->fwr_segsize - write_fop->fwr_iovec.iov_seg.f_addr.cfia_pgoff;
			offset = write_fop->fwr_iovec.iov_seg.f_addr.cfia_pgoff >> bshift;
		}
		else {
			count = write_fop->fwr_segsize;
			offset = 0;
		}*/
		count = write_fop->fwr_iovec.iov_seg.f_addr.f_count;
		offset = write_fop->fwr_iovec.iov_seg.f_offset;

		count = count >> bshift;
		offset = offset >> bshift;

		ctx->fmcw_st_io->si_user.div_vec.ov_vec.v_count = &count;
		ctx->fmcw_st_io->si_user.div_vec.ov_buf = &addr;

		ctx->fmcw_st_io->si_stob.iv_index = &offset;
		ctx->fmcw_st_io->si_stob.iv_vec.v_count = &count;
	//}

	/*
	 * Total number of segments in IO vector 
	 */

	ctx->fmcw_st_io->si_user.div_vec.ov_vec.v_nr = 1;
	ctx->fmcw_st_io->si_stob.iv_vec.v_nr = 1;

	ctx->fmcw_st_io->si_opcode = SIO_WRITE;
	ctx->fmcw_st_io->si_flags = 0;

	/* 
	 * A new clink is used to wait on the channel 
	 * from c2_stob_io.
	 */

	c2_clink_init(&clink, NULL);
	c2_clink_add(&ctx->fmcw_st_io->si_wait, &clink);

	/*
	 * Launch IO and wait for status. 
	 */

	printf("write: before io launch.\n");
	result = c2_stob_io_launch(ctx->fmcw_st_io, ctx->fmcw_stob, &tx, NULL);
	if(result == 0)
		c2_chan_wait(&clink);

	printf("write: after io launch. Result = %d\n", result);
	/*
	 * It is assumed that reply FOP is already allocated
	 * by the calling FOM.
	 */

	/*
	((struct c2_fop_cob_io_rep *)write_fop->fwr_foprep)->fwrr_rc = ctx->fmcw_st_io->si_rc;
	((struct c2_fop_cob_io_rep *)write_fop->fwr_foprep)->fwrr_count = ctx->fmcw_st_io->si_count << bshift;
	*/
	rep_fop_data->fwrr_rc = ctx->fmcw_st_io->si_rc;
	rep_fop_data->fwrr_count = ctx->fmcw_st_io->si_count << bshift;;
	printf("write: Reply FOP populated\n");
	printf("write: %lu bytes written\n", rep_fop_data->fwrr_count);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	//c2_stob_io_vec_free(ctx->fmcw_st_io);
	c2_stob_io_fini(ctx->fmcw_st_io);
	c2_free(ctx->fmcw_st_io);
	ctx->fmcw_st_io = NULL;

	c2_stob_put(ctx->fmcw_stob);

	if(result != -EDEADLK)	{
		/* XXX Is this an IO ? */
		result = c2_fop_fol_rec_add(ctx->fmcw_fop, 
				ctx->fmcw_fol, &tx.tx_dbtx);
		C2_ASSERT(result == 0);
		rc = c2_db_tx_commit(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
	}
	else {
		//fprintf(stderr, "Deadlock, aborting write.\n");
		rc = c2_db_tx_abort(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
		/* This should go into FAILURE phase */
		ctx->fmcw_gen.fo_phase = FOPH_FAILED;
		return FSO_AGAIN;
	}

	/*
	 * Send reply FOP
	 */

	printf("write: Before post reply\n");
	//c2_net_reply_post(ctx->fmcw_fop_ctx->ft_service, ((struct c2_fop*)write_fop->fwr_foprep), ctx->fmcw_fop_ctx->fc_cookie);
	c2_net_reply_post(ctx->fmcw_fop_ctx->ft_service, rep_fop, ctx->fmcw_fop_ctx->fc_cookie);
	printf("write: after post reply\n");

	/* This goes into DONE phase */
	ctx->fmcw_gen.fo_phase = FOPH_DONE;
	c2_fom_cob_writev_fini(&ctx->fmcw_gen);
	printf("fom finished\n");
	return FSO_AGAIN;
}

/**
 * <b> State Transition function for "read IO" operation
 *     that executes on data server. </b>
 *  - Submit the read IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */

int c2_fom_cob_read_state(struct c2_fom *fom) 
{
	struct c2_fop_file_fid 		*fid;
	struct c2_fom_cob_readv 	*ctx;
	struct c2_stob_id		*stobid;
	struct c2_dtx			tx;
	uint32_t			bshift;
	uint64_t			bmask;
	int 				result;
	void				*addr;
	c2_bcount_t			count;
	c2_bindex_t			offset;
	struct c2_clink			clink;
	int				rc;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop			*rep_fop;
	struct c2_fop_cob_readv_rep	*rep_fop_data;

	C2_PRE(fom != NULL);
	printf("c2_fom_cob_read_state entered.\n");
	rep_fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
	C2_ASSERT(rep_fop != NULL);
	rep_fop_data = c2_fop_data(rep_fop);

	/*
	 * Change the phase of FOM
	 */

	fom->fo_phase = FOPH_COB_READ;
	/*
	 * Since a c2_fom object is passed down to every FOM 
	 * state method, the context structure which is the parent structure 
	 * of the FOM is type casted from c2_fom.
	 * This is possible since the context structure has the 
	 * first element as FOM.
	 */

	ctx = container_of(fom, struct c2_fom_cob_readv, fmcr_gen);
	read_fop = c2_fop_data(ctx->fmcr_fop);

	/* 
	 * Map the given fid to find out corresponding stob id.
	 */

	printf("read: fop retrieved from c2_fop.\n");
	fid = &read_fop->frd_fid;
	stobid = c2_fid2stob_map(fid);
	C2_ASSERT(stobid != NULL);

	/* 
	 * This is a transaction IO and should be a separate phase 
	 * with full fledged FOM. 
	 */

	result = ctx->fmcr_domain->sd_ops->sdo_tx_make(ctx->fmcr_domain, &tx);
	C2_ASSERT(result == 0);

	/*
	 * Allocate and find out the c2_stob object from given domain. 
	 */

	//ctx->fmcr_stob = c2_alloc(sizeof(struct c2_stob));
	result = c2_stob_find(ctx->fmcr_domain, stobid, &ctx->fmcr_stob);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(ctx->fmcr_stob, &tx);
	printf("read: stob found.\n");

	/* 
	 * Allocate and initialize stob io object 
	 */

	ctx->fmcr_st_io = c2_alloc(sizeof(struct c2_stob_io));
	c2_stob_io_init(ctx->fmcr_st_io);
	printf("read: stob io init.\n");

	/*
	 * Since the upper layer IO block size could differ
	 * with IO block size of storage object, the block
	 * alignment and mapping is necesary. 
	 */

	bshift = ctx->fmcr_stob->so_op->sop_block_shift(ctx->fmcr_stob);
	bmask = (1 << bshift) - 1;
	C2_ASSERT((read_fop->frd_ioseg.f_offset & bmask) == 0);
	C2_ASSERT((read_fop->frd_ioseg.f_count & bmask) == 0);

	/*
	 * Memory allocation for necessary buffers in c2_diovec
	 * and c2_indexvec structures in c2_stob_io
	 */
	
	printf("read: stob io vec allocated.\n");
	//c2_stob_io_vec_alloc(ctx->fmcr_st_io, read_fop->frd_iovec.iov_count);

	/*
	 * Populate the c2_diovec and c2_indexvec structures 
	 * from structure c2_stob_io.
	 * The for loop is used to transfer vectored IO segments
	 * from user space buffers to stob io buffers.
	 */

	//for(i = 0; i < read_fop->frd_iovec.iov_count; ++i)
	//{
	C2_ALLOC_ARR(rep_fop_data->frdr_buf.f_buf, read_fop->frd_ioseg.f_count);
	C2_ASSERT(rep_fop_data->frdr_buf.f_buf != NULL);

		addr = c2_stob_addr_pack(rep_fop_data->frdr_buf.f_buf, bshift);

		/*if(i == 0) {
			count = read_fop->frd_segsize - read_fop->frd_iovec.iov_seg.f_addr.cfia_pgoff;
			offset = read_fop->frd_iovec.iov_seg.f_addr.cfia_pgoff >> bshift;
		}
		else {
			count = read_fop->frd_segsize;
			offset = 0;
		}*/

		count = read_fop->frd_ioseg.f_count;
		offset = read_fop->frd_ioseg.f_offset;

		count = count >> bshift;
		offset = offset >> bshift;

		ctx->fmcr_st_io->si_user.div_vec.ov_vec.v_count = &count;
		ctx->fmcr_st_io->si_user.div_vec.ov_buf = &addr;

		ctx->fmcr_st_io->si_stob.iv_index = &offset;
		ctx->fmcr_st_io->si_stob.iv_vec.v_count = &count;
	//}

	/*
	 * Total number of segments in IO vector 
	 */

	ctx->fmcr_st_io->si_user.div_vec.ov_vec.v_nr = 1;
	ctx->fmcr_st_io->si_stob.iv_vec.v_nr = 1;

	ctx->fmcr_st_io->si_opcode = SIO_READ;
	ctx->fmcr_st_io->si_flags = 0;

	/* 
	 * A new clink is used to wait on the channel 
	 * from c2_stob_io.
	 */

	c2_clink_init(&clink, NULL);
	c2_clink_add(&ctx->fmcr_st_io->si_wait, &clink);

	/*
	 * Launch IO and wait for status. 
	 */

	printf("read: before io launch.\n");
	result = c2_stob_io_launch(ctx->fmcr_st_io, ctx->fmcr_stob, &tx, NULL);
	if(result == 0)
		c2_chan_wait(&clink);
	printf("read: after io launch. Result = %d\n", result);

	/*
	 * It is assumed that reply FOP is already allocated
	 * by the calling FOM.
	 */

	/*
	io_rep = c2_fop_data((struct c2_fop*)read_fop->frd_foprep);
	((struct c2_fop_cob_io_rep*)io_rep)->fwrr_rc = ctx->fmcr_st_io->si_rc;
	((struct c2_fop_cob_io_rep*)io_rep)->fwrr_count = ctx->fmcr_st_io->si_count << bshift;
	*/
	rep_fop_data->frdr_rc = ctx->fmcr_st_io->si_rc;
	rep_fop_data->frdr_buf.f_count = ctx->fmcr_st_io->si_count << bshift;;
	printf("read: Reply FOP populated\n");

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	//c2_stob_io_vec_free(ctx->fmcr_st_io);
	c2_stob_io_fini(ctx->fmcr_st_io);
	c2_free(ctx->fmcr_st_io);
	ctx->fmcr_st_io = NULL;

	c2_stob_put(ctx->fmcr_stob);

	if(result != -EDEADLK)	{
		rc = c2_db_tx_commit(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
	}
	else {
		rc = c2_db_tx_abort(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
		/* This should go into FAILURE phase */
		ctx->fmcr_gen.fo_phase = FOPH_FAILED;
		return FSO_AGAIN;
	}

	/*
	 * Send reply FOP
	 */

	printf("read: Before post reply\n");
	//c2_net_reply_post(ctx->fmcr_fop_ctx->ft_service, ((struct c2_fop*)read_fop->frd_foprep), ctx->fmcr_fop_ctx->fc_cookie);
	c2_net_reply_post(ctx->fmcr_fop_ctx->ft_service, rep_fop, ctx->fmcr_fop_ctx->fc_cookie);
	printf("read: After post reply\n");

	/* This goes into DONE phase */
	ctx->fmcr_gen.fo_phase = FOPH_DONE;
	c2_fom_cob_readv_fini(&ctx->fmcr_gen);
	return FSO_AGAIN;
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

