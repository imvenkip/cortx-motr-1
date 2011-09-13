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
#include <sys/stat.h>	/* mkdir */
#include <sys/types.h>	/* mkdir */
#include <err.h>

#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"
#include "lib/processor.h"
#include "lib/list.h"

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

#include "reqh/ut/fom_io.ff"
#include "reqh/reqh_fops.ff"

/**
   @addtogroup reqh
   @{
 */

/**
 *  Server side structures and objects
 */

enum {
	PORT = 10001
};

/**
 * Write fop specific fom execution phases
 */
enum reqh_ut_write_fom_phase {
	FOPH_WRITE_STOB_IO = FOPH_NR + 1,
	FOPH_WRITE_STOB_IO_WAIT
};

/**
 * Read fop specific fom execution phases
 */
enum reqh_ut_read_fom_phase {
	FOPH_READ_STOB_IO = FOPH_NR + 1,
	FOPH_READ_STOB_IO_WAIT
};

static struct c2_stob_domain *sdom;
static struct c2_net_domain	ndom;
static struct c2_fol	fol;

/**
 * Global reqh object
 */
static struct c2_reqh		reqh;

/**
 * Structure to hold c2_net_call and c2_clink, for network communication
 */
struct reqh_ut_net_call {
	struct c2_net_call	ncall;
	struct c2_clink		rclink;
};

static int  reply;
static bool reqh_ut_status;
static int  reqh_ut_io_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
 * Fop operation structures for corresponding fops.
 */
static struct c2_fop_type_ops reqh_ut_write_fop_ops = {
	.fto_fom_init = reqh_ut_io_fom_init,
	.fto_execute = NULL,
};

static struct c2_fop_type_ops reqh_ut_read_fop_ops = {
	.fto_fom_init = reqh_ut_io_fom_init,
	.fto_execute = NULL,
};

static struct c2_fop_type_ops reqh_ut_create_fop_ops = {
	.fto_fom_init = reqh_ut_io_fom_init,
	.fto_execute = NULL,
};

/**
 * Reply fop fid enumerations
 */
enum reply_fop {
	CREATE_REQ = 40,
	WRITE_REQ,
	READ_REQ,
	CREATE_REP = 43,
	WRITE_REP,
	READ_REP
};

/**
 * Fop type declarations for corresponding fops
 */
C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_create, "reqh_ut_create", CREATE_REQ, &reqh_ut_create_fop_ops);
C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_write, "reqh_ut_write", WRITE_REQ, &reqh_ut_write_fop_ops);
C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_read, "reqh_ut_read", READ_REQ, &reqh_ut_read_fop_ops);

C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_create_rep, "reqh_ut_create reply", CREATE_REP, NULL);
C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_write_rep, "reqh_ut_write reply", WRITE_REP, NULL);
C2_FOP_TYPE_DECLARE(reqh_ut_fom_io_read_rep, "reqh_ut_read reply",  READ_REP, NULL);

/**
 * Fop type structures required for initialising corresponding fops.
 */
static struct c2_fop_type *reqh_ut_fops[] = {
	&reqh_ut_fom_io_create_fopt,
	&reqh_ut_fom_io_write_fopt,
	&reqh_ut_fom_io_read_fopt,

	&reqh_ut_fom_io_create_rep_fopt,
	&reqh_ut_fom_io_write_rep_fopt,
	&reqh_ut_fom_io_read_rep_fopt,
};

static struct c2_fop_type *reqh_ut_fopt[] = {
	&reqh_ut_fom_io_create_fopt,
	&reqh_ut_fom_io_write_fopt,
	&reqh_ut_fom_io_read_fopt,
};

static struct c2_fop_type_format *reqh_ut_fmts[] = {
        &reqh_ut_fom_fop_fid_tfmt,
};

/**
 * A generic fom structure to hold fom, reply fop, storage object
 * and storage io object, used for fop execution.
 */
struct reqh_ut_io_fom {
	/** Generic c2_fom object. */
	struct c2_fom			 rh_ut_fom;
	/** Reply FOP associated with request FOP above. */
	struct c2_fop			*rep_fop;
	/** Stob object on which this FOM is acting. */
	struct c2_stob			*rh_ut_stobj;
	/** Stob IO packet for the operation. */
	struct c2_stob_io		 rh_ut_stio;
};

