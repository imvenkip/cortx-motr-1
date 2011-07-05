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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <err.h>

#include "lib/cdefs.h"
#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/getopts.h"
#include "lib/arith.h"
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/queue.h"
#include "lib/chan.h"
#include "lib/processor.h"

#include "colibri/init.h"
#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "stob/linux.h"

#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "reqh/reqh_fops_k.h"
# include "fom_io_k.h"
#else

#include "reqh/reqh_fops_u.h"
#include "fom_io_u.h"
#endif

#include "fom_io.ff"
#include "reqh/reqh_fops.ff"

/**
   @addtogroup reqh
   @{
 */

/**** server side structures and objects ****/
enum {
	PORT = 10001
};

typedef unsigned long long U64;
static struct c2_stob_domain *sdom;
struct c2_net_domain	ndom;
static struct c2_fol	fol;

/* global reqh object */
struct c2_reqh		reqh;

/* structure to hold c2_net_call and c2_clink, for network communication */
struct reqh_net_call {
	struct c2_net_call	ncall;
	struct c2_clink		rclink;
};

int reply;
int c2_io_fom_init(struct c2_fop *fop, struct c2_fom **m);
int c2_io_fom_fail(struct c2_fom *fom);
extern bool c2_fom_invariant(struct c2_fom *fom);

/**
 * fop operation structures for corresponding fops.
 */
static struct c2_fop_type_ops write_fop_ops = {
	.fto_fom_init = c2_io_fom_init,
	.fto_execute = NULL,
};

static struct c2_fop_type_ops read_fop_ops = {
	.fto_fom_init = c2_io_fom_init,
	.fto_execute = NULL,
};

static struct c2_fop_type_ops create_fop_ops = {
	.fto_fom_init = c2_io_fom_init,
	.fto_execute = NULL,
};

static struct c2_fop_type_ops quit_fop_ops = {
	.fto_fom_init = c2_io_fom_init,
	.fto_execute = NULL,
};

C2_FOP_TYPE_DECLARE(c2_fom_io_create, "create", 10, &create_fop_ops);
C2_FOP_TYPE_DECLARE(c2_fom_io_write, "write", 11, &write_fop_ops);
C2_FOP_TYPE_DECLARE(c2_fom_io_read, "read", 12, &read_fop_ops);
C2_FOP_TYPE_DECLARE(c2_fom_io_quit, "quit", 13, &quit_fop_ops);

C2_FOP_TYPE_DECLARE(c2_fom_io_create_rep, "create reply", 21, NULL);
C2_FOP_TYPE_DECLARE(c2_fom_io_write_rep, "write reply", 22, NULL);
C2_FOP_TYPE_DECLARE(c2_fom_io_read_rep, "read reply",  23, NULL);

static struct c2_fop_type *fops[] = {
	&c2_fom_io_create_fopt,
	&c2_fom_io_write_fopt,
	&c2_fom_io_read_fopt,
	&c2_fom_io_quit_fopt,

	&c2_fom_io_create_rep_fopt,
	&c2_fom_io_write_rep_fopt,
	&c2_fom_io_read_rep_fopt,
};

static struct c2_fop_type *fopt[] = {
	&c2_fom_io_create_fopt,
	&c2_fom_io_write_fopt,
	&c2_fom_io_read_fopt,
	&c2_fom_io_quit_fopt,
};

static struct c2_fop_type_format *fmts[] = {
	&c2_fom_fop_fid_tfmt,
	&c2_fom_io_seg_tfmt,
	&c2_fom_io_buf_tfmt,
	&c2_fom_io_vec_tfmt,
};

/**
 * A generic fom structure to hold fom, reply fop, storage object
 * and storage io object, used for fop execution.
 */
struct c2_io_fom {
	/** Generic c2_fom object. */
	struct c2_fom			 c2_gen_fom;
	/** Reply FOP associated with request FOP above. */
	struct c2_fop			*rep_fop;
	/** Stob object on which this FOM is acting. */
	struct c2_stob			*stobj;
	/** Stob IO packet for the operation. */
	struct c2_stob_io		 st_io;
};

