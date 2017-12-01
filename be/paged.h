/* -*- C -*- */
/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy.Bilenko@seagate.com>
 * Original creation date: 29-Nov-2017
 */

#pragma once

#ifndef __MERO_BE_PAGED_H__
#define __MERO_BE_PAGED_H__

/**
 * @defgroup PageD
 *
 * @{
 */

#include "lib/mutex.h"
#include "lib/tlist.h"

struct m0_be_pd_request_pages;
struct m0_be_reg_area;
struct m0_co_context;
struct m0_be_reg;
struct m0_be_op;
struct m0_fom;


struct m0_be_pd {
	struct m0_tl                   bp_address_spaces;
	struct m0_be_pd_request_queue  bp_reqq;
	struct                        *bp_fom;
};

struct m0_be_pd_page {
	void        *pp_page;
	void        *pp_cellar;
	m0_bcount_t  pp_size;
	m0_bcount_t  pp_ref;
	bool         pp_dirty;
};

struct m0_be_pd_address_space {
	struct m0_be_pd_page *pas_pages;
	m0_bcount_t           pas_pcount;
};

M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pd               *paged,
				       struct m0_be_pd_request_pages *pages);

/**
 * (struct m0_be_pd, struct m0_be_pd_pages)
 * M0_BE_PD_REQUEST_PAGES_FORALL(paged, page) {
 * }
 */
#define M0_BE_PD_PAGES_FORALL(paged, page)

M0_INTERNAL int m0_be_pd_as_init(struct m0_be_pd_address_space *asp,
				 m0_bcount_t                    page_size);
M0_INTERNAL int m0_be_pd_as_fini(struct m0_be_pd_address_space *asp);

M0_INTERNAL int m0_be_pd_as_attach(struct m0_be_pd *paged,
				   const void      *addr,
				   m0_bcount_t      size);

M0_INTERNAL void m0_be_pd_as_detach(const void *addr);

M0_INTERNAL int m0_be_pd_as_page_attach(struct m0_be_pd_address_space *asp,
					struct m0_be_pd_page          *page);

M0_INTERNAL int m0_be_pd_as_page_detach(struct m0_be_pd_address_space *asp,
					struct m0_be_pd_page          *page);

/* ------------------------------------------------------------------------- */

enum m0_be_pd_request_type {
	M0_PRT_READ,
	M0_PRT_WRITE,
};

struct m0_be_pd_request_pages {
	enum m0_be_pd_request_type prt_type;
	struct m0_be_reg_area     *prp_reg_area; /* for M0_PRT_WRITE requests */
	struct m0_be_reg           prp_reg;      /* for  M0_PRT_READ requests */
};

M0_INTERNAL void m0_be_pd_request_pages_init(struct m0_be_pd_request_pages *rqp,
					     enum m0_be_pd_request_type type);

/**
 * (struct m0_be_pd, struct m0_be_pd_request_pages)
 * M0_BE_PD_REQUEST_PAGES_FORALL(paged, page) {
 * }
 */
#define M0_BE_PD_REQUEST_PAGES_FORALL(paged, page)


struct m0_be_pd_request {
	struct m0_be_op               *prt_op;
	struct m0_be_pd_request_pages  prt_pages;
	struct m0_tl                  *prt_related_txs;
	struct m0_tlink                ptr_rq_link;
};

M0_INTERNAL void m0_be_pd_request_init(struct m0_be_pd_request       *request,
				       struct m0_be_pd_request_pages *pages,
				       struct m0_be_op               *op,
				       struct m0_tl                  *txs);

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request       *request);

/* ------------------------------------------------------------------------- */

struct m0_be_pd_request_queue {
	struct m0_mutex prq_lock;
	struct m0_tl    prq_queue;
};

M0_INTERNAL int m0_be_pd_request_queue_init(struct m0_be_pd_request_queue *rq);
M0_INTERNAL void m0_be_pd_request_queue_fini(struct m0_be_pd_request_queue *rq);


M0_INTERNAL int m0_be_pd_request_pop(struct m0_be_pd_request_queue   *request,
				     struct m0_fom                   *fom,
				     struct m0_co_context            *context);
M0_INTERNAL void m0_be_pd_request_push(struct m0_be_pd_request_queue *request);
M0_INTERNAL void m0_be_pd_request_wait(struct m0_be_pd_request_queue *request,
				       struct m0_fom                 *fom,
				       struct m0_co_context          *context);

/* ------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_pd_reg_get(struct m0_be_pd  *paged,
				  struct m0_be_reg *reg,
				  struct m0_be_op  *op);

M0_INTERNAL void m0_be_pd_reg_put(struct m0_be_pd        *paged,
				  const struct m0_be_reg *reg);

/* ------------------------------------------------------------------------- */

struct m0_be_pd_fom {
	struct m0_fom          bpf_gen;
	struct m0_reqh        *bpf_reqh;
	struct m0_be_pd       *bpf_pd;
};

M0_INTERNAL void m0_be_pd_fom_init(struct m0_be_pd_fom    *fom,
					 struct m0_be_pd  *pd,
					 struct m0_reqh   *reqh);

M0_INTERNAL void m0_be_pd_fom_fini(struct m0_be_pd_fom    *fom);

M0_INTERNAL void m0_be_pd_fom_mod_init(void);
M0_INTERNAL void m0_be_pd_fom_mod_fini(void);

/* ------------------------------------------------------------------------- */


/** @} end of PageD group */
#endif /* __MERO_BE_PAGED_H__ */

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
