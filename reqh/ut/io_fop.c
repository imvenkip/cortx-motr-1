/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "config.h"
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
#include "stob/stob.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "rpc/rpc2.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fop_item_type.h"
#include "reqh/ut/io_fop_xc.h"

/**
   @defgroup stobio
   @{
 */

/**
 * Read fop specific fom execution phases
 */
enum stob_read_fom_phase {
	C2_FOPH_READ_STOB_IO = C2_FOPH_NR + 1,
	C2_FOPH_READ_STOB_IO_WAIT
};

/**
 * Write fop specific fom execution phases
 */
enum stob_write_fom_phase {
	C2_FOPH_WRITE_STOB_IO = C2_FOPH_NR + 1,
	C2_FOPH_WRITE_STOB_IO_WAIT
};

struct c2_fop_type c2_stob_io_create_fopt;
struct c2_fop_type c2_stob_io_read_fopt;
struct c2_fop_type c2_stob_io_write_fopt;
struct c2_fop_type c2_stob_io_create_rep_fopt;
struct c2_fop_type c2_stob_io_read_rep_fopt;
struct c2_fop_type c2_stob_io_write_rep_fopt;

/**
 * RPC item operations structures
 */
/**
 * Fop operation structures for corresponding fops.
 */
static const struct c2_fop_type_ops default_fop_ops = {
        .fto_size_get = c2_fop_xcode_length,
};

static const struct c2_fop_type_ops default_rep_fop_ops = {
        .fto_size_get = c2_fop_xcode_length,
};

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

extern struct c2_stob_domain *reqh_ut_stob_domain_find(void);
static int stob_create_fom_create(struct c2_fop *fop, struct c2_fom **out);
static int stob_read_fom_create(struct c2_fop *fop, struct c2_fom **out);
static int stob_write_fom_create(struct c2_fop *fop, struct c2_fom **out);

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

/**
 * Function to map a fop to its corresponding fom
 */
static struct c2_fom_type *stob_fom_type_map(c2_fop_type_code_t code)
{
	C2_ASSERT(IS_IN_ARRAY((code - C2_STOB_IO_CREATE_REQ_OPCODE),
			      stob_fom_types));

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
	fom_stdom = reqh_ut_stob_domain_find();
	C2_ASSERT(fom_stdom != NULL);
	result = c2_stob_find(fom_stdom, &id, &obj);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(obj, tx);
	return obj;
}

/**
 * Fom initialization function, invoked from reqh_fop_handle.
 * Invokes c2_fom_init()
 */
static int stob_io_fop_fom_create_helper(struct c2_fop *fop,
		struct c2_fom_ops *fom_ops, struct c2_fop_type *fop_type,
		struct c2_fom **out)
{
	struct c2_stob_io_fom *fom_obj;

	C2_PRE(fop != NULL);
	C2_PRE(fom_ops != NULL);
	C2_PRE(fop_type != NULL);
	C2_PRE(out != NULL);

	fom_obj= c2_alloc(sizeof *fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;
	fom_obj->sif_rep_fop = c2_fop_alloc(fop_type, NULL);
	if (fom_obj->sif_rep_fop == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}
	fom_obj->sif_stobj = NULL;

	c2_fom_init(&fom_obj->sif_fom, &fop->f_type->ft_fom_type, fom_ops, fop,
		    fom_obj->sif_rep_fop);

	*out = &fom_obj->sif_fom;
	return 0;
}

/**
 * Creates a fom for create fop.
 */
static int stob_create_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return stob_io_fop_fom_create_helper(fop, &stob_create_fom_ops,
				      &c2_stob_io_create_rep_fopt, out);
}

/**
 * Creates a fom for write fop.
 */
static int stob_write_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return stob_io_fop_fom_create_helper(fop, &stob_write_fom_ops,
				      &c2_stob_io_write_rep_fopt, out);
}

/**
 * Creates a fom for read fop.
 */