int create_fom_state(struct c2_fom *fom);
int write_fom_state(struct c2_fom *fom);
int read_fom_state(struct c2_fom *fom);
void c2_io_fom_fini(struct c2_fom *fom);
int c2_create_fom_create(struct c2_fom_type *t, struct c2_fom **out);
int c2_write_fom_create(struct c2_fom_type *t, struct c2_fom **out);
int c2_read_fom_create(struct c2_fom_type *t, struct c2_fom **out);
size_t fom_home_locality(const struct c2_fom *fom);

/**
 * operation structures for respective foms
 */
static struct c2_fom_ops create_fom_ops = {
	.fo_fini = c2_io_fom_fini,
	.fo_state = create_fom_state,
	.fo_home_locality = fom_home_locality,
};

static struct c2_fom_ops write_fom_ops = {
	.fo_fini = c2_io_fom_fini,
	.fo_state = write_fom_state,
	.fo_home_locality = fom_home_locality,
};

static struct c2_fom_ops read_fom_ops = {
	.fo_fini = c2_io_fom_fini,
	.fo_state = read_fom_state,
	.fo_home_locality = fom_home_locality,
};

/**
 * fom type operations structures for corresponding foms.
 */
static const struct c2_fom_type_ops create_fom_type_ops = {
	.fto_create = c2_create_fom_create,
};

static const struct c2_fom_type_ops write_fom_type_ops = {
	.fto_create = c2_write_fom_create,
};

static const struct c2_fom_type_ops read_fom_type_ops = {
	.fto_create = c2_read_fom_create,
};

static struct c2_fom_type create_fom_mopt = {
	.ft_ops = &create_fom_type_ops,
};

static struct c2_fom_type write_fom_mopt = {
	.ft_ops = &write_fom_type_ops,
};

static struct c2_fom_type read_fom_mopt = {
	.ft_ops = &read_fom_type_ops,
};

static struct c2_fom_type *c2_fom_types[] = {
	&create_fom_mopt,
	&write_fom_mopt,
	&read_fom_mopt,
};

struct c2_fom_type *c2_fom_type_map(c2_fop_type_code_t code)
{
	C2_UT_ASSERT(IS_IN_ARRAY((code - 10), c2_fom_types));
	return c2_fom_types[code - 10];
}

static int netcall(struct c2_net_conn *conn, struct reqh_net_call *call)
{
	C2_UT_ASSERT(conn != NULL);
	C2_UT_ASSERT(call != NULL);
	return c2_net_cli_send(conn, &call->ncall);
}

/**
 * call back function to simulate async reply recieved
 * at client side.
 */
static void fom_rep_cb(struct c2_clink *clink)
{
	C2_UT_ASSERT(clink != NULL);
	/* Remove fom from wait list and put fom back on run queue of fom locality */
	if (clink != NULL) {
		struct reqh_net_call *rcall = container_of(clink, struct reqh_net_call, rclink);
		if (rcall != NULL) {
			struct c2_fop *rfop = rcall->ncall.ac_ret;
			C2_UT_ASSERT(rfop != NULL);
				switch(rfop->f_type->ft_code) {
					case 21:
					{
						struct c2_fom_io_create_rep *rep;
						rep = c2_fop_data(rfop);
						if(rep != NULL) {
							printf("Create reply: %i\n",rep->sicr_rc);
							++reply;
						}
						c2_fop_free(rfop);
						break;
					}
					case 22:
					{
						struct c2_fom_io_write_rep *rep;
						rep = c2_fop_data(rfop);
						if(rep != NULL) {
							printf("Write reply: %i %i\n", rep->siwr_rc,
									rep->siwr_count);
							++reply;
						}
						c2_fop_free(rfop);
						break;
					}
					case 23:
					{
						struct c2_fom_io_read_rep *rep;
						unsigned long j = 0;
						rep = c2_fop_data(rfop);
						if(rep != NULL) {
							printf("Read reply: %i %i\n", rep->sirr_rc,
									rep->sirr_buf.cib_count);
							printf("\t[");
							for (j = 0; j < rep->sirr_buf.cib_count; ++j)
								printf("%02x", rep->sirr_buf.cib_value[j]);
							printf("]\n");
							++reply;
						}
						c2_fop_free(rfop);
						break;
					}
					default:
					{
						/* looks like we got an error fop */
						struct c2_reqh_error_rep *rep;
						rep = c2_fop_data(rfop);
						if (rep != NULL) {
							printf("Got reply: %i\n", rep->sierr_rc);
							++reply;
						}
						c2_fop_free(rfop);
						break;
					}
				}
				c2_free(rcall);
			}
		}
}