static int reqh_ut_create_fom_state(struct c2_fom *fom);
static int reqh_ut_write_fom_state(struct c2_fom *fom);
static int reqh_ut_read_fom_state(struct c2_fom *fom);
static void reqh_ut_io_fom_fini(struct c2_fom *fom);
static int reqh_ut_create_fom_create(struct c2_fom_type *t, struct c2_fom **out);
static int reqh_ut_write_fom_create(struct c2_fom_type *t, struct c2_fom **out);
static int reqh_ut_read_fom_create(struct c2_fom_type *t, struct c2_fom **out);
static size_t reqh_ut_find_fom_home_locality(const struct c2_fom *fom);

/**
 * Operation structures for respective foms
 */
static struct c2_fom_ops reqh_ut_create_fom_ops = {
	.fo_fini = reqh_ut_io_fom_fini,
	.fo_state = reqh_ut_create_fom_state,
	.fo_home_locality = reqh_ut_find_fom_home_locality,
};

static struct c2_fom_ops reqh_ut_write_fom_ops = {
	.fo_fini = reqh_ut_io_fom_fini,
	.fo_state = reqh_ut_write_fom_state,
	.fo_home_locality = reqh_ut_find_fom_home_locality,
};

static struct c2_fom_ops reqh_ut_read_fom_ops = {
	.fo_fini = reqh_ut_io_fom_fini,
	.fo_state = reqh_ut_read_fom_state,
	.fo_home_locality = reqh_ut_find_fom_home_locality,
};

/**
 * Fom type operations structures for corresponding foms.
 */
static const struct c2_fom_type_ops reqh_ut_create_fom_type_ops = {
	.fto_create = reqh_ut_create_fom_create,
};

static const struct c2_fom_type_ops reqh_ut_write_fom_type_ops = {
	.fto_create = reqh_ut_write_fom_create,
};

static const struct c2_fom_type_ops reqh_ut_read_fom_type_ops = {
	.fto_create = reqh_ut_read_fom_create,
};

static struct c2_fom_type reqh_ut_create_fom_mopt = {
	.ft_ops = &reqh_ut_create_fom_type_ops,
};

static struct c2_fom_type reqh_ut_write_fom_mopt = {
	.ft_ops = &reqh_ut_write_fom_type_ops,
};

static struct c2_fom_type reqh_ut_read_fom_mopt = {
	.ft_ops = &reqh_ut_read_fom_type_ops,
};

static struct c2_fom_type *reqh_ut_fom_types[] = {
	&reqh_ut_create_fom_mopt,
	&reqh_ut_write_fom_mopt,
	&reqh_ut_read_fom_mopt,
};

/**
 * Function to map a fop to its corresponding fom
 */
static struct c2_fom_type *reqh_ut_fom_type_map(c2_fop_type_code_t code)
{
	C2_UT_ASSERT(IS_IN_ARRAY((code - CREATE_REQ), reqh_ut_fom_types));

	return reqh_ut_fom_types[code - CREATE_REQ];
}

/**
 * Dispatches the request fop.
 */
static int reqh_ut_netcall(struct c2_net_conn *conn, struct reqh_ut_net_call *call)
{
	C2_UT_ASSERT(conn != NULL);
	C2_UT_ASSERT(call != NULL);

	return c2_net_cli_send(conn, &call->ncall);
}

/**
 * Call back function to simulate async reply recieved
 * at client side.
 */
