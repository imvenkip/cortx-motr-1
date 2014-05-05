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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 18-Jul-2013
 */

#pragma once

#ifndef __MERO_BE_DOMAIN_H__
#define __MERO_BE_DOMAIN_H__

#include "be/engine.h"
#include "lib/tlist.h"

/**
 * @defgroup be
 *
 * @{
 */

struct m0_be_tx;
struct m0_be_0type;

struct m0_be_domain_cfg {
	struct m0_be_engine_cfg bc_engine;
	m0_bcount_t             bc_seg0_size;
};

struct m0_be_domain {
	struct m0_be_domain_cfg	bd_cfg;
	struct m0_be_engine	bd_engine;
	struct m0_tl            bd_0type_list;
	/** List of segments in this domain. First segment in which is seg0. */
	struct m0_tl            bd_seg_list;
	struct m0_stob         *bd_seg0_stob;
};

M0_INTERNAL int m0_be_domain_start(struct m0_be_domain *dom,
				  struct m0_be_domain_cfg *cfg);
M0_INTERNAL int m0_be_domain_init(struct m0_be_domain *dom);

M0_INTERNAL void m0_be_domain_fini(struct m0_be_domain *dom);

M0_INTERNAL struct m0_be_tx *m0_be_domain_tx_find(struct m0_be_domain *dom,
						  uint64_t id);

M0_INTERNAL struct m0_be_engine *m0_be_domain_engine(struct m0_be_domain *dom);
M0_INTERNAL
struct m0_be_seg *m0_be_domain_seg0_get(const struct m0_be_domain *dom);
M0_INTERNAL bool m0_be_domain_is_locked(const struct m0_be_domain *dom);

/**
 * Returns existing be-segment if @addr is inside it. Otherwize returns NULL.
 */
M0_INTERNAL struct m0_be_seg *m0_be_domain_seg(const struct m0_be_domain *dom,
					       const void *addr);
/**
 * Returns existing be-segment by its @id. If no segments found return NULL.
 */
M0_INTERNAL struct m0_be_seg *
m0_be_domain_seg_by_id(const struct m0_be_domain *dom, uint64_t id);

/* for internal be-usage only */
M0_INTERNAL void m0_be_domain__0type_register(struct m0_be_domain *dom,
					      struct m0_be_0type *type);
M0_INTERNAL void m0_be_domain__0type_unregister(struct m0_be_domain *dom,
						struct m0_be_0type *type);
M0_INTERNAL void m0_be_domain__seg_add(struct m0_be_domain *dom,
				       struct m0_be_seg *seg);
M0_INTERNAL void m0_be_domain__seg_del(struct m0_be_domain *dom,
				       struct m0_be_seg *seg);

/** @} end of be group */
#endif /* __MERO_BE_DOMAIN_H__ */

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