/********************************************************************
		Client side fop sending methods
********************************************************************/
static void create_send(struct c2_net_conn *conn, const struct c2_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct c2_fom_io_create		*fop;
	struct c2_fom_io_create_rep	*rep;
	struct reqh_net_call		*rcall;

	f = c2_fop_alloc(&c2_fom_io_create_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_fom_io_create_rep_fopt, NULL);
	rep = c2_fop_data(r);
	fop->sic_object = *fid;

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	netcall(conn, rcall);
}

static void read_send(struct c2_net_conn *conn, const struct c2_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct c2_fom_io_read		*fop;
	struct c2_fom_io_read_rep	*rep;
	struct reqh_net_call		*rcall;

	f = c2_fop_alloc(&c2_fom_io_read_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_fom_io_read_rep_fopt, NULL);
	rep = c2_fop_data(r);

	fop->sir_object = *fid;
	fop->sir_seg.f_offset = (unsigned long long)0;
	fop->sir_seg.f_count = (unsigned int)1;

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	netcall(conn, rcall);
}

static void write_send(struct c2_net_conn *conn, const struct c2_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct c2_fom_io_write		*fop;
	struct c2_fom_io_write_rep	*rep;
	struct reqh_net_call		*rcall;
	char filler;

	f = c2_fop_alloc(&c2_fom_io_write_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_fom_io_write_rep_fopt, NULL);
	rep = c2_fop_data(r);

	C2_SET0(&rep);
	fop->siw_object = *fid;
	fop->siw_offset = (unsigned long long)0;
	fop->siw_buf.cib_count = (unsigned long long)1;
	filler = 'a';
	fop->siw_buf.cib_value = c2_alloc(fop->siw_buf.cib_count);
	C2_UT_ASSERT(fop->siw_buf.cib_value != NULL);
	memset(fop->siw_buf.cib_value, filler, fop->siw_buf.cib_count);

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	netcall(conn, rcall);
}


static void reqh_create_send(struct c2_net_conn *conn, unsigned long seq, unsigned long oid)
{
	C2_UT_ASSERT(conn != NULL);
	struct c2_fom_fop_fid *fid;
	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	create_send(conn, fid);
}

static void reqh_write_send(struct c2_net_conn *conn, unsigned long seq, unsigned long oid)
{
	C2_UT_ASSERT(conn != NULL);
	struct c2_fom_fop_fid *fid;
	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	write_send(conn, fid);
}

static void reqh_read_send(struct c2_net_conn *conn, unsigned long seq, unsigned long oid)
{
	C2_UT_ASSERT(conn != NULL);
	struct c2_fom_fop_fid *fid;
	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	read_send(conn, fid);
}

/**
 * function to locate a storage object.
 */
static struct c2_stob *object_find(const struct c2_fom_fop_fid *fid,
                                   struct c2_dtx *tx, struct c2_fom *fom)
{
	struct c2_stob_id	 id;
	struct c2_stob		*obj;
	int 			 result;

	id.si_bits.u_hi = fid->f_seq;
	id.si_bits.u_lo = fid->f_oid;
	result = fom->fo_stdomain->sd_ops->sdo_stob_find(fom->fo_stdomain, &id, &obj);
	C2_UT_ASSERT(result == 0);
	result = c2_stob_locate(obj, tx);
	return obj;
}