static void fom_rep_cb(struct c2_clink *clink)
{
	C2_UT_ASSERT(clink != NULL);
	if (clink != NULL) {
		struct reqh_ut_net_call *rcall = container_of(clink,
						struct reqh_ut_net_call, rclink);
		if (rcall != NULL) {
			struct c2_fop *rfop = rcall->ncall.ac_ret;
			C2_UT_ASSERT(rfop != NULL);
			switch(rfop->f_type->ft_code) {
			case CREATE_REP:
			{
				struct reqh_ut_fom_io_create_rep *rep;
				rep = c2_fop_data(rfop);
				if(rep != NULL) {
					if (rep->ficr_rc != 0) {
						reqh_ut_status = false;
						C2_UT_FAIL("reqh-ut failed");
						break;
					}
					++reply;
				}
				c2_fop_free(rfop);
				break;
			}
			case WRITE_REP:
			{
				struct reqh_ut_fom_io_write_rep *rep;
				rep = c2_fop_data(rfop);
				if(rep != NULL) {
					if (rep->fiwr_rc != 0) {
						reqh_ut_status = false;
						C2_UT_FAIL("reqh-ut failed");
						break;
					}
					++reply;
				}
				c2_fop_free(rfop);
				break;
			}
			case READ_REP:
			{
				struct reqh_ut_fom_io_read_rep *rep;
				rep = c2_fop_data(rfop);
				if(rep != NULL) {
					if (rep->firr_rc != 0) {
						reqh_ut_status = false;
						C2_UT_FAIL("reqh-ut failed");
						break;
					}
					++reply;
				}
				c2_fop_free(rfop);
				break;
			}
			default:
			{
				struct c2_reqh_error_rep *rep;
				rep = c2_fop_data(rfop);
				if (rep != NULL) {
					if (rep->rerr_rc != 0) {
						reqh_ut_status = false;
						C2_UT_FAIL("reqh-ut failed");
                                                break;
                                        }
				}
				c2_fop_free(rfop);
				break;
			}
			}
				c2_free(rcall);
		}
	}
}

/**
 * Sends create fop request.
 */
static void create_send(struct c2_net_conn *conn, const struct reqh_ut_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct reqh_ut_fom_io_create		*fop;
	struct reqh_ut_fom_io_create_rep	*rep;
	struct reqh_ut_net_call		*rcall;

	f = c2_fop_alloc(&reqh_ut_fom_io_create_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&reqh_ut_fom_io_create_rep_fopt, NULL);
	rep = c2_fop_data(r);
	fop->fic_object = *fid;

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	reqh_ut_netcall(conn, rcall);
}

/**
 * Sends read fop request.
 */
static void read_send(struct c2_net_conn *conn, const struct reqh_ut_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct reqh_ut_fom_io_read		*fop;
	struct reqh_ut_fom_io_read_rep	*rep;
	struct reqh_ut_net_call		*rcall;

	f = c2_fop_alloc(&reqh_ut_fom_io_read_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&reqh_ut_fom_io_read_rep_fopt, NULL);
	rep = c2_fop_data(r);

	fop->fir_object = *fid;

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	reqh_ut_netcall(conn, rcall);
}

/**
 * Sends write fop request.
 */
static void write_send(struct c2_net_conn *conn, const struct reqh_ut_fom_fop_fid *fid)
{
	struct c2_fop			*f;
	struct c2_fop			*r;
	struct reqh_ut_fom_io_write		*fop;
	struct reqh_ut_fom_io_write_rep	*rep;
	struct reqh_ut_net_call		*rcall;

	f = c2_fop_alloc(&reqh_ut_fom_io_write_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&reqh_ut_fom_io_write_rep_fopt, NULL);
	rep = c2_fop_data(r);

	fop->fiw_object = *fid;
	fop->fiw_value = 'a';

	rcall = c2_alloc(sizeof *rcall);
	C2_UT_ASSERT(rcall != NULL);
	rcall->ncall.ac_arg = f;
	rcall->ncall.ac_ret = r;
	c2_chan_init(&rcall->ncall.ac_chan);
	c2_clink_init(&rcall->rclink, &fom_rep_cb);
	c2_clink_add(&rcall->ncall.ac_chan, &rcall->rclink);
	reqh_ut_netcall(conn, rcall);
}


static void reqh_ut_create_send(struct c2_net_conn *conn, unsigned long seq,
							unsigned long oid)
{
	struct reqh_ut_fom_fop_fid *fid;

	C2_PRE(conn != NULL);

	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	create_send(conn, fid);
}

