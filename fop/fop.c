/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#undef M0_ADDB_RT_CREATE_DEFINITION
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "fop/fop_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FOP
#include "lib/trace.h"

#include "lib/cdefs.h" /* M0_EXPORTED */
#include "lib/memory.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/errno.h"
#include "mero/magic.h"
#include "fop/fop.h"
#include "fop/fom_long_lock.h" /* m0_fom_ll_global_init */

/**
   @addtogroup fop
   @{
 */

static const struct m0_fol_rec_type_ops m0_fop_fol_default_ops;
static int fop_fol_type_init(struct m0_fop_type *fopt);
static void fop_fol_type_fini(struct m0_fop_type *fopt);

/** FOP module global ctx */
struct m0_addb_ctx     m0_fop_addb_ctx;
static struct m0_mutex fop_types_lock;
static struct m0_tl    fop_types_list;

M0_TL_DESCR_DEFINE(ft, "fop types", static, struct m0_fop_type,
		   ft_linkage,	ft_magix,
		   M0_FOP_TYPE_MAGIC, M0_FOP_TYPE_HEAD_MAGIC);

M0_TL_DEFINE(ft, static, struct m0_fop_type);

static const char *fop_name(const struct m0_fop *fop)
{
	return fop->f_type->ft_name;
}

static size_t fop_data_size(const struct m0_fop *fop)
{
	return fop->f_type->ft_xt->xct_sizeof;
}

M0_INTERNAL int m0_fop_data_alloc(struct m0_fop *fop)
{
	M0_PRE(fop->f_data.fd_data == NULL && fop->f_type != NULL);

	fop->f_data.fd_data = m0_alloc(fop_data_size(fop));
	return fop->f_data.fd_data == NULL ? -ENOMEM : 0;
}

M0_INTERNAL void m0_fop_init(struct m0_fop *fop, struct m0_fop_type *fopt,
			     void *data, void (*fop_release)(struct m0_ref *))
{
	M0_ENTRY();
	M0_PRE(fop != NULL && fopt != NULL && fop_release != NULL);

	m0_ref_init(&fop->f_ref, 1, fop_release);
	fop->f_type = fopt;
	m0_rpc_item_init(&fop->f_item, &fopt->ft_rpc_item_type);
	fop->f_data.fd_data = data;
	M0_LOG(M0_DEBUG, "fop: %p %s", fop, fop_name(fop));

	M0_POST(m0_ref_read(&fop->f_ref) == 1);
	M0_LEAVE();
}

struct m0_fop *m0_fop_alloc(struct m0_fop_type *fopt, void *data)
{
	struct m0_fop *fop;

	M0_ALLOC_PTR(fop);
	if (fop == NULL)
		return NULL;

	m0_fop_init(fop, fopt, data, m0_fop_release);
	if (data == NULL) {
		int rc = m0_fop_data_alloc(fop);
		if (rc != 0) {
			m0_fop_put(fop);
			return NULL;
		}
	}

	M0_POST(m0_ref_read(&fop->f_ref) == 1);
	return fop;
}
M0_EXPORTED(m0_fop_alloc);

M0_INTERNAL void m0_fop_fini(struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	M0_ENTRY("fop: %p %s", fop, fop_name(fop));
	M0_PRE(M0_IN(m0_ref_read(&fop->f_ref), (0, 1)));

	m0_rpc_item_fini(&fop->f_item);
	if (fop->f_data.fd_data != NULL)
		m0_xcode_free(&M0_FOP_XCODE_OBJ(fop));
	M0_LEAVE();
}

M0_INTERNAL void m0_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;

	M0_ENTRY();
	M0_PRE(ref != NULL);

	fop = container_of(ref, struct m0_fop, f_ref);
	m0_fop_fini(fop);
	m0_free(fop);

	M0_LEAVE();
}

struct m0_fop *m0_fop_get(struct m0_fop *fop)
{
	uint64_t count = m0_ref_read(&fop->f_ref);

	M0_ENTRY("fop: %p %s [%llu -> %llu]", fop, fop_name(fop),
	         (unsigned long long)count, (unsigned long long)count + 1);
	M0_PRE(count > 0);

	m0_ref_get(&fop->f_ref);