/********************************************************************
		   Create methods for foms
********************************************************************/
/**
 * function to create a fom for create fop.
 */
int c2_create_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct c2_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->c2_gen_fom;
	fom->fo_type = t;

		fom->fo_ops = &create_fom_ops;

		fom_obj->rep_fop =
			c2_fop_alloc(&c2_fom_io_create_rep_fopt, NULL);
		if (fom_obj->rep_fop == NULL) {
			c2_free(fom_obj);
			return -ENOMEM;
		}

	fom_obj->stobj = NULL;
	*out = fom;
	return 0;

}

/**
 * function to create a fom for write fop.
 */
int c2_write_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct c2_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->c2_gen_fom;
	fom->fo_type = t;

	fom->fo_ops = &write_fom_ops;
	fom_obj->rep_fop =
		c2_fop_alloc(&c2_fom_io_write_rep_fopt, NULL);
	if (fom_obj->rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->stobj = NULL;
	*out = fom;
	return 0;
}

/**
 * function to create a fom for read fop.
 */
int c2_read_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct c2_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->c2_gen_fom;
	fom->fo_type = t;

	fom->fo_ops = &read_fom_ops;
	fom_obj->rep_fop =
		c2_fop_alloc(&c2_fom_io_read_rep_fopt, NULL);
	if (fom_obj->rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->stobj = NULL;
	*out = fom;
	return 0;
}

/**
 * Function to find home locality for this type of fom.
 * This function using a basic hashin method locates a home locality for a particaulr
 * type of fome, thus everytime we get same locality for aparticular type of fom.
 */
size_t fom_home_locality(const struct c2_fom *fom)
{
	size_t iloc;
	size_t fd_nr;

	if (fom == NULL)
		return -EINVAL;
	
	fd_nr = fom->fo_domain->fd_localities_nr;
	switch(fom->fo_fop->f_type->ft_code) {
		case 10: {
			struct c2_fom_io_create *fop;
			U64 oid;
			fop = c2_fop_data(fom->fo_fop);
			oid = fop->sic_object.f_oid;
			iloc = oid % fd_nr;
		}
		case 11: {
			struct c2_fom_io_read *fop;
			U64 oid;
			fop = c2_fop_data(fom->fo_fop);
			oid = fop->sir_object.f_oid;
			iloc = oid % fd_nr;
		}
		case 12: {
			struct c2_fom_io_write *fop;
			U64 oid;
			fop = c2_fop_data(fom->fo_fop);
			oid = fop->siw_object.f_oid;
			iloc = oid % fd_nr;
		}
	}
	return iloc;
}

/*******************************************************
		fom operations
*******************************************************/
/**
 * fop execution method for create fop.
 */
int create_fom_state(struct c2_fom *fom)
{
	struct c2_fom_io_create		*in_fop;
	struct c2_fom_io_create_rep	*out_fop;
	struct c2_io_fom		*fom_obj;
	int				 result = FSO_AGAIN;

	if (fom->fo_phase < FOPH_NR)
		result = c2_fom_state_generic(fom);
	else {
	
		if (fom->fo_rep_fop == NULL) {
			fom_obj = container_of(fom, struct c2_io_fom, c2_gen_fom);

			if (fom->fo_fop->f_type->ft_code == 10) {
					in_fop = c2_fop_data(fom->fo_fop);
					out_fop = c2_fop_data(fom_obj->rep_fop);
			} else
				return 0;

			fom_obj->stobj = object_find(&in_fop->sic_object, &fom->fo_tx, fom);

			result = c2_stob_create(fom_obj->stobj, &fom->fo_tx);
			C2_UT_ASSERT(result == 0);
			out_fop->sicr_rc = 0;
			fom->fo_rep_fop = fom_obj->rep_fop;
			/* 
			 * add fol record 
			 * if we block set fom->fo_phase to FOPH_QUEUE_REPLY_WAIT, 
			 * else transition to FOPH_QUEUE_REPLY.
			 */
			result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, &fom->fo_tx.tx_dbtx);
			fom->fo_phase = FOPH_QUEUE_REPLY;
			result = FSO_AGAIN;
		} else {

			c2_stob_put(fom_obj->stobj);

			fom->fo_phase = FOPH_SUCCEED;
			result = FSO_AGAIN;
		}
	}

	return result;
}

