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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/19/2010
 */
#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "lib/errno.h"
#include "fop/fop.h"
#include "fop/fom_long_lock.h" /* c2_fom_ll_global_init */

/**
   @addtogroup fop
   @{
 */

static const struct c2_fol_rec_type_ops c2_fop_fol_default_ops;

const struct c2_addb_ctx_type c2_fop_addb_ctx = {
	.act_name = "fop"
};

static const struct c2_addb_ctx_type c2_fop_type_addb_ctx = {
	.act_name = "fop-type"
};

static const struct c2_addb_loc c2_fop_addb_loc = {
	.al_name = "fop"
};

static struct c2_mutex fop_types_lock;
static struct c2_tl    fop_types_list;

C2_TL_DESCR_DEFINE(ft, "fop types", static, struct c2_fop_type,
		   ft_linkage,	ft_magix,
		   0xba11ab1ea5111dae /* bailable asilidae */,
		   0xd15ea5e0fed1f1ce /* disease of edifice */);

C2_TL_DEFINE(ft, static, struct c2_fop_type);

static size_t fop_data_size(const struct c2_fop *fop)
{
	return fop->f_type->ft_xt->xct_sizeof;
}

int c2_fop_data_alloc(struct c2_fop *fop)
{
	size_t nob;

	C2_PRE(fop->f_data.fd_data == NULL && fop->f_type != NULL);

	nob = fop_data_size(fop);
	fop->f_data.fd_data = c2_alloc(nob);

	return fop->f_data.fd_data == NULL ? -ENOMEM : 0;
}

void c2_fop_init(struct c2_fop *fop, struct c2_fop_type *fopt, void *data)
{

	C2_PRE(fop != NULL && fopt != NULL);

	fop->f_type = fopt;
	fop->f_data.fd_data = data;
	c2_addb_ctx_init(&fop->f_addb, &c2_fop_addb_ctx, &fopt->ft_addb);
	c2_list_link_init(&fop->f_link);

	c2_rpc_item_init(&fop->f_item, &fopt->ft_rpc_item_type);
	fop->f_item.ri_ops = &c2_fop_default_item_ops;
}

struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data)
{
	struct c2_fop *fop;
	int            err;

	C2_ALLOC_PTR(fop);
	if (fop != NULL) {
		c2_fop_init(fop, fopt, data);
		if (data == NULL) {
			err = c2_fop_data_alloc(fop);
			if (err != 0) {
				c2_free(fop);
				return NULL;
			}
		}
	}

	return fop;
}
C2_EXPORTED(c2_fop_alloc);

/**
   @todo Current implementation just frees the top level object;
   instead traverse and free entire tree of objects.
 */
void c2_fop_fini(struct c2_fop *fop)
{
	C2_ASSERT(fop != NULL);

	c2_rpc_item_fini(&fop->f_item);
	c2_addb_ctx_fini(&fop->f_addb);
	if (fop->f_data.fd_data != NULL)
		c2_free(fop->f_data.fd_data);
	c2_list_link_fini(&fop->f_link);
}

void c2_fop_free(struct c2_fop *fop)
{
	if (fop != NULL) {
		c2_fop_fini(fop);
		c2_free(fop);
	}
}

void *c2_fop_data(struct c2_fop *fop)
{
	return fop->f_data.fd_data;
}
C2_EXPORTED(c2_fop_data);

void c2_fop_type_fini(struct c2_fop_type *fopt)
{
	c2_fol_rec_type_unregister(&fopt->ft_rec_type);
	c2_mutex_lock(&fop_types_lock);
	c2_rpc_item_type_deregister(&fopt->ft_rpc_item_type);
	ft_tlink_del_fini(fopt);
	fopt->ft_magix = 0;
	c2_mutex_unlock(&fop_types_lock);
	c2_addb_ctx_fini(&fopt->ft_addb);
}
C2_EXPORTED(c2_fop_type_fini);

