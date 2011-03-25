/* -*- C -*- */
#include "fop/fop.h"
#include "io_foms.h"
#include "io_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "net/net.h"

#ifdef __KERNEL__
#include "io_fops_k.h"
#else
#include "io_fops_u.h"
#endif

#include "fop/fop_format_def.h"
#include "ioservice/io_fops.ff"

/**
 * @addtogroup io_foms
 * @{
 */

#ifndef __KERNEL__

/** Readv specific FOM type operations vector. */
struct c2_fom_type c2_fom_cob_readv_mopt = {
	.ft_ops = &cob_readv_type_ops,
};

/** Writev specific FOM type operations vector. */
struct c2_fom_type c2_fom_cob_writev_mopt = {
	.ft_ops = &cob_writev_type_ops,
};

/**
 *  An array of c2_fom_type structs for all possible FOMs.
 */
struct c2_fom_type *fom_types[] = {
	&c2_fom_cob_readv_mopt,
	&c2_fom_cob_writev_mopt,
};

/**
 * Find out the respective FOM type object (c2_fom_type)
 * from the given opcode.
 * This opcode is obtained from the FOP type (c2_fop_type->ft_code) 
 */
struct c2_fom_type *c2_fom_type_map(c2_fop_type_code_t code)
{
	C2_PRE(IS_IN_ARRAY((code - c2_io_service_readv_opcode), fom_types));
	return fom_types[code - c2_io_service_readv_opcode];
}

/**
 * Function to map given fid to corresponding Component object id(in turn,
 * storage object id).
 * Currently, this mapping is identity. But it is subject to 
 * change as per the future requirements.
 */
/*void c2_fid2stob_map(struct c2_fid *in, struct c2_stob_id *out)
{
	out->si_bits.u_hi = in->f_container;
	out->si_bits.u_lo = in->f_key;
}*/
struct c2_stob_id *c2_fid2stob_map(struct c2_fid *fid)
{
       struct c2_stob_id       *stobid;
       stobid = c2_alloc(sizeof(struct c2_stob_id));
       C2_ASSERT(stobid != NULL);
       stobid->si_bits.u_hi = fid->f_container;
       stobid->si_bits.u_lo = fid->f_key;
       return stobid;
}

/**
 * Function to map the on-wire FOP format to in-core FOP format.
 */
/*void c2_fid_wire2mem(struct c2_fop_file_fid *in, struct c2_fid *out)
{
	out->f_container = in->f_seq;
	out->f_key = in->f_oid;
}*/
struct c2_fid *c2_fid_wire2mem(struct c2_fop_file_fid *ffid)
{
       struct c2_fid *fid = NULL;

       fid = c2_alloc(sizeof(struct c2_fid));
       C2_ASSERT(fid != NULL);
       fid->f_container = ffid->f_seq;
       fid->f_key = ffid->f_oid;
       return fid;
}

/** 
 * Create FOM context object for c2_fop_cob_writev FOP.
 */
int c2_fom_cob_writev_create(struct c2_fom_type *t, struct c2_fop *fop, 
			     struct c2_fom **out)
{
	struct c2_fom *fom = *out;
	struct c2_fom_cob_rwv *fom_ctx = NULL;

	C2_PRE(t != NULL);
	C2_PRE(out != NULL);
	C2_PRE(fop != NULL);

	fom_ctx = c2_alloc(sizeof(struct c2_fom_cob_rwv));
	C2_ASSERT(fom_ctx != NULL);
	fom_ctx->fcrw_gen = *fom;

	/* Initialize the FOM context object. */
	fom_ctx->fcrw_fop = fop;
	fom_ctx->fcrw_domain = NULL;
	fom_ctx->fcrw_fop_ctx = NULL;
	fom_ctx->fcrw_stob = NULL;
	fom_ctx->fcrw_st_io = NULL;
	c2_free(*out);
	*out = &fom_ctx->fcrw_gen;
	return 0;
}

/**
 * Common state function for readv and writev FOMs.
 */