	M0_LEAVE();
	return fop;
}
M0_EXPORTED(m0_fop_get);

void m0_fop_put(struct m0_fop *fop)
{
	uint64_t count = m0_ref_read(&fop->f_ref);

	M0_ENTRY("fop: %p %s [%llu -> %llu]", fop, fop_name(fop),
		 (unsigned long long)count, (unsigned long long)count - 1);
	M0_PRE(m0_ref_read(&fop->f_ref) > 0);

	m0_ref_put(&fop->f_ref);

	M0_LEAVE();
}
M0_EXPORTED(m0_fop_put);

void *m0_fop_data(struct m0_fop *fop)
{
	return fop->f_data.fd_data;
}
M0_EXPORTED(m0_fop_data);

uint32_t m0_fop_opcode(const struct m0_fop *fop)
{
	return fop->f_type->ft_rpc_item_type.rit_opcode;
}
M0_EXPORTED(m0_fop_opcode);

void m0_fop_type_fini(struct m0_fop_type *fopt)
{
	fop_fol_type_fini(fopt);
	m0_mutex_lock(&fop_types_lock);
	m0_rpc_item_type_deregister(&fopt->ft_rpc_item_type);
	ft_tlink_del_fini(fopt);
	fopt->ft_magix = 0;
	m0_mutex_unlock(&fop_types_lock);
}
M0_EXPORTED(m0_fop_type_fini);

int m0_fop_type_init(struct m0_fop_type *ft,
		     const struct __m0_fop_type_init_args *args)
{
	int			 rc;
	struct m0_rpc_item_type *rpc_type;

	M0_PRE(ft->ft_magix == 0);

	rpc_type = &ft->ft_rpc_item_type;

	ft->ft_name         = args->name;
	ft->ft_xt           = args->xt;
	ft->ft_ops          = args->fop_ops;

	rpc_type->rit_opcode = args->opcode;
	rpc_type->rit_flags  = args->rpc_flags;
	rpc_type->rit_ops    = args->rpc_ops ?: &m0_fop_default_item_type_ops;

	m0_fom_type_init(&ft->ft_fom_type, args->fom_ops, args->svc_type,
			 args->sm);
	rc = m0_rpc_item_type_register(&ft->ft_rpc_item_type) ?:
		fop_fol_type_init(ft);
	M0_ASSERT(rc == 0);
	m0_mutex_lock(&fop_types_lock);
	ft_tlink_init_at(ft, &fop_types_list);
	m0_mutex_unlock(&fop_types_lock);
	return rc;
}
M0_EXPORTED(m0_fop_type_init);

M0_INTERNAL int m0_fop_type_init_nr(const struct m0_fop_type_batch *batch)
{
	int result = 0;

	for (; batch->tb_type != NULL && result == 0; ++batch)
		result = m0_fop_type_init(batch->tb_type, &batch->tb_args);
	if (result != 0)
		m0_fop_type_fini_nr(batch);
	return result;
}

M0_INTERNAL void m0_fop_type_fini_nr(const struct m0_fop_type_batch *batch)
{
	for (; batch->tb_type != NULL; ++batch) {
		if (batch->tb_type->ft_magix != 0)
			m0_fop_type_fini(batch->tb_type);
	}
}

M0_INTERNAL struct m0_fop_type *m0_fop_type_next(struct m0_fop_type *ftype)
{
	struct m0_fop_type *rtype;

	m0_mutex_lock(&fop_types_lock);
	if (ftype == NULL) {
		/* Returns head of fop_types_list */
		rtype = ft_tlist_head(&fop_types_list);
	} else {
		/* Returns Next from fop_types_list */
		rtype = ft_tlist_next(&fop_types_list, ftype);
	}
	m0_mutex_unlock(&fop_types_lock);
	return rtype;
}

M0_INTERNAL int m0_fops_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_fop_mod);
	m0_addb_rec_type_register(&m0_addb_rt_fom_init);
	m0_addb_rec_type_register(&m0_addb_rt_fom_fini);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_fop_addb_ctx, &m0_addb_ct_fop_mod,
			 &m0_addb_proc_ctx);
	ft_tlist_init(&fop_types_list);
	m0_mutex_init(&fop_types_lock);
	m0_fom_ll_global_init();
	return 0;
}