/**
 * fop execution method for read fop.
 */
int read_fom_state(struct c2_fom *fom)
{
	struct c2_fom_io_read		*in_fop;
	struct c2_fom_io_read_rep	*out_fop;
	struct c2_io_fom 		*fom_obj;
	struct c2_clink			 clink;
	void				*addr;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int				 result = FSO_AGAIN;

	if (fom->fo_phase < FOPH_NR)
		result = c2_fom_state_generic(fom);
	else {
		if (fom->fo_rep_fop == NULL) {
			fom_obj = container_of(fom, struct c2_io_fom, c2_gen_fom);

			if (fom->fo_fop->f_type->ft_code == 12) {

				in_fop = c2_fop_data(fom->fo_fop);
				out_fop = c2_fop_data(fom_obj->rep_fop);
			} else
				return 0;

			fom_obj->stobj = object_find(&in_fop->sir_object, &fom->fo_tx, fom);

			bshift = fom_obj->stobj->so_op->sop_block_shift(fom_obj->stobj);
			bmask  = (1 << bshift) - 1;

			C2_UT_ASSERT((in_fop->sir_seg.f_count & bmask) == 0);
			C2_UT_ASSERT((in_fop->sir_seg.f_offset & bmask) == 0);

			C2_ALLOC_ARR(out_fop->sirr_buf.cib_value, in_fop->sir_seg.f_count);
			C2_UT_ASSERT(out_fop->sirr_buf.cib_value != NULL);

			in_fop->sir_seg.f_count >>= bshift;
			in_fop->sir_seg.f_offset >>= bshift;

			addr = c2_stob_addr_pack(out_fop->sirr_buf.cib_value, bshift);

			c2_stob_io_init(&fom_obj->st_io);

			fom_obj->st_io.si_user.div_vec.ov_vec.v_nr    = 1;
			fom_obj->st_io.si_user.div_vec.ov_vec.v_count = &in_fop->sir_seg.f_count;
			fom_obj->st_io.si_user.div_vec.ov_buf = &addr;

			fom_obj->st_io.si_stob.iv_vec.v_nr    = 1;
			fom_obj->st_io.si_stob.iv_vec.v_count = &in_fop->sir_seg.f_count;
			fom_obj->st_io.si_stob.iv_index       = &in_fop->sir_seg.f_offset;

			fom_obj->st_io.si_opcode = SIO_READ;
			fom_obj->st_io.si_flags  = 0;

			c2_clink_init(&clink, NULL);
			c2_clink_add(&fom_obj->st_io.si_wait, &clink);

			result = c2_stob_io_launch(&fom_obj->st_io, fom_obj->stobj, &fom->fo_tx, NULL);
			C2_UT_ASSERT(result == 0);

			c2_chan_wait(&clink);

			out_fop->sirr_rc            = fom_obj->st_io.si_rc;
			out_fop->sirr_buf.cib_count = fom_obj->st_io.si_count << bshift;

			c2_clink_del(&clink);
			c2_clink_fini(&clink);


			if (result == -EDEADLK) {
				fom->fo_phase = FOPH_FAILURE;
				result = FSO_AGAIN;
			} else {

				/* 
				 * add fol record.
				 * Transition to FOPH_QUEUE_REPLY.
				 */
				fom->fo_rep_fop = fom_obj->rep_fop;
				result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, 
								&fom->fo_tx.tx_dbtx);
				fom->fo_phase = FOPH_QUEUE_REPLY;
				result = FSO_AGAIN;
			}
		} else {
			
			c2_stob_io_fini(&fom_obj->st_io);
			c2_stob_put(fom_obj->stobj);
			fom->fo_phase = FOPH_SUCCEED;
			result = FSO_AGAIN;
		}
	}
	return result;
}

