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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 29-May-2013
 */

#include "be/seg.h"

#include "lib/misc.h"         /* M0_IN */
#include "lib/memory.h"       /* m0_alloc_aligned */
#include "lib/errno.h"        /* ENOMEM */

#include "be/seg_internal.h"  /* m0_be_seg_hdr */
#include "be/be.h"            /* m0_be_op */
#include "be/tx_regmap.h"     /* m0_be_reg_area */
#include "be/io.h"	      /* m0_be_io */

#include <sys/mman.h>         /* mmap */
#include <search.h>           /* twalk */

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL int m0_be_seg_create(struct m0_be_seg *seg,
				 m0_bcount_t size,
				 void *addr)
{
	struct m0_be_seg_hdr hdr;

	M0_PRE(seg->bs_state == M0_BSS_INIT);
	M0_PRE(seg->bs_stob->so_domain != NULL);
	M0_PRE(addr != NULL);
	M0_PRE(size > 0);

	hdr = (struct m0_be_seg_hdr) {
		.bh_addr = addr,
		.bh_size = size,
	};
	return m0_be_io_sync(seg->bs_stob, SIO_WRITE,
			     &hdr, M0_BE_SEG_HEADER_OFFSET, sizeof hdr);
}

M0_INTERNAL int m0_be_seg_destroy(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));

	/* XXX TODO: seg destroy ... */

	return 0;
}

M0_INTERNAL void m0_be_seg_init(struct m0_be_seg *seg,
				struct m0_stob *stob,
				struct m0_be_domain *dom)
{
	*seg = (struct m0_be_seg) {
		.bs_reserved = M0_BE_SEG_HEADER_OFFSET +
			       sizeof(struct m0_be_seg_hdr),
		.bs_domain   = dom,
		.bs_stob     = stob,
		.bs_state    = M0_BSS_INIT,
	};
}

M0_INTERNAL void m0_be_seg_fini(struct m0_be_seg *seg)
{
	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));
}

M0_INTERNAL bool m0_be__seg_invariant(const struct m0_be_seg *seg)
{
	return seg != NULL && seg->bs_addr != NULL && seg->bs_size > 0;
}

bool m0_be__reg_invariant(const struct m0_be_reg *reg)
{
	return reg != NULL && reg->br_seg != NULL &&
		reg->br_size > 0 && reg->br_addr != NULL &&
		m0_be_seg_contains(reg->br_seg, reg->br_addr) &&
		m0_be_seg_contains(reg->br_seg,
				   reg->br_addr + reg->br_size - 1);
}

M0_INTERNAL int m0_be_seg_open(struct m0_be_seg *seg)
{
	struct m0_be_seg_hdr  hdr;
	void                 *p;
	int                   rc;

	M0_PRE(M0_IN(seg->bs_state, (M0_BSS_INIT, M0_BSS_CLOSED)));
	rc = m0_be_io_sync(seg->bs_stob, SIO_READ,
			   &hdr, M0_BE_SEG_HEADER_OFFSET, sizeof hdr);
	if (rc != 0)
		return rc;
	/* XXX check for magic */

	p = mmap(hdr.bh_addr, hdr.bh_size, PROT_READ|PROT_WRITE,
		 MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (p != hdr.bh_addr)
		return -errno;

	rc = m0_be_io_sync(seg->bs_stob, SIO_READ, hdr.bh_addr, 0, hdr.bh_size);
	if (rc == 0) {
		seg->bs_size  = hdr.bh_size;
		seg->bs_addr  = hdr.bh_addr;
		seg->bs_state = M0_BSS_OPENED;
	} else {
		munmap(hdr.bh_addr, hdr.bh_size);
	}
	return rc;
}

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg)
{
	M0_PRE(seg->bs_state == M0_BSS_OPENED);

	munmap(seg->bs_addr, seg->bs_size);
	seg->bs_state = M0_BSS_CLOSED;
}

M0_INTERNAL void m0_be_reg_get(struct m0_be_reg *reg, struct m0_be_op *op)
{
	/* XXX not implemented */
}

M0_INTERNAL void m0_be_reg_get_fast(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
}

M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
}

M0_INTERNAL bool m0_be__reg_is_pinned(const struct m0_be_reg *reg)
{
	/* XXX not implemented */
	return true;
}

M0_INTERNAL bool m0_be_seg_contains(const struct m0_be_seg *seg, void *addr)
{
	return seg->bs_addr <= addr && addr < seg->bs_addr + seg->bs_size;
}