static int stob_read_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return stob_io_fop_fom_create_helper(fop, &stob_read_fom_ops,
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
	if (fom->fo_phase < C2_FOPH_NR) {
		result = c2_fom_state_generic(fom);
	} else {
		in_fop = c2_fop_data(fom->fo_fop);
		out_fop = c2_fop_data(fom_obj->sif_rep_fop);

		fom_obj->sif_stobj = stob_object_find(&in_fop->fic_object,
						      &fom->fo_tx, fom);

		result = c2_stob_create(fom_obj->sif_stobj, &fom->fo_tx);
		out_fop->ficr_rc = result;
		fop = fom_obj->sif_rep_fop;
		item = c2_fop_to_rpc_item(fop);
		item->ri_type = &fop->f_type->ft_rpc_item_type;
		item->ri_group = NULL;
		fom->fo_rep_fop = fom_obj->sif_rep_fop;
		fom->fo_rc = result;
		if (result != 0)
			fom->fo_phase = C2_FOPH_FAILURE;
		 else
			fom->fo_phase = C2_FOPH_SUCCESS;

		result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
					    &fom->fo_tx.tx_dbtx);
		C2_ASSERT(result == 0);
		result = C2_FSO_AGAIN;
	}

	if (fom->fo_phase == C2_FOPH_FINISH && fom->fo_rc == 0)
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
        int                              result = 0;

        C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
			C2_STOB_IO_READ_REQ_OPCODE);

        fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
        stio = &fom_obj->sif_stio;
        if (fom->fo_phase < C2_FOPH_NR) {
                result = c2_fom_state_generic(fom);
        } else {
                out_fop = c2_fop_data(fom_obj->sif_rep_fop);
                C2_ASSERT(out_fop != NULL);

                if (fom->fo_phase == C2_FOPH_READ_STOB_IO) {

                        in_fop = c2_fop_data(fom->fo_fop);
                        C2_ASSERT(in_fop != NULL);
                        fom_obj->sif_stobj = stob_object_find(
				&in_fop->fir_object,
				&fom->fo_tx, fom);

                        stobj =  fom_obj->sif_stobj;
                        bshift = stobj->so_op->sop_block_shift(stobj);

                        addr = c2_stob_addr_pack(&out_fop->firr_value, bshift);
                        count = 1 >> bshift;
                        offset = 0;

                        c2_stob_io_init(stio);

                        stio->si_user = (struct c2_bufvec)
				C2_BUFVEC_INIT_BUF(&addr, &count);

                        stio->si_stob.iv_vec.v_nr    = 1;
                        stio->si_stob.iv_vec.v_count = &count;
                        stio->si_stob.iv_index       = &offset;

                        stio->si_opcode = SIO_READ;
                        stio->si_flags  = 0;

                        c2_fom_wait_on(fom, &stio->si_wait, &fom->fo_cb);
                        result = c2_stob_io_launch(stio, stobj, &fom->fo_tx, NULL);

                        if (result != 0) {
                                c2_fom_callback_cancel(&fom->fo_cb);
                                fom->fo_rc = result;
                                fom->fo_phase = C2_FOPH_FAILURE;
                        } else {
                                fom->fo_phase = C2_FOPH_READ_STOB_IO_WAIT;
                                result = C2_FSO_WAIT;
                        }
                } else if (fom->fo_phase == C2_FOPH_READ_STOB_IO_WAIT) {
                        fom->fo_rc = stio->si_rc;
                        stobj = fom_obj->sif_stobj;
                        if (fom->fo_rc != 0)
                                fom->fo_phase = C2_FOPH_FAILURE;
                        else {
                                bshift = stobj->so_op->sop_block_shift(stobj);
                                out_fop->firr_count = stio->si_count << bshift;
                                fom->fo_phase = C2_FOPH_SUCCESS;
                        }

                }

                if (fom->fo_phase == C2_FOPH_FAILURE ||
                    fom->fo_phase == C2_FOPH_SUCCESS) {
                        out_fop->firr_rc = fom->fo_rc;
			fop = fom_obj->sif_rep_fop;
			item = c2_fop_to_rpc_item(fop);
			item->ri_type = &fop->f_type->ft_rpc_item_type;
                        item->ri_group = NULL;
                        fom->fo_rep_fop = fom_obj->sif_rep_fop;
                        result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                                        &fom->fo_tx.tx_dbtx);
                        C2_ASSERT(result == 0);
                        result = C2_FSO_AGAIN;
                }

        }

        if (fom->fo_phase == C2_FOPH_FINISH) {
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
        int                              result = 0;

        C2_PRE(fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode ==
			C2_STOB_IO_WRITE_REQ_OPCODE);

        fom_obj = container_of(fom, struct c2_stob_io_fom, sif_fom);
        stio = &fom_obj->sif_stio;

        if (fom->fo_phase < C2_FOPH_NR) {
                result = c2_fom_state_generic(fom);
        } else {
                out_fop = c2_fop_data(fom_obj->sif_rep_fop);
                C2_ASSERT(out_fop != NULL);

                if (fom->fo_phase == C2_FOPH_WRITE_STOB_IO) {
                        in_fop = c2_fop_data(fom->fo_fop);
                        C2_ASSERT(in_fop != NULL);

                        fom_obj->sif_stobj = stob_object_find(
				&in_fop->fiw_object, &fom->fo_tx, fom);

                        stobj = fom_obj->sif_stobj;
                        bshift = stobj->so_op->sop_block_shift(stobj);

                        addr = c2_stob_addr_pack(&in_fop->fiw_value, bshift);
                        count = 1 >> bshift;
                        offset = 0;

                        c2_stob_io_init(stio);

                        stio->si_user = (struct c2_bufvec)
				C2_BUFVEC_INIT_BUF(&addr, &count);

                        stio->si_stob.iv_vec.v_nr    = 1;
                        stio->si_stob.iv_vec.v_count = &count;
                        stio->si_stob.iv_index       = &offset;
                        stio->si_opcode = SIO_WRITE;
                        stio->si_flags  = 0;

                        c2_fom_wait_on(fom, &stio->si_wait, &fom->fo_cb);
                        result = c2_stob_io_launch(stio,
						   stobj, &fom->fo_tx, NULL);

                        if (result != 0) {
                                c2_fom_callback_cancel(&fom->fo_cb);
                                fom->fo_rc = result;
                                fom->fo_phase = C2_FOPH_FAILURE;
                        } else {
                                fom->fo_phase = C2_FOPH_WRITE_STOB_IO_WAIT;
                                result = C2_FSO_WAIT;
                        }
                } else if (fom->fo_phase == C2_FOPH_WRITE_STOB_IO_WAIT) {
                        fom->fo_rc = stio->si_rc;
                        stobj = fom_obj->sif_stobj;
                        if (fom->fo_rc != 0)
                                fom->fo_phase = C2_FOPH_FAILURE;
                        else {
                                bshift = stobj->so_op->sop_block_shift(stobj);
                                out_fop->fiwr_count = stio->si_count << bshift;
                                fom->fo_phase = C2_FOPH_SUCCESS;
                        }

                }

                if (fom->fo_phase == C2_FOPH_FAILURE ||
                    fom->fo_phase == C2_FOPH_SUCCESS) {
                        out_fop->fiwr_rc = fom->fo_rc;
			fop = fom_obj->sif_rep_fop;
			item = c2_fop_to_rpc_item(fop);
			item->ri_type = &fop->f_type->ft_rpc_item_type;
			item->ri_group = NULL;
                        fom->fo_rep_fop = fom_obj->sif_rep_fop;
                        result = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                                        &fom->fo_tx.tx_dbtx);
                        C2_ASSERT(result == 0);
                        result = C2_FSO_AGAIN;
                }
        }

        if (fom->fo_phase == C2_FOPH_FINISH) {
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
	int		    result;
	int		    i;
	c2_fop_type_code_t  code;
	struct c2_fom_type *fom_type;
	struct c2_fop_type *fop_type;

	c2_xc_io_fop_xc_init();
	result = C2_FOP_TYPE_INIT(&c2_stob_io_create_fopt,
				  .name      = "Stob create",
				  .opcode    = C2_STOB_IO_CREATE_REQ_OPCODE,
				  .xt        = c2_stob_io_create_xc,
				  .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					       C2_RPC_ITEM_TYPE_MUTABO,
				  .fop_ops   = &default_fop_ops) ?:
		C2_FOP_TYPE_INIT(&c2_stob_io_read_fopt,
				 .name      = "Stob read",
				 .opcode    = C2_STOB_IO_READ_REQ_OPCODE,
				 .xt        = c2_stob_io_read_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fop_ops   = &default_fop_ops) ?:
		C2_FOP_TYPE_INIT(&c2_stob_io_write_fopt,
				 .name      = "Stob write",
				 .opcode    = C2_STOB_IO_WRITE_REQ_OPCODE,
				 .xt        = c2_stob_io_write_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fop_ops   = &default_fop_ops) ?:
		C2_FOP_TYPE_INIT(&c2_stob_io_create_rep_fopt,
				 .name      = "Stob create reply",
				 .opcode    = C2_STOB_IO_CREATE_REPLY_OPCODE,
				 .xt        = c2_stob_io_create_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .fop_ops   = &default_rep_fop_ops) ?:
		C2_FOP_TYPE_INIT(&c2_stob_io_read_rep_fopt,
				 .name      = "Stob read reply",
				 .opcode    = C2_STOB_IO_READ_REPLY_OPCODE,
				 .xt        = c2_stob_io_read_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .fop_ops   = &default_rep_fop_ops) ?:
		C2_FOP_TYPE_INIT(&c2_stob_io_write_rep_fopt,
				 .name      = "Stob write reply",
				 .opcode    = C2_STOB_IO_WRITE_REPLY_OPCODE,
				 .xt        = c2_stob_io_write_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .fop_ops   = &default_rep_fop_ops);
	if (result == 0) {
		for (i = 0; i < ARRAY_SIZE(stob_fops); ++i) {
			fop_type = stob_fops[i];
			if ((fop_type->ft_rpc_item_type.rit_flags &
						C2_RPC_ITEM_TYPE_REQUEST) == 0)
				continue;
			code = fop_type->ft_rpc_item_type.rit_opcode;
			fom_type = stob_fom_type_map(code);
			C2_ASSERT(fom_type != NULL);
			fop_type->ft_fom_type = *fom_type;
		}
	} else
		c2_stob_io_fop_fini();

	return result;
}

/**
 * Function to clean stob io fops
 */
void c2_stob_io_fop_fini(void)
{
	c2_fop_type_fini(&c2_stob_io_write_rep_fopt);
	c2_fop_type_fini(&c2_stob_io_read_rep_fopt);
	c2_fop_type_fini(&c2_stob_io_create_rep_fopt);
	c2_fop_type_fini(&c2_stob_io_write_fopt);
	c2_fop_type_fini(&c2_stob_io_read_fopt);
	c2_fop_type_fini(&c2_stob_io_create_fopt);
	c2_xc_io_fop_xc_fini();
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