static void reqh_ut_write_send(struct c2_net_conn *conn, unsigned long seq,
							unsigned long oid)
{
	struct reqh_ut_fom_fop_fid *fid;

	C2_PRE(conn != NULL);

	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	write_send(conn, fid);
}

static void reqh_ut_read_send(struct c2_net_conn *conn, unsigned long seq,
							unsigned long oid)
{
	struct reqh_ut_fom_fop_fid *fid;

	C2_PRE(conn != NULL);

	fid = c2_alloc(sizeof *fid);
	fid->f_seq = seq;
	fid->f_oid = oid;
	read_send(conn, fid);
}

/**
 * Function to locate a storage object.
 */
static struct c2_stob *object_find(const struct reqh_ut_fom_fop_fid *fid,
                                   struct c2_dtx *tx, struct c2_fom *fom)
{
	struct c2_stob_id	 id;
	struct c2_stob		*obj;
	int			 result;
	struct c2_stob_domain	*fom_stdom;

	id.si_bits.u_hi = fid->f_seq;
	id.si_bits.u_lo = fid->f_oid;
	fom_stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;
	result = fom_stdom->sd_ops->sdo_stob_find(fom_stdom, &id, &obj);
	C2_UT_ASSERT(result == 0);
	result = c2_stob_locate(obj, tx);
	return obj;
}

/**
 * Creates a fom for create fop.
 */
static int reqh_ut_create_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct reqh_ut_io_fom	*fom_obj;

	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->rh_ut_fom;
	fom->fo_type = t;

	fom->fo_ops = &reqh_ut_create_fom_ops;

	fom_obj->rep_fop =
		c2_fop_alloc(&reqh_ut_fom_io_create_rep_fopt, NULL);
	if (fom_obj->rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->rh_ut_stobj = NULL;
	*out = fom;
	return 0;

}

/**
 * Creates a fom for write fop.
 */
static int reqh_ut_write_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct reqh_ut_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->rh_ut_fom;
	fom->fo_type = t;

	fom->fo_ops = &reqh_ut_write_fom_ops;
	fom_obj->rep_fop =
		c2_fop_alloc(&reqh_ut_fom_io_write_rep_fopt, NULL);
	if (fom_obj->rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->rh_ut_stobj = NULL;
	*out = fom;
	return 0;
}

/**
 * Creates a fom for read fop.
 */
static int reqh_ut_read_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct reqh_ut_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->rh_ut_fom;
	fom->fo_type = t;

	fom->fo_ops = &reqh_ut_read_fom_ops;
	fom_obj->rep_fop =
		c2_fop_alloc(&reqh_ut_fom_io_read_rep_fopt, NULL);
	if (fom_obj->rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->rh_ut_stobj = NULL;
	*out = fom;
	return 0;
}

/**
 * Finds home locality for this type of fom.
 * This function, using a basic hashing method
 * locates a home locality for a particular type
 * of fome, inorder to have same locality of
 * execution for a certain type of fom.
 */
static size_t reqh_ut_find_fom_home_locality(const struct c2_fom *fom)
{
	size_t iloc;

	if (fom == NULL)
		return -EINVAL;

	switch (fom->fo_fop->f_type->ft_code) {
	case CREATE_REQ: {
		struct reqh_ut_fom_io_create *fop;
		uint64_t oid;
		fop = c2_fop_data(fom->fo_fop);
		oid = fop->fic_object.f_oid;
		iloc = oid;
		break;
	}
	case WRITE_REQ: {
		struct reqh_ut_fom_io_read *fop;
		uint64_t oid;
		fop = c2_fop_data(fom->fo_fop);
		oid = fop->fir_object.f_oid;
		iloc = oid;
		break;
	}
	case READ_REQ: {
		struct reqh_ut_fom_io_write *fop;
		uint64_t oid;
		fop = c2_fop_data(fom->fo_fop);
		oid = fop->fiw_object.f_oid;
		iloc = oid;
		break;
	}
	default:
		return -EINVAL;
	}
	return iloc;
}

/**
 * A simple non blocking create fop specific fom
 * state method implemention.
 */
