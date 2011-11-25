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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/chan.h"
#include "lib/list.h"

#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/bulk_sunrpc.h"
#include "rpc/rpc2.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fop_onwire.h"
#include "xcode/bufvec_xcode.h"

#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "stob/io_fop_k.h"
#else
#include "stob/io_fop_u.h"
#endif

#include "stob/io_fop.ff"

/**
   @defgroup stobio
   @{
 */

/**
 * Read fop specific fom execution phases
 */
enum stob_read_fom_phase {
	FOPH_READ_STOB_IO = FOPH_NR + 1,
	FOPH_READ_STOB_IO_WAIT
};

/**
 * Write fop specific fom execution phases
 */
enum stob_write_fom_phase {
	FOPH_WRITE_STOB_IO = FOPH_NR + 1,
	FOPH_WRITE_STOB_IO_WAIT
};

static int stob_io_fom_init(struct c2_fop *fop, struct c2_fom **m);
static void rpc_item_reply_cb(struct c2_rpc_item *item, int rc);

/**
 * RPC item operations structures
 */
static const struct c2_rpc_item_type_ops default_rpc_item_type_ops = {
        .rito_replied = rpc_item_reply_cb,
        .rito_item_size = c2_fop_item_type_default_onwire_size,
        .rito_encode = c2_fop_item_type_default_encode,
        .rito_decode = c2_fop_item_type_default_decode,
};

static const struct c2_rpc_item_type_ops default_rep_rpc_item_type_ops = {
        .rito_item_size = c2_fop_item_type_default_onwire_size,
        .rito_encode = c2_fop_item_type_default_encode,
        .rito_decode = c2_fop_item_type_default_decode,
};

/**
 * Fop operation structures for corresponding fops.
 */
static const struct c2_fop_type_ops default_fop_ops = {
	.fto_fom_init = stob_io_fom_init,
        .fto_size_get = c2_xcode_fop_size_get,
};

static const struct c2_fop_type_ops default_rep_fop_ops = {
        .fto_size_get = c2_xcode_fop_size_get,
};

/**
 * Fop type declarations for corresponding fops
 */
C2_FOP_TYPE_DECLARE(c2_stob_io_create, "stob_create", &default_fop_ops,
		    C2_STOB_IO_CREATE_REQ_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		    &default_rpc_item_type_ops);
C2_FOP_TYPE_DECLARE(c2_stob_io_read, "stob_read", &default_fop_ops,
		    C2_STOB_IO_READ_REQ_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		    &default_rpc_item_type_ops);
C2_FOP_TYPE_DECLARE(c2_stob_io_write, "stob_write", &default_fop_ops,
		    C2_STOB_IO_WRITE_REQ_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		    &default_rpc_item_type_ops);

C2_FOP_TYPE_DECLARE(c2_stob_io_create_rep, "stob_create reply", &default_rep_fop_ops,
		    C2_STOB_IO_CREATE_REPLY_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY, &default_rep_rpc_item_type_ops);
C2_FOP_TYPE_DECLARE(c2_stob_io_read_rep, "stob_read reply",  &default_rep_fop_ops,
		    C2_STOB_IO_READ_REPLY_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY, &default_rep_rpc_item_type_ops);
C2_FOP_TYPE_DECLARE(c2_stob_io_write_rep, "stob_write reply", &default_rep_fop_ops,
		    C2_STOB_IO_WRITE_REPLY_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY, &default_rep_rpc_item_type_ops);

/**
 * Fop type structures required for initialising corresponding fops.
 */
static struct c2_fop_type *stob_fops[] = {
	&c2_stob_io_create_fopt,
	&c2_stob_io_write_fopt,
	&c2_stob_io_read_fopt,

	&c2_stob_io_create_rep_fopt,
	&c2_stob_io_write_rep_fopt,
	&c2_stob_io_read_rep_fopt,
};

static struct c2_fop_type_format *stob_fmts[] = {
        &stob_io_fop_fid_tfmt,
};

/**
 * A generic fom structure to hold fom, reply fop, storage object
 * and storage io object, used for fop execution.
 */
struct c2_stob_io_fom {
	/** Generic c2_fom object. */
	struct c2_fom			 sif_fom;
	/** Reply FOP associated with request FOP above. */
	struct c2_fop			*sif_rep_fop;
	/** Stob object on which this FOM is acting. */
	struct c2_stob			*sif_stobj;
	/** Stob IO packet for the operation. */
	struct c2_stob_io		 sif_stio;
};