int c2_fom_cob_rw_state(struct c2_fom *fom, int rw)
{
	struct c2_fop_file_fid		*ffid;
	//struct c2_fid 			fid;
	struct c2_fid 			*fid;
	struct c2_fom_cob_rwv 		*ctx;
	//struct c2_stob_id		stobid;
	struct c2_stob_id		*stobid;
	struct c2_dtx			 tx;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int 				 result;
	void				*addr;
	c2_bcount_t			 count;
	c2_bindex_t			 offset;
	struct c2_clink			 clink;
	int				 rc;
	struct c2_fop_cob_writev	*write_fop;
	struct c2_fop_cob_readv		*read_fop;
	struct c2_fop			*rep_fop;
	struct c2_fop_cob_writev_rep	*wr_rep_fop;
	struct c2_fop_cob_readv_rep	*rd_rep_fop;

	C2_PRE(fom != NULL);

	if(rw == WRITE) 
	{
		rep_fop = c2_fop_alloc(&c2_fop_cob_writev_rep_fopt, NULL);
		C2_ASSERT(rep_fop != NULL);
		wr_rep_fop = c2_fop_data(rep_fop);
		/*
		 * Change the phase of FOM
		 */
		fom->fo_phase = FOPH_COB_WRITE;
		printf("write reply FOP allocated.\n");
	}
	else 
	{
		rep_fop = c2_fop_alloc(&c2_fop_cob_readv_rep_fopt, NULL);
		C2_ASSERT(rep_fop != NULL);
		rd_rep_fop = c2_fop_data(rep_fop);
		/*
		 * Change the phase of FOM
		 */
		fom->fo_phase = FOPH_COB_READ;
		printf("read reply FOP allocated.\n");
	}

	/*
	 * Since a c2_fom object is passed down to every FOM 
	 * state method, the context structure which is the 
	 * parent structure of the FOM is type casted from c2_fom.
	 */
	ctx = container_of(fom, struct c2_fom_cob_rwv, fcrw_gen);
	if(rw == WRITE)
	{
		write_fop = c2_fop_data(ctx->fcrw_fop);
		ffid = &write_fop->fwr_fid;
		printf("write FOL record added.\n");
	}
	else
	{
		read_fop = c2_fop_data(ctx->fcrw_fop);
		ffid = &read_fop->frd_fid;
	}

	/* Find out the in-core fid from on-wire fid. */
	//c2_fid_wire2mem(ffid, &fid);
	fid = c2_fid_wire2mem(ffid);

	/* 
	 * Map the given fid to find out corresponding stob id.
	 */
	//c2_fid2stob_map(&fid, &stobid);
	stobid = c2_fid2stob_map(fid);

	/* 
	 * This is a transaction IO and should be a separate phase 
	 * with full fledged FOM. 
	 */
	result = ctx->fcrw_domain->sd_ops->sdo_tx_make(ctx->fcrw_domain, &tx);
	C2_ASSERT(result == 0);

	if(rw == WRITE)
	{
		/* 
		 * Make an FOL transaction record.
		 */
		result = c2_fop_fol_rec_add(ctx->fcrw_fop, 
				ctx->fcrw_fol, &tx.tx_dbtx);
		C2_ASSERT(result == 0);
	}

	/*
	 * Allocate and find out the c2_stob object from given domain. 
	 */
	result = c2_stob_find(ctx->fcrw_domain, stobid, &ctx->fcrw_stob);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(ctx->fcrw_stob, &tx);
	C2_ASSERT(result == 0);
	printf("Stob found and located..\n");

	/* 
	 * Allocate and initialize stob io object 
	 */
	ctx->fcrw_st_io = c2_alloc(sizeof(struct c2_stob_io));
	C2_ASSERT(ctx->fcrw_st_io);
	c2_stob_io_init(ctx->fcrw_st_io);
	printf("stob io initiated..\n");

	/*
	 * Since the upper layer IO block size could differ
	 * with IO block size of storage object, the block
	 * alignment and mapping is necesary. 
	 */
	bshift = ctx->fcrw_stob->so_op->sop_block_shift(ctx->fcrw_stob);
	bmask = (1 << bshift) - 1;
	if(rw == WRITE)
	{
		C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_offset & bmask) == 0);
		C2_ASSERT((write_fop->fwr_iovec.iov_seg.f_buf.f_count & bmask) == 0);
	}
	else 
	{
		C2_ASSERT((read_fop->frd_ioseg.f_offset & bmask) == 0);
		C2_ASSERT((read_fop->frd_ioseg.f_count & bmask) == 0);

		C2_ALLOC_ARR(rd_rep_fop->frdr_buf.f_buf, read_fop->frd_ioseg.f_count);     
		C2_ASSERT(rd_rep_fop->frdr_buf.f_buf != NULL);
		printf("read buffer allocated..\n");
	}

	if(rw == WRITE)
	{
		addr = c2_stob_addr_pack(write_fop->fwr_iovec.
					 iov_seg.f_buf.f_buf, bshift);
		count = write_fop->fwr_iovec.iov_seg.f_buf.f_count;
		offset = write_fop->fwr_iovec.iov_seg.f_offset;
		ctx->fcrw_st_io->si_opcode = SIO_WRITE;
	}
	else 
	{
		addr = c2_stob_addr_pack(rd_rep_fop->frdr_buf.f_buf, 
					 bshift);
		count = read_fop->frd_ioseg.f_count;
		offset = read_fop->frd_ioseg.f_offset; 
		ctx->fcrw_st_io->si_opcode = SIO_READ;
	}
	printf("offset and count calculated.\n");

	count = count >> bshift;
	offset = offset >> bshift;

	ctx->fcrw_st_io->si_user.div_vec.ov_vec.v_count = &count;
	ctx->fcrw_st_io->si_user.div_vec.ov_buf = &addr;

	ctx->fcrw_st_io->si_stob.iv_index = &offset;
	ctx->fcrw_st_io->si_stob.iv_vec.v_count = &count;

	/*
	 * Total number of segments in IO vector 
	 */
	ctx->fcrw_st_io->si_user.div_vec.ov_vec.v_nr = 1;
	ctx->fcrw_st_io->si_stob.iv_vec.v_nr = 1;

	ctx->fcrw_st_io->si_flags = 0;
	printf("stob io vector populated.\n");

	/* 
	 * A new clink is used to wait on the channel 
	 * from c2_stob_io.
	 */
	c2_clink_init(&clink, NULL);
	c2_clink_add(&ctx->fcrw_st_io->si_wait, &clink);

	/*
	 * Launch IO and wait for status. 
	 */
	printf("before io launch.\n");
	result = c2_stob_io_launch(ctx->fcrw_st_io, ctx->fcrw_stob, &tx, NULL);
	if(result == 0)
		c2_chan_wait(&clink);

	if(rw == WRITE)
	{
		wr_rep_fop->fwrr_rc = ctx->fcrw_st_io->si_rc;
		wr_rep_fop->fwrr_count = ctx->fcrw_st_io->si_count << bshift;;
	}
	else
	{
		rd_rep_fop->frdr_rc = ctx->fcrw_st_io->si_rc;
		rd_rep_fop->frdr_buf.f_count = ctx->fcrw_st_io->si_count << bshift;
	}
	printf("after io launch.\n");

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(ctx->fcrw_st_io);
	c2_free(ctx->fcrw_st_io);
	ctx->fcrw_st_io = NULL;

	c2_stob_put(ctx->fcrw_stob);

	if(result != -EDEADLK)	{
		rc = c2_db_tx_commit(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
	}
	else {
		rc = c2_db_tx_abort(&tx.tx_dbtx);
		C2_ASSERT(rc == 0);
		/* This should go into FAILURE phase */
		ctx->fcrw_gen.fo_phase = FOPH_FAILED;
		return FSO_AGAIN;
	}
	printf("transaction committed/aborted..\n");

	/*
	 * Send reply FOP
	 */
	printf("before post reply.\n");
	c2_net_reply_post(ctx->fcrw_fop_ctx->ft_service, rep_fop, ctx->fcrw_fop_ctx->fc_cookie);
	printf("after post reply.\n");

	/* This goes into DONE phase */
	ctx->fcrw_gen.fo_phase = FOPH_DONE;
	if(rw == WRITE)
		c2_fom_cob_writev_fini(&ctx->fcrw_gen);
	else 
		c2_fom_cob_readv_fini(&ctx->fcrw_gen);
	printf("after read/write fini.\n");
	return FSO_AGAIN;
}

