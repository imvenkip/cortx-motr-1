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

/* Readv specific FOM type operations vector. */
struct c2_fom_type c2_fom_cob_readv_mopt = {
	.ft_ops = &cob_readv_type_ops,
};

/* Writev specific FOM type operations vector. */
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
	return fom_types[code - c2_io_service_fom_start_opcode];
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

	/* Initialize the FOM context object. */
	fom_ctx->fmcw_fop = fop;
	fom_ctx->fmcw_domain = NULL;
	fom_ctx->fmcw_fop_ctx = NULL;
	fom_ctx->fmcw_stob = NULL;
	fom_ctx->fmcw_st_io = NULL;
	c2_free(*out);
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
	fom_ctx->fmcw_domain = d;
	fom_ctx->fmcw_fop_ctx = fopctx;
	fom_ctx->fmcw_fol = fol;
	return 0;
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
	result = c2_stob_find(ctx->fmcw_domain, stobid, &ctx->fmcw_stob);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(ctx->fmcw_stob, &tx);

	/* 
	 * Allocate and initialize stob io object 
	 */
	ctx->fmcw_st_io = c2_alloc(sizeof(struct c2_stob_io));
	c2_stob_io_init(ctx->fmcw_st_io);

	/*
	 * Since the upper layer IO block size could differ
	 * with IO block size of storage object, the block
	 * alignment and mapping is necesary. 
	 */
	bshift = ctx->fmcw_stob->so_op->sop_block_shift(ctx->fmcw_stob);
	bmask = (1 << bshift) - 1;
	C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_offset & bmask) == 0);
	C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_addr.f_count & bmask) == 0);

	addr = c2_stob_addr_pack(write_fop->fwr_iovec.
			iov_seg.f_addr.f_buf, bshift);

	count = write_fop->fwr_iovec.iov_seg.f_addr.f_count;
	offset = write_fop->fwr_iovec.iov_seg.f_offset;

	count = count >> bshift;
	offset = offset >> bshift;

	ctx->fmcw_st_io->si_user.div_vec.ov_vec.v_count = &count;
	ctx->fmcw_st_io->si_user.div_vec.ov_buf = &addr;

	ctx->fmcw_st_io->si_stob.iv_index = &offset;
	ctx->fmcw_st_io->si_stob.iv_vec.v_count = &count;

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
	result = c2_stob_io_launch(ctx->fmcw_st_io, ctx->fmcw_stob, &tx, NULL);
	if(result == 0)
		c2_chan_wait(&clink);

	rep_fop_data->fwrr_rc = ctx->fmcw_st_io->si_rc;
	rep_fop_data->fwrr_count = ctx->fmcw_st_io->si_count << bshift;;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(ctx->fmcw_st_io);
	c2_free(ctx->fmcw_st_io);
	ctx->fmcw_st_io = NULL;

	c2_stob_put(ctx->fmcw_stob);

	if(result != -EDEADLK)	{
		result = c2_fop_fol_rec_add(ctx->fmcw_fop, 
				ctx->fmcw_fol, &tx.tx_dbtx);
		C2_ASSERT(result == 0);
		rc = c2_db_tx_commit(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
	}
	else {
		rc = c2_db_tx_abort(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
		/* This should go into FAILURE phase */
		ctx->fmcw_gen.fo_phase = FOPH_FAILED;
		return FSO_AGAIN;
	}

	/*
	 * Send reply FOP
	 */
	c2_net_reply_post(ctx->fmcw_fop_ctx->ft_service, rep_fop, ctx->fmcw_fop_ctx->fc_cookie);

	/* This goes into DONE phase */
	ctx->fmcw_gen.fo_phase = FOPH_DONE;
	c2_fom_cob_writev_fini(&ctx->fmcw_gen);
	return FSO_AGAIN;
}

/* Fini of write FOM object */
void c2_fom_cob_writev_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_writev *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_writev, fmcw_gen);
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

	/* Initialize the FOM context object. */
	fom_ctx->fmcr_fop = fop;
	fom_ctx->fmcr_domain = NULL;
	fom_ctx->fmcr_fop_ctx = NULL;
	fom_ctx->fmcr_stob = NULL;
	fom_ctx->fmcr_st_io = NULL;
	c2_free(*out);
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
	fid = &read_fop->frd_fid;
	stobid = c2_fid2stob_map(fid);
	C2_ASSERT(stobid != NULL);

	/* 
	 * This is a transaction IO and should be a separate phase 
	 * with full fledged FOM. 
	 */
	result = ctx->fmcr_domain->sd_ops->sdo_tx_make(ctx->fmcr_domain, &tx);
	C2_ASSERT(result == 0);

	result = c2_stob_find(ctx->fmcr_domain, stobid, &ctx->fmcr_stob);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(ctx->fmcr_stob, &tx);

	/* 
	 * Allocate and initialize stob io object 
	 */
	ctx->fmcr_st_io = c2_alloc(sizeof(struct c2_stob_io));
	c2_stob_io_init(ctx->fmcr_st_io);

	/*
	 * Since the upper layer IO block size could differ
	 * with IO block size of storage object, the block
	 * alignment and mapping is necesary. 
	 */
	bshift = ctx->fmcr_stob->so_op->sop_block_shift(ctx->fmcr_stob);
	bmask = (1 << bshift) - 1;
	C2_ASSERT((read_fop->frd_ioseg.f_offset & bmask) == 0);
	C2_ASSERT((read_fop->frd_ioseg.f_count & bmask) == 0);

	C2_ALLOC_ARR(rep_fop_data->frdr_buf.f_buf, read_fop->frd_ioseg.f_count);
	C2_ASSERT(rep_fop_data->frdr_buf.f_buf != NULL);

	addr = c2_stob_addr_pack(rep_fop_data->frdr_buf.f_buf, bshift);

	count = read_fop->frd_ioseg.f_count;
	offset = read_fop->frd_ioseg.f_offset;

	count = count >> bshift;
	offset = offset >> bshift;

	ctx->fmcr_st_io->si_user.div_vec.ov_vec.v_count = &count;
	ctx->fmcr_st_io->si_user.div_vec.ov_buf = &addr;

	ctx->fmcr_st_io->si_stob.iv_index = &offset;
	ctx->fmcr_st_io->si_stob.iv_vec.v_count = &count;

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
	result = c2_stob_io_launch(ctx->fmcr_st_io, ctx->fmcr_stob, &tx, NULL);
	if(result == 0)
		c2_chan_wait(&clink);

	rep_fop_data->frdr_rc = ctx->fmcr_st_io->si_rc;
	rep_fop_data->frdr_buf.f_count = ctx->fmcr_st_io->si_count << bshift;;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

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
	c2_net_reply_post(ctx->fmcr_fop_ctx->ft_service, rep_fop, ctx->fmcr_fop_ctx->fc_cookie);

	/* This goes into DONE phase */
	ctx->fmcr_gen.fo_phase = FOPH_DONE;
	c2_fom_cob_readv_fini(&ctx->fmcr_gen);
	return FSO_AGAIN;
}