static int stob_create_fom_create(struct c2_fom_type *t, struct c2_fom **out);
static int stob_read_fom_create(struct c2_fom_type *t, struct c2_fom **out);
static int stob_write_fom_create(struct c2_fom_type *t, struct c2_fom **out);

static int stob_create_fom_state(struct c2_fom *fom);
static int stob_read_fom_state(struct c2_fom *fom);
static int stob_write_fom_state(struct c2_fom *fom);

static void stob_io_fom_fini(struct c2_fom *fom);
static size_t stob_find_fom_home_locality(const struct c2_fom *fom);

/**
 * Operation structures for respective foms
 */
static struct c2_fom_ops stob_create_fom_ops = {
	.fo_fini = stob_io_fom_fini,
	.fo_state = stob_create_fom_state,
	.fo_home_locality = stob_find_fom_home_locality,
};

static struct c2_fom_ops stob_write_fom_ops = {
	.fo_fini = stob_io_fom_fini,
	.fo_state = stob_write_fom_state,
	.fo_home_locality = stob_find_fom_home_locality,
};

static struct c2_fom_ops stob_read_fom_ops = {
	.fo_fini = stob_io_fom_fini,
	.fo_state = stob_read_fom_state,
	.fo_home_locality = stob_find_fom_home_locality,
};

/**
 * Fom type operations structures for corresponding foms.
 */
static const struct c2_fom_type_ops stob_create_fom_type_ops = {
	.fto_create = stob_create_fom_create,
};

static const struct c2_fom_type_ops stob_read_fom_type_ops = {
	.fto_create = stob_read_fom_create,
};

static const struct c2_fom_type_ops stob_write_fom_type_ops = {
	.fto_create = stob_write_fom_create,
};

static struct c2_fom_type stob_create_fom_mopt = {
	.ft_ops = &stob_create_fom_type_ops,
};

static struct c2_fom_type stob_read_fom_mopt = {
	.ft_ops = &stob_read_fom_type_ops,
};

static struct c2_fom_type stob_write_fom_mopt = {
	.ft_ops = &stob_write_fom_type_ops,
};

static struct c2_fom_type *stob_fom_types[] = {
	&stob_create_fom_mopt,
	&stob_write_fom_mopt,
	&stob_read_fom_mopt,
};

static void rpc_item_reply_cb(struct c2_rpc_item *item, int rc)
{
	C2_PRE(item != NULL);
        C2_PRE(c2_chan_has_waiters(&item->ri_chan));

        c2_chan_signal(&item->ri_chan);
}

/**
 * Function to map a fop to its corresponding fom
 */
static struct c2_fom_type *stob_fom_type_map(c2_fop_type_code_t code)
{
	C2_ASSERT(IS_IN_ARRAY((code - C2_STOB_IO_CREATE_REQ_OPCODE), stob_fom_types));

	return stob_fom_types[code - C2_STOB_IO_CREATE_REQ_OPCODE];
}

/**
 * Function to locate a storage object.
 */
static struct c2_stob *stob_object_find(const struct stob_io_fop_fid *fid,
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
	C2_ASSERT(result == 0);
	result = c2_stob_locate(obj, tx);
	return obj;
}

/**
 * Create fom helper
 */
static int stob_fom_create_helper(struct c2_fom_type *t, struct c2_fom_ops *fom_ops,
				  struct c2_fop_type *fop_type, struct c2_fom **out)
{
	struct c2_fom		*fom;
	struct c2_stob_io_fom	*fom_obj;
	C2_PRE(t != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom = &fom_obj->sif_fom;
	fom->fo_type = t;

	fom->fo_ops = fom_ops;
	fom_obj->sif_rep_fop =
		c2_fop_alloc(fop_type, NULL);
	if (fom_obj->sif_rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}

	fom_obj->sif_stobj = NULL;
	*out = fom;
	return 0;
}

/**
 * Creates a fom for create fop.
 */
static int stob_create_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	return stob_fom_create_helper(t, &stob_create_fom_ops,
				      &c2_stob_io_create_rep_fopt, out);
}

/**
 * Creates a fom for write fop.
 */
static int stob_write_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	return stob_fom_create_helper(t, &stob_write_fom_ops,
				      &c2_stob_io_write_rep_fopt, out);
}

/**
 * Creates a fom for read fop.
 */
