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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/19/2010
 */
#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/memory.h"
#include "lib/misc.h"   /* C2_SET0 */
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/vec.h"
#include "fop/fop.h"
#include "fop/fop_iterator.h"

/**
   @addtogroup fop
   @{
 */

extern struct c2_addb_ctx_type c2_fop_addb_ctx;

struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data)
{
	struct c2_fop *fop;

	C2_ALLOC_PTR(fop);
	if (fop != NULL) {
		c2_bcount_t nob;

		fop->f_type = fopt;
		fop->f_private = NULL;
		#ifndef __KERNEL__
		//c2_rpc_item_init(&fop->f_item);
		/* Associate rpc_item_type with the rpc item. */
		//fop->f_item.ri_type = &fopt->ft_ri_type->fri_i_type;
		#endif
		fop->f_item.ri_type = fopt->ft_ri_type;
		nob = fopt->ft_top->fft_layout->fm_sizeof;
		if (data == NULL)
			data = c2_alloc(nob);
		if (data != NULL) {
			fop->f_data.fd_data = data;
			c2_addb_ctx_init(&fop->f_addb, &c2_fop_addb_ctx,
					 &fopt->ft_addb);
			c2_list_link_init(&fop->f_link);
		} else {
			c2_free(fop);
			fop = NULL;
		}
	}
	return fop;
}
C2_EXPORTED(c2_fop_alloc);

void c2_fop_free(struct c2_fop *fop)
{
	if (fop != NULL) {
		c2_addb_ctx_fini(&fop->f_addb);
		c2_free(fop->f_data.fd_data);
		c2_list_link_fini(&fop->f_link);
		c2_free(fop);
	}
}
C2_EXPORTED(c2_fop_free);

void *c2_fop_data(struct c2_fop *fop)
{
	return fop->f_data.fd_data;

}
C2_EXPORTED(c2_fop_data);

/*
 * fop-fol interaction.
 */

#ifdef __KERNEL__

/* XXX for now */

int fop_fol_type_init(struct c2_fop_type *fopt)
{
	return 0;
}

void fop_fol_type_fini(struct c2_fop_type *fopt)
{
}

#else /* !__KERNEL__ */

static const struct c2_fol_rec_type_ops c2_fop_fol_default_ops;

int fop_fol_type_init(struct c2_fop_type *fopt)
{
	struct c2_fol_rec_type *rtype;

	C2_CASSERT(sizeof rtype->rt_opcode == sizeof fopt->ft_code);

	rtype = &fopt->ft_rec_type;
	rtype->rt_name   = fopt->ft_name;
	rtype->rt_opcode = fopt->ft_code;
	if (fopt->ft_ops != NULL && fopt->ft_ops->fto_rec_ops != NULL)
		rtype->rt_ops = fopt->ft_ops->fto_rec_ops;
	else
		rtype->rt_ops = &c2_fop_fol_default_ops;
	return c2_fol_rec_type_register(rtype);
}

void fop_fol_type_fini(struct c2_fop_type *fopt)
{
	c2_fol_rec_type_unregister(&fopt->ft_rec_type);
}

int c2_fop_fol_rec_add(struct c2_fop *fop, struct c2_fol *fol,
		       struct c2_db_tx *tx)
{
	struct c2_fop_type    *fopt;
	struct c2_fol_rec_desc desc;

	fopt = fop->f_type;
	C2_CASSERT(sizeof desc.rd_header.rh_opcode == sizeof fopt->ft_code);

	C2_SET0(&desc);
	desc.rd_type               = &fop->f_type->ft_rec_type;
	desc.rd_type_private       = fop;
	desc.rd_lsn                = c2_fol_lsn_allocate(fol);
	/* XXX an arbitrary number for now */
	desc.rd_header.rh_refcount = 1;
	/*
	 * @todo fill the rest by iterating through fop fields.
	 */
	return c2_fol_add(fol, tx, &desc);
}
C2_EXPORTED(c2_fop_fol_rec_add);

static size_t fol_pack_size(struct c2_fol_rec_desc *desc)
{
	struct c2_fop *fop = desc->rd_type_private;

	return fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
}

static void fol_pack(struct c2_fol_rec_desc *desc, void *buf)
{
	struct c2_fop *fop = desc->rd_type_private;

	memcpy(buf, c2_fop_data(fop), fol_pack_size(desc));
}

static const struct c2_fol_rec_type_ops c2_fop_fol_default_ops = {
	.rto_commit     = NULL,
	.rto_abort      = NULL,
	.rto_persistent = NULL,
	.rto_cull       = NULL,
	.rto_open       = NULL,
	.rto_fini       = NULL,
	.rto_pack_size  = fol_pack_size,
	.rto_pack       = fol_pack
};

#endif /* __KERNEL__ */

struct c2_rpc_item *c2_fop_to_rpc_item(struct c2_fop *fop)
{
	return &fop->f_item;
}
C2_EXPORTED(c2_fop_to_rpc_item);

struct c2_fop *c2_rpc_item_to_fop(const struct c2_rpc_item *item)
{
	return container_of(item, struct c2_fop, f_item);
}
C2_EXPORTED(c2_rpc_item_to_fop);

/* onwire_fmt
struct c2_fop_type *c2_item_type_to_fop_type
		    (const struct c2_rpc_item_type *item_type)
{
	struct c2_fop_type		*ftype;
	struct c2_fop_rpc_item_type	*fri_type;

	C2_PRE(item_type != NULL);

	fri_type = container_of(item_type, struct c2_fop_rpc_item_type,
				fri_i_type);
	ftype = fri_type->fri_f_type;
	return ftype;
}
C2_EXPORTED(c2_item_type_to_fop_type);
*/
struct c2_fop_type *c2_item_type_to_fop_type
		    (const struct c2_rpc_item_type *item_type)
{
	struct c2_fop_type		*ftype;
	int				 opcode;
	C2_PRE(item_type != NULL);

	opcode = item_type->rit_opcode;
	ftype = c2_fop_type_search(opcode);
	C2_ASSERT(ftype != NULL);
	return ftype;
}
C2_EXPORTED(c2_item_type_to_fop_type);

/** @} end of fop group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