/**
 * fop execution method for write fop.
 */
int write_fom_state(struct c2_fom *fom)
{

	struct c2_fom_io_write		*in_fop;
	struct c2_fom_io_write_rep	*out_fop;
	struct c2_io_fom		*fom_obj;
	void				*addr;
	c2_bcount_t			 count;
	c2_bindex_t			 offset;
	struct c2_clink			 clink;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int				 result = FSO_AGAIN;

	if (fom->fo_phase < FOPH_NR)
		result = c2_fom_state_generic(fom);
	else {
		if (fom->fo_rep_fop == NULL) {
			fom_obj = container_of(fom, struct c2_io_fom, c2_gen_fom);

			if (fom->fo_fop->f_type->ft_code == 11) {
					in_fop = c2_fop_data(fom->fo_fop);
					out_fop = c2_fop_data(fom_obj->rep_fop);
			} else
				return 0;

			fom_obj->stobj = object_find(&in_fop->siw_object, &fom->fo_tx, fom);

			bshift = fom_obj->stobj->so_op->sop_block_shift(fom_obj->stobj);
			bmask  = (1 << bshift) - 1;

			C2_UT_ASSERT((in_fop->siw_buf.cib_count & bmask) == 0);
			C2_UT_ASSERT((in_fop->siw_offset & bmask) == 0);

			addr = c2_stob_addr_pack(in_fop->siw_buf.cib_value, bshift);
			count = in_fop->siw_buf.cib_count >> bshift;
			offset = in_fop->siw_offset >> bshift;

			c2_stob_io_init(&fom_obj->st_io);

			fom_obj->st_io.si_user.div_vec.ov_vec.v_nr    = 1;
			fom_obj->st_io.si_user.div_vec.ov_vec.v_count = &count;
			fom_obj->st_io.si_user.div_vec.ov_buf = &addr;

			fom_obj->st_io.si_stob.iv_vec.v_nr    = 1;
			fom_obj->st_io.si_stob.iv_vec.v_count = &count;
			fom_obj->st_io.si_stob.iv_index       = &offset;

			fom_obj->st_io.si_opcode = SIO_WRITE;
			fom_obj->st_io.si_flags  = 0;

			c2_clink_init(&clink, NULL);
			c2_clink_add(&fom_obj->st_io.si_wait, &clink);

			result = c2_stob_io_launch(&fom_obj->st_io, fom_obj->stobj,
							&fom->fo_tx, NULL);
			C2_UT_ASSERT(result == 0);

			c2_chan_wait(&clink);

			out_fop->siwr_rc    = fom_obj->st_io.si_rc;
			out_fop->siwr_count = fom_obj->st_io.si_count << bshift;

			c2_clink_del(&clink);
			c2_clink_fini(&clink);

			if (result == -EDEADLK) {
				fom->fo_phase = FOPH_FAILURE;
				result = FSO_AGAIN;
			} else {

				/* 
				 * add fol record.
				 * Transition to FOPH_QUEUE_REPLY.
				 */
				fom->fo_rep_fop = fom_obj->rep_fop;
				result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, 
								&fom->fo_tx.tx_dbtx);
				fom->fo_phase = FOPH_QUEUE_REPLY;
				result = FSO_AGAIN;
			}
		} else {
			c2_stob_io_fini(&fom_obj->st_io);
			c2_stob_put(fom_obj->stobj);
			fom->fo_phase = FOPH_SUCCEED;
			result = FSO_AGAIN;
		}
	}
	return result;
}

/**
 * fom clean up function
 */
void c2_io_fom_fini(struct c2_fom *fom)
{
	struct c2_io_fom *fom_obj;
	fom_obj = container_of(fom, struct c2_io_fom, c2_gen_fom);
	if (c2_fom_invariant(fom))
		c2_fom_fini(fom);
	c2_free(fom_obj);
}

/**
 * fom initialization function, invoked from reqh_fop_handle.
 */