static int stob_read_fom_create(struct c2_fom_type *t, struct c2_fom **out)
{
	return stob_fom_create_helper(t, &stob_read_fom_ops,
				      &c2_stob_io_read_rep_fopt, out);
}

/**
 * Finds home locality for this type of fom.
 * This function, using a basic hashing method
 * locates a home locality for a particular type
 * of fome, inorder to have same locality of
 * execution for a certain type of fom.
 */
static size_t stob_find_fom_home_locality(const struct c2_fom *fom)
{
	size_t iloc;

	if (fom == NULL)
		return -EINVAL;

	switch (fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode) {
	case C2_STOB_IO_CREATE_REQ_OPCODE: {
		struct c2_stob_io_create *fop;
		uint64_t oid;
		fop = c2_fop_data(fom->fo_fop);
		oid = fop->fic_object.f_oid;
		iloc = oid;
		break;
	}
	case C2_STOB_IO_WRITE_REQ_OPCODE: {
		struct c2_stob_io_read *fop;
		uint64_t oid;
		fop = c2_fop_data(fom->fo_fop);
		oid = fop->fir_object.f_oid;
		iloc = oid;
		break;
	}
	case C2_STOB_IO_READ_REQ_OPCODE: {
		struct c2_stob_io_write *fop;
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
static int stob_create_fom_state(struct c2_fom *fom)
{
	struct c2_stob_io_create	*in_fop;
	struct c2_stob_io_create_rep	*out_fop;
	struct c2_stob_io_fom		*fom_obj;
	struct c2_rpc_item              *item;
	struct c2_fop                   *fop;
	int                             result;

	C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
			C2_STOB_IO_CREATE_REQ_OPCODE);

	fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
	if (fom->fo_phase < FOPH_NR) {
		result = c2_fom_state_generic(fom);
	} else {
		in_fop = c2_fop_data(fom->fo_fop);
		out_fop = c2_fop_data(fom_obj->sif_rep_fop);

		fom_obj->sif_stobj = stob_object_find(&in_fop->fic_object, &fom->fo_tx, fom);

		result = c2_stob_create(fom_obj->sif_stobj, &fom->fo_tx);
		out_fop->ficr_rc = result;
		fop = fom_obj->sif_rep_fop;
		item = c2_fop_to_rpc_item(fop);
		item->ri_type = &fop->f_type->ft_rpc_item_type;
		item->ri_group = NULL;
		fom->fo_rep_fop = fom_obj->sif_rep_fop;
		fom->fo_rc = result;
		if (result != 0)
			fom->fo_phase = FOPH_FAILURE;
		 else
			fom->fo_phase = FOPH_SUCCESS;

		result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol, &fom->fo_tx.tx_dbtx);
		C2_ASSERT(result == 0);
		result = FSO_AGAIN;
	}

	if (fom->fo_phase == FOPH_FINISH && fom->fo_rc == 0)
		c2_stob_put(fom_obj->sif_stobj);

	return result;
}

/**
 * A simple non blocking read fop specific fom
 * state method implemention.
 */
static int stob_read_fom_state(struct c2_fom *fom)
{
        struct c2_stob_io_read      *in_fop;
        struct c2_stob_io_read_rep  *out_fop;
        struct c2_stob_io_fom           *fom_obj;
        struct c2_stob_io               *stio;
        struct c2_stob                  *stobj;
	struct c2_rpc_item              *item;
	struct c2_fop                   *fop;
        void                            *addr;
        c2_bcount_t                      count;
        c2_bcount_t                      offset;
        uint32_t                         bshift;
        int                              result;

        C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
			C2_STOB_IO_READ_REQ_OPCODE);

        fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
        stio = &fom_obj->sif_stio;
        if (fom->fo_phase < FOPH_NR) {
                result = c2_fom_state_generic(fom);
        } else {
                out_fop = c2_fop_data(fom_obj->sif_rep_fop);
                C2_ASSERT(out_fop != NULL);

                if (fom->fo_phase == FOPH_READ_STOB_IO) {

                        in_fop = c2_fop_data(fom->fo_fop);
                        C2_ASSERT(in_fop != NULL);
                        fom_obj->sif_stobj = stob_object_find(&in_fop->fir_object,
                                                                &fom->fo_tx, fom);

                        stobj =  fom_obj->sif_stobj;
                        bshift = stobj->so_op->sop_block_shift(stobj);

                        addr = c2_stob_addr_pack(&out_fop->firr_value, bshift);
                        count = 1 >> bshift;
                        offset = 0;

                        c2_stob_io_init(stio);

                        stio->si_user = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&addr,
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
                        stobj = fom_obj->sif_stobj;
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
			fop = fom_obj->sif_rep_fop;
			item = c2_fop_to_rpc_item(fop);
			item->ri_type = &fop->f_type->ft_rpc_item_type;
                        item->ri_group = NULL;
                        fom->fo_rep_fop = fom_obj->sif_rep_fop;
                        result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                                        &fom->fo_tx.tx_dbtx);
                        C2_ASSERT(result == 0);
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
                        c2_stob_put(fom_obj->sif_stobj);
                }
        }

        return result;
}