static int reqh_ut_create_fom_state(struct c2_fom *fom)
{
	struct reqh_ut_fom_io_create		*in_fop;
	struct reqh_ut_fom_io_create_rep	*out_fop;
	struct reqh_ut_io_fom			*fom_obj;
	int                                      result;

	C2_PRE(fom->fo_fop->f_type->ft_code == CREATE_REQ);

	fom_obj = container_of(fom, struct reqh_ut_io_fom, rh_ut_fom);
	if (fom->fo_phase < FOPH_NR) {
		result = c2_fom_state_generic(fom);
	} else {
		in_fop = c2_fop_data(fom->fo_fop);
		out_fop = c2_fop_data(fom_obj->rep_fop);

		fom_obj->rh_ut_stobj = object_find(&in_fop->fic_object, &fom->fo_tx, fom);

		result = c2_stob_create(fom_obj->rh_ut_stobj, &fom->fo_tx);
		out_fop->ficr_rc = result;
		fom->fo_rep_fop = fom_obj->rep_fop;
		fom->fo_rc = result;
		if (result != 0)
			fom->fo_phase = FOPH_FAILURE;
		 else
			fom->fo_phase = FOPH_SUCCESS;

		result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, &fom->fo_tx.tx_dbtx);
		C2_UT_ASSERT(result == 0);
		result = FSO_AGAIN;
	}

	if (fom->fo_phase == FOPH_FINISH && fom->fo_rc == 0)
		c2_stob_put(fom_obj->rh_ut_stobj);

	return result;
}

/**
 * A simple non blocking read fop specific fom
 * state method implemention.
 */
static int reqh_ut_read_fom_state(struct c2_fom *fom)
{
	struct reqh_ut_fom_io_read	*in_fop;
	struct reqh_ut_fom_io_read_rep	*out_fop;
	struct reqh_ut_io_fom		*fom_obj;
	struct c2_stob_io               *stio;
	struct c2_stob                  *stobj;
	void				*addr;
	c2_bcount_t			 count;
	c2_bcount_t			 offset;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int				 result;

	C2_PRE(fom->fo_fop->f_type->ft_code == READ_REQ);

	fom_obj = container_of(fom, struct reqh_ut_io_fom, rh_ut_fom);
	stio = &fom_obj->rh_ut_stio;
	if (fom->fo_phase < FOPH_NR) {
		result = c2_fom_state_generic(fom);
	} else {
		out_fop = c2_fop_data(fom_obj->rep_fop);
		C2_UT_ASSERT(out_fop != NULL);

		if (fom->fo_phase == FOPH_READ_STOB_IO) {

			in_fop = c2_fop_data(fom->fo_fop);
			C2_UT_ASSERT(in_fop != NULL);
			fom_obj->rh_ut_stobj = object_find(&in_fop->fir_object,
								&fom->fo_tx, fom);

			stobj =  fom_obj->rh_ut_stobj;
			bshift = stobj->so_op->sop_block_shift(stobj);
			bmask  = (1 << bshift) - 1;

			addr = c2_stob_addr_pack(&out_fop->firr_value, bshift);
			count = 1 >> bshift;
			offset = 0;

			c2_stob_io_init(stio);

			stio->si_user.div_vec = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&addr,
										&count);

			stio->si_stob.iv_vec.v_nr    = 1;
			stio->si_stob.iv_vec.v_count = &count;
			stio->si_stob.iv_index       = &offset;

			stio->si_opcode = SIO_READ;
			stio->si_flags  = 0;

			c2_fom_block_enter(fom);
			c2_fom_block_at(fom, &stio->si_wait);
			result = c2_stob_io_launch(stio, stobj, &fom->fo_tx, NULL);

			if (result != 0) {
				fom->fo_rc = result;
				fom->fo_phase = FOPH_FAILURE;
			} else {
				fom->fo_phase = FOPH_READ_STOB_IO_WAIT;
				result = FSO_WAIT;
			}
		} else if (fom->fo_phase == FOPH_READ_STOB_IO_WAIT) {
			fom->fo_rc = stio->si_rc;
			stobj = fom_obj->rh_ut_stobj;
			if (fom->fo_rc != 0)
				fom->fo_phase = FOPH_FAILURE;
			else {
				bshift = stobj->so_op->sop_block_shift(stobj);
				out_fop->firr_count = stio->si_count << bshift;
				fom->fo_phase = FOPH_SUCCESS;
			}

		}

		if (fom->fo_phase == FOPH_FAILURE || fom->fo_phase == FOPH_SUCCESS) {
			c2_fom_block_leave(fom);
			out_fop->firr_rc = fom->fo_rc;
			fom->fo_rep_fop = fom_obj->rep_fop;
			result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
							&fom->fo_tx.tx_dbtx);
			C2_UT_ASSERT(result == 0);
			result = FSO_AGAIN;
		}

	}

	if (fom->fo_phase == FOPH_FINISH) {
		/*
		   If we fail in any of the generic phase, stob io
		   is uninitialised, so no need to fini.
		 */
		if (stio->si_state != SIS_ZERO) {
			c2_stob_io_fini(stio);
			c2_stob_put(fom_obj->rh_ut_stobj);
		}
	}

	return result;
}