/* Fini of read FOM object */
void c2_fom_cob_readv_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_readv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_readv, fmcr_gen);
	c2_free(fom_ctx);
}

/*
 * A dummy request handler API to handle incoming FOPs.
 * Actual reqh will be used in future.
 */
int c2_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
		void *cookie, struct c2_fol *fol, struct c2_stob_domain *dom)
{
	struct c2_fop_ctx ctx;
	int 		  result = 0;
	struct c2_fom	  *fom = NULL;

	ctx.ft_service = s;
	ctx.fc_cookie  = cookie;

	/*
	 * Reqh generic phases will be run here that will do 
	 * the standard actions like authentication, authorization,
	 * resource allocation, locking &c.
	 */

	/* 
	 * This init function will allocate memory for a c2_fom 
	 * structure. 
	 * It will find out the respective c2_fom_type object
	 * for the given c2_fop_type object using a mapping function
	 * and will embed the c2_fom_type object in c2_fop_type object.
	 */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	/*
	 * This create function will create a type specific FOM
	 * structure which primarily includes the context.
	 */
	fom->fo_type->ft_ops->fto_create(&fop->f_type->ft_fom_type, fop, &fom);
	/*
	 * Populate the FOM context object with whatever is 
	 * needed from server side.
	 */
	fom->fo_type->ft_ops->fto_populate(fom, dom, &ctx, fol);
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