int c2_io_fom_init(struct c2_fop *fop, struct c2_fom **m)
{

	struct c2_fom_type	*fom_type;
	int			 result = 0;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	fom_type = c2_fom_type_map(fop->f_type->ft_code);
	if (fom_type == NULL)
		return -EINVAL;
	fop->f_type->ft_fom_type = *fom_type;
	result = fop->f_type->ft_fom_type.ft_ops->fto_create(&(fop->f_type->ft_fom_type), m);
	if (!result) {
		(*m)->fo_fop = fop;
		result = c2_fom_init(*m);
	}

	return result;
}

/**
 * function to clean io fops
 */
void fom_io_fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

/**
 * function to intialize io fops.
 */
int fom_io_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
		if (result == 0)
			c2_fop_object_init(&c2_fom_fop_fid_tfmt);
	}
	if (result != 0)
		fom_io_fop_fini();
	return result;
}

/********* Memory allocation structures ***********/

struct mock_balloc {
	struct c2_mutex  mb_lock;
	c2_bindex_t      mb_next;
	struct ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct ad_balloc *ballroom)
{
	return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
                            uint32_t bshift)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_init(&mb->mb_lock);
	return 0;
}

static void mock_balloc_fini(struct ad_balloc *ballroom)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_fini(&mb->mb_lock);
}

static int mock_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *tx,
                             c2_bcount_t count, struct c2_ext *out)
{
	struct mock_balloc *mb = b2mock(ballroom);
	c2_bcount_t 	    giveout;

	c2_mutex_lock(&mb->mb_lock);
	giveout = min64u(count, 500000);
	out->e_start = mb->mb_next;
	out->e_end   = mb->mb_next + giveout;
	mb->mb_next += giveout + 1;
	/*
	printf("allocated %8lx/%8lx bytes: [%8lx .. %8lx)\n", giveout, count,
		out->e_start, out->e_end); */
	c2_mutex_unlock(&mb->mb_lock);
	return 0;
}

static int mock_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *tx,
                            struct c2_ext *ext)
{
	printf("freed     %8lx bytes: [%8lx .. %8lx)\n", c2_ext_length(ext),
		ext->e_start, ext->e_end);
	return 0;
}

static const struct ad_balloc_ops mock_balloc_ops = {
	.bo_init  = mock_balloc_init,
	.bo_fini  = mock_balloc_fini,
	.bo_alloc = mock_balloc_alloc,
	.bo_free  = mock_balloc_free,
};

static struct mock_balloc mb = {
	.mb_next = 0,
	.mb_ballroom = {
		.ab_ops = &mock_balloc_ops
	}
};

/**
 * reqh service handler, for incoming fops.
 * This function submits fop for processing to reqh.
 */
static int reqh_service_handler(struct c2_service *service,
                                   struct c2_fop *fop,
                                   void *cookie)
{
	C2_UT_ASSERT(service != NULL);
	C2_UT_ASSERT(fop != NULL);

	c2_reqh_fop_handle(&reqh, fop, cookie);
	return 0;
}

/**
 * Creates and initializes network resources.
 */
int create_net_connection(struct c2_service_id *rsid, struct c2_net_conn **conn,
			  struct c2_service_id *node_arg, struct c2_service *rserv)
{
	int rc = 0;

	C2_SET0(rserv);
	rserv->s_table.not_start = fopt[0]->ft_code;
	rserv->s_table.not_nr    = ARRAY_SIZE(fopt);
	rserv->s_table.not_fopt  = fopt;
	rserv->s_handler         = &reqh_service_handler;

	rc = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&ndom, &c2_net_usunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_id_init(rsid, &ndom, "127.0.0.1", PORT);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_start(rserv, rsid);
	C2_UT_ASSERT(rc >= 0);
	rc = c2_net_conn_create(rsid);
	C2_UT_ASSERT(rc == 0);

	*conn = c2_net_conn_find(rsid);
	C2_UT_ASSERT(*conn != NULL);

	return rc;
}

/**
 * Test function for reqh ut
 */