/**
 * A simple non blocking write fop specific fom
 * state method implemention.
 */
static int reqh_ut_write_fom_state(struct c2_fom *fom)
{

	struct reqh_ut_fom_io_write	*in_fop;
	struct reqh_ut_fom_io_write_rep	*out_fop;
	struct reqh_ut_io_fom		*fom_obj;
	struct c2_stob_io               *stio;
	struct c2_stob                  *stobj;
	void				*addr;
	c2_bcount_t			 count;
	c2_bindex_t			 offset;
	uint32_t			 bshift;
	uint64_t			 bmask;
	int				 result;

	C2_PRE(fom->fo_fop->f_type->ft_code == WRITE_REQ);

	fom_obj = container_of(fom, struct reqh_ut_io_fom, rh_ut_fom);
	stio = &fom_obj->rh_ut_stio;

	if (fom->fo_phase < FOPH_NR) {
		result = c2_fom_state_generic(fom);
	} else {
		out_fop = c2_fop_data(fom_obj->rep_fop);
		C2_UT_ASSERT(out_fop != NULL);

		if (fom->fo_phase == FOPH_WRITE_STOB_IO) {
			in_fop = c2_fop_data(fom->fo_fop);
			C2_UT_ASSERT(in_fop != NULL);

			fom_obj->rh_ut_stobj = object_find(&in_fop->fiw_object, &fom->fo_tx, fom);

			stobj = fom_obj->rh_ut_stobj;
			bshift = stobj->so_op->sop_block_shift(stobj);
			bmask  = (1 << bshift) - 1;

			addr = c2_stob_addr_pack(&in_fop->fiw_value, bshift);
			count = 1 >> bshift;
			offset = 0;

			c2_stob_io_init(stio);

			stio->si_user.div_vec = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&addr, &count);

			stio->si_stob.iv_vec.v_nr    = 1;
			stio->si_stob.iv_vec.v_count = &count;
			stio->si_stob.iv_index       = &offset;

			stio->si_opcode = SIO_WRITE;
			stio->si_flags  = 0;

			c2_fom_block_enter(fom);
			c2_fom_block_at(fom, &stio->si_wait);
			result = c2_stob_io_launch(stio, stobj, &fom->fo_tx, NULL);

			if (result != 0) {
				fom->fo_rc = result;
				fom->fo_phase = FOPH_FAILURE;
			} else {
				fom->fo_phase = FOPH_WRITE_STOB_IO_WAIT;
				result = FSO_WAIT;
			}
		} else if (fom->fo_phase == FOPH_WRITE_STOB_IO_WAIT) {
			c2_fom_block_leave(fom);
			fom->fo_rc = stio->si_rc;
			stobj = fom_obj->rh_ut_stobj;
			if (fom->fo_rc != 0)
				fom->fo_phase = FOPH_FAILURE;
			else {
				bshift = stobj->so_op->sop_block_shift(stobj);
				out_fop->fiwr_count = stio->si_count << bshift;
				fom->fo_phase = FOPH_SUCCESS;
			}

		}

		if (fom->fo_phase == FOPH_FAILURE || fom->fo_phase == FOPH_SUCCESS) {
			out_fop->fiwr_rc = fom->fo_rc;
			fom->fo_rep_fop = fom_obj->rep_fop;
			result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
							&fom->fo_tx.tx_dbtx);
			C2_UT_ASSERT(result == 0);
			result = FSO_AGAIN;
		}
	}

	if (fom->fo_phase == FOPH_FINISH) {
		/*
		   If we fail in any of the generic phase, stob io
		   is uninitialised, so no need to fini.
		 */
		if (stio->si_state != SIS_ZERO) {
			c2_stob_io_fini(stio);
			c2_stob_put(fom_obj->rh_ut_stobj);
		}
	}
	return result;
}