int c2_fop_type_init(struct c2_fop_type *ft,
		     const struct __c2_fop_type_init_args *args)
{
	struct c2_fol_rec_type  *fol_type;
	struct c2_rpc_item_type *rpc_type;

	C2_PRE(ft->ft_magix == 0);

	fol_type = &ft->ft_rec_type;
	rpc_type = &ft->ft_rpc_item_type;

	ft->ft_name         = args->name;
	ft->ft_xt           = args->xt;
	ft->ft_ops          = args->fop_ops;
	fol_type->rt_name   = args->name;
	fol_type->rt_opcode = args->opcode;
	fol_type->rt_ops    = args->fol_ops ?:
		&c2_fop_fol_default_ops;

	rpc_type->rit_opcode   = args->opcode;
	rpc_type->rit_flags    = args->rpc_flags;
	rpc_type->rit_ops      = args->rpc_ops ?:
		&c2_rpc_fop_default_item_type_ops;

	c2_fom_type_init(&ft->ft_fom_type, args->fom_ops, args->svc_type,
			 args->sm);
	c2_rpc_item_type_register(&ft->ft_rpc_item_type);
	c2_fol_rec_type_register(&ft->ft_rec_type);
	c2_addb_ctx_init(&ft->ft_addb, &c2_fop_type_addb_ctx,
			 &c2_addb_global_ctx);
	c2_mutex_lock(&fop_types_lock);
	ft_tlink_init_at(ft, &fop_types_list);
	c2_mutex_unlock(&fop_types_lock);
	return 0;
}
C2_EXPORTED(c2_fop_type_init);

int c2_fop_type_init_nr(const struct c2_fop_type_batch *batch)
{
	int result = 0;

	for (; batch->tb_type != NULL && result == 0; ++batch)
		result = c2_fop_type_init(batch->tb_type, &batch->tb_args);
	if (result != 0)
		c2_fop_type_fini_nr(batch);
	return result;
}

void c2_fop_type_fini_nr(const struct c2_fop_type_batch *batch)
{
	for (; batch->tb_type != NULL; ++batch) {
		if (batch->tb_type->ft_magix != 0)
			c2_fop_type_fini(batch->tb_type);
	}
}

struct c2_fop_type *c2_fop_type_next(struct c2_fop_type *ftype)
{
	struct c2_fop_type *rtype;

	c2_mutex_lock(&fop_types_lock);
	if (ftype == NULL) {
		/* Returns head of fop_types_list */
		rtype = ft_tlist_head(&fop_types_list);
	} else {
		/* Returns Next from fop_types_list */
		rtype = ft_tlist_next(&fop_types_list, ftype);
	}
	c2_mutex_unlock(&fop_types_lock);
	return rtype;
}

int c2_fops_init(void)
{
	ft_tlist_init(&fop_types_list);
	c2_mutex_init(&fop_types_lock);
	c2_fom_ll_global_init();
	return 0;
}

void c2_fops_fini(void)
{
	c2_mutex_fini(&fop_types_lock);
	ft_tlist_fini(&fop_types_list);
}

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

int c2_fop_fol_rec_add(struct c2_fop *fop, struct c2_fol *fol,
		       struct c2_db_tx *tx)
{
	return 0;
}

#else /* !__KERNEL__ */

static const struct c2_fol_rec_type_ops c2_fop_fol_default_ops;

int fop_fol_type_init(struct c2_fop_type *fopt)
{
	struct c2_fol_rec_type *rtype;

	C2_CASSERT(sizeof rtype->rt_opcode == sizeof
		   fopt->ft_rpc_item_type.rit_opcode);

	rtype = &fopt->ft_rec_type;
	rtype->rt_name   = fopt->ft_name;
	rtype->rt_opcode = fopt->ft_rpc_item_type.rit_opcode;
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
	C2_CASSERT(sizeof desc.rd_header.rh_opcode ==
		   sizeof fopt->ft_rpc_item_type.rit_opcode);

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

static size_t fol_pack_size(struct c2_fol_rec_desc *desc)
{
	struct c2_fop *fop = desc->rd_type_private;

	return fop_data_size(fop);
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

struct c2_fop_type *c2_item_type_to_fop_type
		    (const struct c2_rpc_item_type *item_type)
{
	C2_PRE(item_type != NULL);

	return container_of(item_type, struct c2_fop_type, ft_rpc_item_type);
}

/*
   See declaration for more information.
 */
void c2_fop_item_free(struct c2_rpc_item *item)
{
	struct c2_fop *fop;

	fop = c2_rpc_item_to_fop(item);
	/* c2_fop_free() internally calls c2_fop_fini() on the fop, which
	   calls c2_rpc_item_fini() on the rpc-item */
	c2_fop_free(fop);
}

const struct c2_rpc_item_ops c2_fop_default_item_ops = {
	.rio_free = c2_fop_item_free,
};
C2_EXPORTED(c2_fop_default_item_ops);

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