/**
 * A simple non blocking write fop specific fom
 * state method implemention.
 */
static int stob_write_fom_state(struct c2_fom *fom)
{
        struct c2_stob_io_write     *in_fop;
        struct c2_stob_io_write_rep *out_fop;
        struct c2_stob_io_fom           *fom_obj;
        struct c2_stob_io               *stio;
        struct c2_stob                  *stobj;
	struct c2_rpc_item              *item;
	struct c2_fop                   *fop;
        void                            *addr;
        c2_bcount_t                      count;
        c2_bindex_t                      offset;
        uint32_t                         bshift;
        int                              result;

        C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
			C2_STOB_IO_WRITE_REQ_OPCODE);

        fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
        stio = &fom_obj->sif_stio;

        if (fom->fo_phase < FOPH_NR) {
                result = c2_fom_state_generic(fom);
        } else {
                out_fop = c2_fop_data(fom_obj->sif_rep_fop);
                C2_ASSERT(out_fop != NULL);

                if (fom->fo_phase == FOPH_WRITE_STOB_IO) {
                        in_fop = c2_fop_data(fom->fo_fop);
                        C2_ASSERT(in_fop != NULL);

                        fom_obj->sif_stobj = stob_object_find(&in_fop->fiw_object, &fom->fo_tx, fom);

                        stobj = fom_obj->sif_stobj;
                        bshift = stobj->so_op->sop_block_shift(stobj);

                        addr = c2_stob_addr_pack(&in_fop->fiw_value, bshift);
                        count = 1 >> bshift;
                        offset = 0;

                        c2_stob_io_init(stio);

                        stio->si_user = (struct c2_bufvec)C2_BUFVEC_INIT_BUF(&addr, &count);

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
                        stobj = fom_obj->sif_stobj;
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
			fop = fom_obj->sif_rep_fop;
			item = c2_fop_to_rpc_item(fop);
			item->ri_type = &fop->f_type->ft_rpc_item_type;
			item->ri_group = NULL;
                        fom->fo_rep_fop = fom_obj->sif_rep_fop;
                        result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                                        &fom->fo_tx.tx_dbtx);
                        C2_ASSERT(result == 0);
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
                        c2_stob_put(fom_obj->sif_stobj);
                }
        }
        return result;
}

/**
 * Fom initialization function, invoked from reqh_fop_handle.
 * Invokes c2_fom_init()
 */
static int stob_io_fom_init(struct c2_fop *fop, struct c2_fom **m)
{

	struct c2_fom_type	*fom_type;
	int			 result;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	fom_type = stob_fom_type_map(fop->f_type->ft_rpc_item_type.rit_opcode);
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
 * Fom specific clean up function, invokes c2_fom_fini()
 */
static void stob_io_fom_fini(struct c2_fom *fom)
{
	struct c2_stob_io_fom *fom_obj;

	fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
	c2_fom_fini(fom);
	c2_free(fom_obj);
}

void c2_stob_io_fop_fini(void);

/**
 * Function to intialise stob io fops.
 */
int c2_stob_io_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(stob_fmts, ARRAY_SIZE(stob_fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(stob_fops, ARRAY_SIZE(stob_fops));
                if (result == 0)
                        c2_fop_object_init(&stob_io_fop_fid_tfmt);
	}
	if (result != 0)
		c2_stob_io_fop_fini();
	return result;
}
C2_EXPORTED(c2_stob_io_fop_init);

/**
 * Function to clean stob io fops
 */
void c2_stob_io_fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(stob_fops, ARRAY_SIZE(stob_fops));
}
C2_EXPORTED(c2_stob_io_fop_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