/**
 * Fom specific clean up function, invokes c2_fom_fini()
 */
static void reqh_ut_io_fom_fini(struct c2_fom *fom)
{
	struct reqh_ut_io_fom *fom_obj;

	fom_obj = container_of(fom, struct reqh_ut_io_fom, rh_ut_fom);
	c2_fom_fini(fom);
	c2_free(fom_obj);
}

/**
 * Fom initialization function, invoked from reqh_fop_handle.
 * Invokes c2_fom_init()
 */
static int reqh_ut_io_fom_init(struct c2_fop *fop, struct c2_fom **m)
{

	struct c2_fom_type	*fom_type;
	int			 result;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	fom_type = reqh_ut_fom_type_map(fop->f_type->ft_code);
	if (fom_type == NULL)
		return -EINVAL;
	fop->f_type->ft_fom_type = *fom_type;
	result = fop->f_type->ft_fom_type.ft_ops->fto_create(&(fop->f_type->ft_fom_type), m);
	if (result == 0) {
		(*m)->fo_fop = fop;
		c2_fom_init(*m);
	}

	return result;
}

/**
 * Function to clean reqh ut io fops
 */
static void reqh_ut_fom_io_fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(reqh_ut_fops, ARRAY_SIZE(reqh_ut_fops));
}

/**
 * Function to intialise reqh ut io fops.
 */
static int reqh_ut_fom_io_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(reqh_ut_fmts, ARRAY_SIZE(reqh_ut_fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(reqh_ut_fops, ARRAY_SIZE(reqh_ut_fops));
                if (result == 0)
                        c2_fop_object_init(&reqh_ut_fom_fop_fid_tfmt);
	}
	if (result != 0)
		reqh_ut_fom_io_fop_fini();
	return result;
}

/**
 * Helper structures and functions for ad stob.
 * These are used while performing a stob operation.
 */
struct reqh_ut_balloc {
	struct c2_mutex  rb_lock;
	c2_bindex_t      rb_next;
	struct ad_balloc rb_ballroom;
};

static struct reqh_ut_balloc *getballoc(struct ad_balloc *ballroom)
{
	return container_of(ballroom, struct reqh_ut_balloc, rb_ballroom);
}

static int reqh_ut_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
                            uint32_t bshift)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_init(&rb->rb_lock);
	return 0;
}

static void reqh_ut_balloc_fini(struct ad_balloc *ballroom)
{
	struct reqh_ut_balloc *rb = getballoc(ballroom);

	c2_mutex_fini(&rb->rb_lock);
}

static int reqh_ut_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *tx,
                             c2_bcount_t count, struct c2_ext *out)
{
	struct reqh_ut_balloc	*rb = getballoc(ballroom);

	c2_mutex_lock(&rb->rb_lock);
	out->e_start = rb->rb_next;
	out->e_end   = rb->rb_next + count;
	rb->rb_next += count;
	c2_mutex_unlock(&rb->rb_lock);
	return 0;
}

static int reqh_ut_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *tx,
                            struct c2_ext *ext)
{
	return 0;
}

static const struct ad_balloc_ops reqh_ut_balloc_ops = {
	.bo_init  = reqh_ut_balloc_init,
	.bo_fini  = reqh_ut_balloc_fini,
	.bo_alloc = reqh_ut_balloc_alloc,
	.bo_free  = reqh_ut_balloc_free,
};

static struct reqh_ut_balloc rb = {
	.rb_next = 0,
	.rb_ballroom = {
		.ab_ops = &reqh_ut_balloc_ops
	}
};