M0_INTERNAL bool m0_be_reg_eq(const struct m0_be_reg *r1,
			      const struct m0_be_reg *r2)
{
	return r1->br_seg == r2->br_seg &&
	       r1->br_size == r2->br_size &&
	       r1->br_addr == r2->br_addr;
}

M0_INTERNAL m0_bindex_t m0_be_seg_offset(const struct m0_be_seg *seg,
					 void *addr)
{
	M0_PRE(m0_be_seg_contains(seg, addr));
	return addr - seg->bs_addr;
}

M0_INTERNAL m0_bindex_t m0_be_reg_offset(const struct m0_be_reg *reg)
{
	return m0_be_seg_offset(reg->br_seg, reg->br_addr);
}

static int
be_seg_io(struct m0_be_reg *reg, void *ptr, enum m0_stob_io_opcode opcode)
{
	return m0_be_io_sync(reg->br_seg->bs_stob, opcode,
			     ptr, m0_be_reg_offset(reg), reg->br_size);
}

M0_INTERNAL int m0_be_seg__read(struct m0_be_reg *reg, void *dst)
{
	return be_seg_io(reg, dst, SIO_READ);
}

M0_INTERNAL int m0_be_seg__write(struct m0_be_reg *reg, void *src)
{
	return be_seg_io(reg, src, SIO_WRITE);
}

M0_INTERNAL int m0_be_reg__read(struct m0_be_reg *reg)
{
	return m0_be_seg__read(reg, reg->br_addr);
}

M0_INTERNAL int m0_be_reg__write(struct m0_be_reg *reg)
{
	return m0_be_seg__write(reg, reg->br_addr);
}

<<<<<<< HEAD

/* ------------------------------------------------------------------
 * Segment dictionary implementation
 * ------------------------------------------------------------------ */
#define BUF_INIT_STR(str) M0_BUF_INIT(strlen(str)+1, (str))
static m0_bcount_t dict_ksize(const void *key)
{
	return strlen(key)+1;
}

static m0_bcount_t dict_vsize(const void *data)
{
	return sizeof(void*);
}

static int dict_cmp(const void *key0, const void *key1)
{
	return strcmp(key0, key1);
}

static const struct m0_be_btree_kv_ops dict_ops = {
        .ko_ksize   = dict_ksize,
        .ko_vsize   = dict_vsize,
        .ko_compare = dict_cmp
};

static int tx_open(struct m0_be_seg *seg, struct m0_be_tx_credit *cred,
		   struct m0_be_tx *tx, struct m0_sm_group *grp)
{
        m0_be_tx_init(tx, 0, seg->bs_domain, grp, NULL, NULL, NULL, NULL);
        m0_be_tx_prep(tx, cred);
        m0_be_tx_open(tx);
        return m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				  M0_TIME_NEVER);
}

M0_INTERNAL int m0_be_seg_dict_lookup(struct m0_be_seg *seg,
				      const char *name,
				      void **out)
{
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	struct m0_buf       val  = M0_BUF_INIT(dict_vsize(*out), out);

	return M0_BE_OP_SYNC_RET(op, m0_be_btree_lookup(tree, &op, &key, &val),
				 bo_u.u_btree.t_rc);
}

M0_INTERNAL int m0_be_seg_dict_insert(struct m0_be_seg *seg,
				      struct m0_sm_group *grp,
				      const char *name,
				      void *value)
{
	M0_BE_TX_CREDIT(cred);
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;
        struct m0_be_tx    *tx;
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	struct m0_buf       val  = M0_BUF_INIT(dict_vsize(value), &value);
	int rc;

	M0_PRE(m0_be__seg_invariant(seg));

        M0_ALLOC_PTR(tx);
        if (tx == NULL)
                return -ENOMEM;

	m0_be_btree_init(tree, seg, &dict_ops);
	m0_be_btree_insert_credit(tree, 1, dict_ksize(name), dict_vsize(value),
				  &cred);
	rc = tx_open(seg, &cred, tx, grp);
        if (rc != 0 || m0_be_tx_state(tx) != M0_BTS_ACTIVE) {
                m0_be_tx_fini(tx);
                m0_free(tx);
                return -EFBIG;
        }

	M0_BE_OP_SYNC(op, m0_be_btree_insert(tree, tx, &op, &key, &val));

        m0_be_tx_close(tx);
        rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
        m0_be_tx_fini(tx);
        m0_free(tx);
        return rc;
}