/**
 * <b> State Transition function for "write IO" operation
 *     that executes on data server. </b>
 *  - Submit the write IO request to the corresponding cob.
 *  - Send reply FOP to client.
 */
int c2_fom_cob_write_state(struct c2_fom *fom) 
{
	int 				 result;
	result = c2_fom_cob_rw_state(fom, WRITE);
	return result; 
}

/** Fini of write FOM object */
void c2_fom_cob_writev_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_rwv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_rwv, fcrw_gen);
	c2_free(fom_ctx);
}

/** 
 * Create FOM context object for c2_fop_cob_readv FOP.
 */
int c2_fom_cob_readv_create(struct c2_fom_type *t, struct c2_fop *fop, 
			    struct c2_fom **out)
{
	struct c2_fom *fom = *out;
	struct c2_fom_cob_rwv *fom_ctx = NULL;

	C2_PRE(t != NULL);
	C2_PRE(out != NULL);
	C2_PRE(fop != NULL);

	fom_ctx = c2_alloc(sizeof(struct c2_fom_cob_rwv));
	C2_ASSERT(fom_ctx != NULL);
	fom_ctx->fcrw_gen = *fom;

	/* Initialize the FOM context object. */
	fom_ctx->fcrw_fop = fop;
	fom_ctx->fcrw_domain = NULL;
	fom_ctx->fcrw_fop_ctx = NULL;
	fom_ctx->fcrw_stob = NULL;
	fom_ctx->fcrw_st_io = NULL;
	c2_free(*out);
	*out = &fom_ctx->fcrw_gen;
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
	int 				 result;
	result = c2_fom_cob_rw_state(fom, READ);
	return  result;
}

/** Fini of read FOM object */
void c2_fom_cob_readv_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_rwv *fom_ctx;

	fom_ctx = container_of(fom, struct c2_fom_cob_rwv, fcrw_gen);
	c2_free(fom_ctx);
}

/**
 * A dummy request handler API to handle incoming FOPs.
 * Actual reqh will be used in future.
 */
int c2_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
			 void *cookie, struct c2_fol *fol, 
			 struct c2_stob_domain *dom)
{
	struct c2_fop_ctx ctx;
	struct c2_fom_cob_rwv	*fom_ctx;
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
	fom_ctx = container_of(fom, struct c2_fom_cob_rwv, fcrw_gen);
	fom_ctx->fcrw_domain = dom;
	fom_ctx->fcrw_fop_ctx = &ctx;
	fom_ctx->fcrw_fol = fol;
	/* 
	 * Start the FOM.
	 */
	return fom->fo_ops->fo_state(fom);
}
#endif

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