/**
 * Service handler, for incoming fops.
 * This function submits fop for processing to reqh.
 */
static int reqh_ut_service_handler(struct c2_service *service,
                                   struct c2_fop *fop,
                                   void *cookie)
{
	C2_UT_ASSERT(service != NULL);
	C2_UT_ASSERT(fop != NULL);

	c2_reqh_fop_handle(&reqh, fop, cookie);
	return 0;
}

/**
 * Creates and initialises network resources.
 */
static int reqh_ut_create_net_conn(struct c2_service_id *rsid, struct c2_net_conn **conn,
							struct c2_service *rserv)
{
	int rc;

	C2_SET0(rserv);
	rserv->s_table.not_start = reqh_ut_fopt[0]->ft_code;
	rserv->s_table.not_nr    = ARRAY_SIZE(reqh_ut_fopt);
	rserv->s_table.not_fopt  = reqh_ut_fopt;
	rserv->s_handler         = &reqh_ut_service_handler;

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
	int                      result;
	unsigned long	         i;
	char                     opath[64];
	char                     dpath[64];
	const char              *path;
	c2_time_t                rdelay;
	c2_time_t                cdelay;
	c2_time_t                wdelay;
	c2_time_t                wait_for_rep;

	struct c2_service_id	 rsid = { .si_uuid = "reqh_ut_node" };
	struct c2_net_conn	*conn;
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

	backid.si_bits.u_hi = 0x0;
	backid.si_bits.u_lo = 0xdf11e;
	path = "reqh_ut";

	/* Initialize processors */
	if (!c2_processor_is_initialized()) {
		result = c2_processors_init();
		C2_UT_ASSERT(result == 0);
	}

	result = reqh_ut_fom_io_fop_init();
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

	result = c2_ad_stob_setup(sdom, &db, bstore, &rb.rb_ballroom);
	C2_UT_ASSERT(result == 0);

	c2_stob_put(bstore);

	/* Create or open a stob into which to store the record. */
	result = bdom->sd_ops->sdo_stob_find(bdom, &reqh_addb_stob_id, &reqh_addb_stob);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(reqh_addb_stob->so_state == CSS_UNKNOWN);

	result = c2_stob_create(reqh_addb_stob, NULL);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(reqh_addb_stob->so_state == CSS_EXISTS);

	/* Write addb record into stob */
	c2_addb_choose_store_media(C2_ADDB_REC_STORE_STOB, c2_addb_stob_add,
					  reqh_addb_stob, NULL);

	reqh_ut_create_net_conn(&rsid, &conn, &rservice);

	/* Initialising request handler */
	result =  c2_reqh_init(&reqh, NULL, NULL, sdom, &fol, &rservice);
	C2_UT_ASSERT(result == 0);

	reqh_ut_status = true;
	/* Create listening thread to accept async reply's */
	for (i = 0; i < 10; ++i) {
		reqh_ut_create_send(conn, i, i);
		if (!reqh_ut_status)
			goto cleanup;
	}

	while (reply < 10)
		c2_nanosleep(c2_time_set(&cdelay, 1, 0), NULL);

	for (i = 0; i < 10; ++i) {
		/* work around fix to make ut compatible on VM */
		c2_nanosleep(c2_time_set(&wdelay, 1, 0), NULL);
		reqh_ut_write_send(conn, i, i);
		if (!reqh_ut_status)
			goto cleanup;
	}

	for (i = 0; i < 10; ++i) {
		/* work around fix to make ut compatible on VM */
		c2_nanosleep(c2_time_set(&rdelay, 1, 0), NULL);
		reqh_ut_read_send(conn, i, i);
		if (!reqh_ut_status)
			goto cleanup;
	}

	while (reply < 30)
		c2_nanosleep(c2_time_set(&wait_for_rep, 1, 0), NULL);

cleanup:
	/* Clean up network connections */
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
	reqh_ut_fom_io_fop_fini();
	if (c2_processor_is_initialized())
		c2_processors_fini();
}

const struct c2_test_suite reqh_ut = {
	.ts_name = "reqh-ut... this takes about 30 seconds",
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