void test_reqh(void)
{
	int 		 result;
	unsigned long 	 i;
	const char	*path;
	char        	 opath[64];
	char        	 dpath[64];

	struct c2_service_id	 rsid = { .si_uuid = "node-1" };
	struct c2_net_conn	*conn;
	struct c2_service_id	 reqh_node_arg = { .si_uuid = {0} };
	/* struct c2_service_id  node_ret = { .si_uuid = {0} }; */
	struct c2_service	 rservice;

	struct c2_stob_domain	*bdom;
	struct c2_stob_id	 backid;
	struct c2_stob		*bstore;
	struct c2_stob		*reqh_addb_stob;
	struct c2_stob_id        reqh_addb_stob_id = {
					.si_bits = {
						.u_hi = 1,
						.u_lo = 2
					}
				};
	struct c2_dbenv         db;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	backid.si_bits.u_hi = 0x8;
	backid.si_bits.u_lo = 0xf00baf11e;
	/* port above 1024 is for normal use access permission */
	path = "../__s";

	/* initialize processors */
	if (!c2_processor_is_initialized())
		c2_processors_init();

	result = fom_io_fop_init();
	C2_UT_ASSERT(result == 0);

	C2_UT_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	result = mkdir(path, 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	result = mkdir(opath, 0700);
	C2_UT_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	sprintf(dpath, "%s/d", path);

	/*
	 * Initialize the data-base and fol.
	 */
	result = c2_dbenv_init(&db, dpath, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_fol_init(&fol, &db);
	C2_UT_ASSERT(result == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
							  path, &bdom);
	C2_UT_ASSERT(result == 0);

	result = bdom->sd_ops->sdo_stob_find(bdom, &backid, &bstore);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(bstore->so_state == CSS_UNKNOWN);

	result = c2_stob_create(bstore, NULL);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(bstore->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	result = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type, "", &sdom);
	C2_UT_ASSERT(result == 0);

	result = ad_setup(sdom, &db, bstore, &mb.mb_ballroom);
	C2_UT_ASSERT(result == 0);

	c2_stob_put(bstore);

	/* create or open a stob into which to store the record. */
	result = bdom->sd_ops->sdo_stob_find(bdom, &reqh_addb_stob_id, &reqh_addb_stob);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(reqh_addb_stob->so_state == CSS_UNKNOWN);

	result = c2_stob_create(reqh_addb_stob, NULL);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(reqh_addb_stob->so_state == CSS_EXISTS);

	/* write addb record into stob */
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_STOB, c2_addb_stob_add,
					  reqh_addb_stob, NULL);

	create_net_connection(&rsid, &conn, &reqh_node_arg, &rservice);

	/* initializing request handler */
	result =  c2_reqh_init(&reqh, NULL, NULL, sdom, &fol, &rservice);
	C2_UT_ASSERT(result == 0);

	/* create listening thread to accept async reply's */

	for(i = 0; i < 10; ++i)
		reqh_create_send(conn, i, i);

	for(i = 0; i < 10; ++i)
		reqh_write_send(conn, i, i);

	for(i = 0; i < 10; ++i)
		reqh_read_send(conn, i, i);

	while(reply < 30)
	     sleep(1);

	/* clean up network connections */
	c2_net_conn_unlink(conn);
	c2_net_conn_release(conn);
	c2_service_stop(&rservice);
	c2_service_id_fini(&rsid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);

	c2_addb_choose_store_media(C2_ADDB_REC_STORE_NONE);
	c2_stob_put(reqh_addb_stob);

	c2_reqh_fini(&reqh);
	sdom->sd_ops->sdo_fini(sdom);
	bdom->sd_ops->sdo_fini(bdom);
	c2_fol_fini(&fol);
	c2_dbenv_fini(&db);
	fom_io_fop_fini();
	if (c2_processor_is_initialized())
		c2_processors_fini();

}

const struct c2_test_suite reqh_ut = {
	.ts_name = "reqh-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "reqh", test_reqh },
		{ NULL, NULL }
	}
};

/** @} end group reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