M0_INTERNAL int m0_be_seg_dict_delete(struct m0_be_seg *seg,
				      struct m0_sm_group *grp,
				      const char *name)
{
	M0_BE_TX_CREDIT(cred);
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;
        struct m0_be_tx    *tx;
	struct m0_buf       key  = BUF_INIT_STR((char*)name);
	int rc;

	M0_PRE(m0_be__seg_invariant(seg));

        M0_ALLOC_PTR(tx);
        if (tx == NULL)
                return -ENOMEM;

	m0_be_btree_init(tree, seg, &dict_ops);
	m0_be_btree_delete_credit(tree, 1, dict_ksize(name), dict_vsize(NULL),
				  &cred);
	rc = tx_open(seg, &cred, tx, grp) ?:
		M0_BE_OP_SYNC_RET(op, m0_be_btree_delete(tree, tx, &op, &key),
				  bo_u.u_btree.t_rc);
        if (rc != 0 || m0_be_tx_state(tx) != M0_BTS_ACTIVE) {
                m0_be_tx_fini(tx);
                m0_free(tx);
                return -EFBIG;
        }

        m0_be_tx_close(tx);
        rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
        m0_be_tx_fini(tx);
        m0_free(tx);
        return rc;
}

M0_INTERNAL void m0_be_seg_dict_init(struct m0_be_seg *seg)
{
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;

	M0_PRE(m0_be__seg_invariant(seg));
	m0_be_btree_init(tree, seg, &dict_ops);
}

M0_INTERNAL int m0_be_seg_dict_create(struct m0_be_seg *seg,
				      struct m0_sm_group *grp)
{
	M0_BE_TX_CREDIT(cred);
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;
        struct m0_be_tx    *tx;
	int rc;

	M0_PRE(m0_be__seg_invariant(seg));

        M0_ALLOC_PTR(tx);
        if (tx == NULL)
                return -ENOMEM;

	m0_be_btree_init(tree, seg, &dict_ops);
	m0_be_btree_create_credit(tree, 1, &cred);
	m0_be_tx_credit_add
		(&cred, &M0_BE_TX_CREDIT_OBJ(1, sizeof(struct m0_be_seg_hdr)));
	rc = tx_open(seg, &cred, tx, grp);
        if (rc != 0 || m0_be_tx_state(tx) != M0_BTS_ACTIVE) {
                m0_be_tx_fini(tx);
                m0_free(tx);
                return -EFBIG;
        }

	M0_BE_OP_SYNC(op, m0_be_btree_create(tree, tx, &op));
	m0_be_tx_capture(tx, &M0_BE_REG(seg, sizeof(struct m0_be_seg_hdr),
				       seg->bs_addr));

        m0_be_tx_close(tx);
        rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
        m0_be_tx_fini(tx);
        m0_free(tx);
        return rc;
}

M0_INTERNAL int m0_be_seg_dict_destroy(struct m0_be_seg *seg,
				       struct m0_sm_group *grp)
{
	M0_BE_TX_CREDIT(cred);
	struct m0_be_btree *tree = &((struct m0_be_seg_hdr *)
				     seg->bs_addr)->bs_dict;
        struct m0_be_tx    *tx;
	int rc;

	M0_PRE(m0_be__seg_invariant(seg));

        M0_ALLOC_PTR(tx);
        if (tx == NULL)
                return -ENOMEM;

	m0_be_btree_init(tree, seg, &dict_ops);
	m0_be_btree_destroy_credit(tree, 1, &cred);
	m0_be_tx_credit_add
		(&cred, &M0_BE_TX_CREDIT_OBJ(1, sizeof(struct m0_be_seg_hdr)));
	rc = tx_open(seg, &cred, tx, grp);
        if (rc != 0 || m0_be_tx_state(tx) != M0_BTS_ACTIVE) {
                m0_be_tx_fini(tx);
                m0_free(tx);
                return -EFBIG;
        }

	M0_BE_OP_SYNC(op, m0_be_btree_destroy(tree, tx, &op));
	m0_be_tx_capture(tx, &M0_BE_REG(seg, sizeof(struct m0_be_seg_hdr),
				       seg->bs_addr));

        m0_be_tx_close(tx);
        rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
        m0_be_tx_fini(tx);
        m0_free(tx);
        return rc;
}

/** @} end of be group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