M0_INTERNAL void m0_fops_fini(void)
{
	m0_addb_ctx_fini(&m0_fop_addb_ctx);
	m0_mutex_fini(&fop_types_lock);
	ft_tlist_fini(&fop_types_list);
}

/*
 * fop-fol interaction.
 */

#ifdef __KERNEL__

/* XXX for now */

static int fop_fol_type_init(struct m0_fop_type *fopt)
{
	return 0;
}

static void fop_fol_type_fini(struct m0_fop_type *fopt)
{
}

#else /* !__KERNEL__ */

static const struct m0_fol_rec_type_ops m0_fop_fol_default_ops;

static int fop_fol_type_init(struct m0_fop_type *fopt)
{
	struct m0_fol_rec_type *rtype;

	M0_CASSERT(sizeof rtype->rt_opcode == sizeof
		   fopt->ft_rpc_item_type.rit_opcode);

	rtype = &fopt->ft_rec_type;
	rtype->rt_name   = fopt->ft_name;
	rtype->rt_opcode = fopt->ft_rpc_item_type.rit_opcode;
	if (fopt->ft_ops != NULL && fopt->ft_ops->fto_rec_ops != NULL)
		rtype->rt_ops = fopt->ft_ops->fto_rec_ops;
	else
		rtype->rt_ops = &m0_fop_fol_default_ops;
	return m0_fol_rec_type_register(rtype);
}

static void fop_fol_type_fini(struct m0_fop_type *fopt)
{
	m0_fol_rec_type_unregister(&fopt->ft_rec_type);
}

M0_INTERNAL void m0_fop_fol_rec_desc_init(struct m0_fol_rec_desc *desc,
					  const struct m0_fop_type *fopt,
					  struct m0_fol *fol)
{
	M0_PRE(desc != NULL);

}

static size_t fol_pack_size(struct m0_fol_rec_desc *desc)
{
	struct m0_fop *fop = desc->rd_type_private;

	return fop_data_size(fop);
}

static void fol_pack(struct m0_fol_rec_desc *desc, void *buf)
{
	struct m0_fop *fop = desc->rd_type_private;

	memcpy(buf, m0_fop_data(fop), fol_pack_size(desc));
}

static const struct m0_fol_rec_type_ops m0_fop_fol_default_ops = {
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

struct m0_rpc_item *m0_fop_to_rpc_item(struct m0_fop *fop)
{
	return &fop->f_item;
}
M0_EXPORTED(m0_fop_to_rpc_item);

struct m0_fop *m0_rpc_item_to_fop(const struct m0_rpc_item *item)
{
	return container_of(item, struct m0_fop, f_item);
}

M0_INTERNAL struct m0_fop_type *m0_item_type_to_fop_type
    (const struct m0_rpc_item_type *item_type) {
	M0_PRE(item_type != NULL);

	return container_of(item_type, struct m0_fop_type, ft_rpc_item_type);
}

M0_INTERNAL m0_bcount_t m0_fop_data_size(struct m0_fop *fop)
{
	struct m0_xcode_ctx ctx;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);

	m0_xcode_ctx_init(&ctx, &M0_FOP_XCODE_OBJ(fop));
	return m0_xcode_length(&ctx);
}

M0_INTERNAL int m0_fop_encdec(struct m0_fop           *fop,
			      struct m0_bufvec_cursor *cur,
			      enum m0_bufvec_what      what)
{
	int		     rc;
	struct m0_xcode_ctx  xc_ctx;

	m0_xcode_ctx_init(&xc_ctx, &M0_FOP_XCODE_OBJ(fop));
	/* structure instance copy! */
	xc_ctx.xcx_buf   = *cur;
	xc_ctx.xcx_alloc = m0_xcode_alloc;

	rc = what == M0_BUFVEC_ENCODE ? m0_xcode_encode(&xc_ctx) :
					m0_xcode_decode(&xc_ctx);
	if (rc == 0) {
		if (what == M0_BUFVEC_DECODE)
			fop->f_data.fd_data =
				m0_xcode_ctx_top(&xc_ctx);
		*cur = xc_ctx.xcx_buf;
	}
	return rc;
}

/** @} end of fop group */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
